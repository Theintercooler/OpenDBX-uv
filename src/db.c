#include "odbxuv/db.h"
#include <assert.h>
#include <string.h>
#include <malloc.h>

/**
 * Removes the finished tasks from the list and then runs callbacks for them.
 */
static void _op_run_callbacks_real(odbxuv_connection_t *con)
{
    if(!con->operationQueue) return;

    odbxuv_op_t *oldQueue = con->operationQueue;
    odbxuv_op_t *firstPendingOperation = oldQueue;

    // Build a new operation queue
    {
        //Find the first operation that is pending
        while(firstPendingOperation)
        {
            if(firstPendingOperation->status != ODBXUV_OP_STATUS_COMPLETED) break;
            firstPendingOperation = firstPendingOperation->next;
        }

        con->operationQueue = firstPendingOperation;
    }

    // Run all the callbacks, assumes the operation is freed inside the callback
    {
        odbxuv_op_t *firstOperation = oldQueue;

        while(firstOperation && firstOperation->status == ODBXUV_OP_STATUS_COMPLETED && firstOperation != firstPendingOperation)
        {
            odbxuv_op_t *currentOperation = firstOperation;
            firstOperation = currentOperation->next;

            if(currentOperation->callback)
            {
                currentOperation->callback(currentOperation, currentOperation->error ? currentOperation->error->error : ODBX_ERR_SUCCESS);
            }
        }
    }
}

static void _op_run_callbacks(uv_async_t *handle)
{
    _op_run_callbacks_real((odbxuv_connection_t *)handle->data);
}

/**
 * Runs the pending operations on the connection
 */
static void _op_run_operations(uv_work_t *req)
{
    odbxuv_connection_t *con = (odbxuv_connection_t *)req->data;
    odbxuv_op_t *operation = con->operationQueue;

    while(operation)
    {
        int res = ODBXUV_OP_STATUS_COMPLETED;
        odbxuv_op_t *next = operation->next;
        if(operation->status == ODBXUV_OP_STATUS_NOT_STARTED)
        {
            operation->status = ODBXUV_OP_STATUS_IN_PROGRESS;
            res = operation->operationFunction(operation);

            if(res != ODBXUV_OP_STATUS_COMPLETED)
            {
                operation->status = ODBXUV_OP_STATUS_COMPLETED;

                uv_async_send(&con->async);
            }
        }

        if(res != ODBXUV_OP_STATUS_COMPLETED && operation->type != ODBXUV_HANDLE_TYPE_OP_DISCONNECT)
        {
            break;
        }

        operation = next;
    }
}

static void _handle_make_error(odbxuv_handle_t *handle, int errorNum, int errorType, const char *errorString)
{
    odbxuv_error_t *error = malloc(sizeof(odbxuv_error_t));
    memset(error, 0, sizeof(*error));
    error->error = errorNum;
    error->errorType = errorType;
    char *msg = malloc(strlen(errorString)+1);
    strcpy(msg, errorString);
    error->errorString  = msg;
    handle->error = error;
    assert(errorNum != -ODBX_ERR_PARAM && "Internal error");
}

/**
 * \internal
 * \private
 */
#define MAKE_ODBX_ERR(operation, result, func) \
    if(result < ODBX_ERR_SUCCESS) \
    { \
        _handle_make_error((odbxuv_handle_t *)operation, result, odbx_error_type( operation->connection->handle, result), odbx_error(operation->connection->handle, result )); \
        func; \
        return 0; \
    }

#define SET_0_COPY_DATA(obj)                        \
    {                                               \
        assert(obj && "Object is NULL");            \
        void *data = obj->data;                     \
        memset(obj, 0, sizeof(*obj));               \
        obj->data = data;                           \
    }


static void con_worker_check(odbxuv_connection_t *connection);


/**
 * called after the worker thread finished.
 * checks if the worker should be restarted
 */
static void _op_after_run_operations(uv_work_t *req, int status)
{
    odbxuv_connection_t *con = (odbxuv_connection_t *)req->data;

    con->workerStatus = ODBXUV_WORKER_IDLE;
    _op_run_callbacks_real(con); //Make sure we run, maybe uv_async is lazy
    con_worker_check(con);
}

/**
 * Start the worker function in another thread when there is work to do.
 */
static void con_worker_check(odbxuv_connection_t *connection)
{
    if(connection->workerStatus == ODBXUV_WORKER_IDLE)
    {
        unsigned char havePendingRequests = 0;

        {
            odbxuv_op_t *operation = connection->operationQueue;
            while(operation)
            {
                if(operation->status == ODBXUV_OP_STATUS_NOT_STARTED)
                {
                    havePendingRequests = 1;
                    break;
                }
                operation = operation->next;
            }
        }

        if(havePendingRequests > 0 && connection->workerStatus == ODBXUV_WORKER_IDLE)
        {
            connection->workerStatus = ODBXUV_WORKER_RUNNING;
            memset(&connection->worker, 0, sizeof(connection->worker));
            connection->worker.data = connection;

            uv_queue_work(connection->loop, &connection->worker, _op_run_operations, _op_after_run_operations);
            connection->workerStatus = ODBXUV_WORKER_RUNNING;
        }
    }
}

/*
 * Up next, functions that implement certain types of operations.
 */

static odbxuv_operation_status_e _op_connect(odbxuv_op_t *req)
{
    int result;
    odbxuv_op_connect_t *op = (odbxuv_op_connect_t *)req;
    assert(op->type == ODBXUV_HANDLE_TYPE_OP_CONNECT);

    result = odbx_init(&op->connection->handle, op->backend, op->host, op->port);

    MAKE_ODBX_ERR(op, result, {
        odbx_finish(op->connection->handle);
        op->connection->handle = NULL;
        op->connection->status = ODBXUV_CON_STATUS_FAILED;
    });

    result = odbx_bind(op->connection->handle, op->database, op->user, op->password, op->method);

    MAKE_ODBX_ERR(op, result, {
        odbx_unbind(op->connection->handle);
        odbx_finish(op->connection->handle);
        op->connection->handle = NULL;
        op->connection->status = ODBXUV_CON_STATUS_FAILED;
    });

    op->connection->status = ODBXUV_CON_STATUS_CONNECTED;

    return 0;
}

static odbxuv_operation_status_e _op_disconnect(odbxuv_op_t *req)
{
    int result;
    odbxuv_op_disconnect_t *op = (odbxuv_op_disconnect_t *)req;
    assert(op->type == ODBXUV_HANDLE_TYPE_OP_DISCONNECT);

    result = odbx_unbind(req->connection->handle);

    MAKE_ODBX_ERR(op, result, {
        odbx_finish(op->connection->handle);
        op->connection->handle = NULL;
        op->connection->status = ODBXUV_CON_STATUS_IDLE;
    });

    result = odbx_finish(op->connection->handle);

    MAKE_ODBX_ERR(op, result, {
        op->connection->handle = NULL;
        op->connection->status = ODBXUV_CON_STATUS_IDLE;
    });

    op->connection->status = ODBXUV_CON_STATUS_DISCONNECTED;

    return 0;
}

static odbxuv_operation_status_e _op_capabilities(odbxuv_op_t *req)
{
    int result;
    odbxuv_op_capabilities_t *op = (odbxuv_op_capabilities_t *)req;
    assert(op->type == ODBXUV_HANDLE_TYPE_OP_CAPABILITIES);

    result = odbx_capabilities(op->connection->handle, op->capabilities);

    MAKE_ODBX_ERR(op, result, {

    });

    op->result = result;

    return 0;
}

static odbxuv_operation_status_e _op_query(odbxuv_op_t *req)
{
    int result;
    odbxuv_op_query_t *op = (odbxuv_op_query_t *)req;
    assert(op->type == ODBXUV_HANDLE_TYPE_OP_QUERY);


    result = odbx_query(op->connection->handle, op->query, 0);

    MAKE_ODBX_ERR(op, result, {
        op->fetchStatus = ODBXUV_FETCH_STATUS_ERROR_BEFORE;
    });

    op->status = ODBXUV_OP_STATUS_COMPLETED;
    op->fetchStatus = ODBXUV_FETCH_STATUS_RUNNING;
    uv_async_send(&op->connection->async); //Note: the callback should not free the op!

    unsigned char didGetInfo = 0;
    do
    {
        if(op->resultHandle != NULL)
        {
            result = odbx_result_finish(op->resultHandle);

            MAKE_ODBX_ERR(op, result, {
                op->error->error = -result;
                op->fetchStatus = ODBXUV_FETCH_STATUS_ERROR_FINISH;
            });
        }

        result = odbx_result(
            op->connection->handle,
            &op->resultHandle,
            NULL,
            op->chunkSize);


        MAKE_ODBX_ERR(op, result, {
            op->error->error = -result;
            op->fetchStatus = ODBXUV_FETCH_STATUS_ERROR_RESULT;
        });

        if(didGetInfo == 0)
        {
            didGetInfo = 1;
            op->columnCount = odbx_column_count(op->resultHandle);
            op->affectedCount = odbx_rows_affected(op->resultHandle);

            if(op->columns == NULL && (op->flags & ODBXUV_QUERY_FETCH_NAME || op->flags & ODBXUV_QUERY_FETCH_TYPE))
            {
                {
                    size_t len = sizeof(odbxuv_column_info_t) * op->columnCount;
                    op->columns = malloc(len);
                    memset(op->columns, 0, len); //TODO: manually init with 0 if we need it ?
                }

                int i;
                for(i = 0; i < op->columnCount; i++)
                {
                    if(op->flags & ODBXUV_QUERY_FETCH_NAME)
                    {
                        const char *name = odbx_column_name(op->resultHandle, i);

                        op->columns[i].name = malloc(strlen(name)+1);
                        strcpy(op->columns[i].name, name);
                    }
                    if(op->flags & ODBXUV_QUERY_FETCH_TYPE)
                    {
                        op->columns[i].type = odbx_column_type(op->resultHandle, i);
                    }
                }
            }
        }

        switch(result)
        {
            case ODBX_RES_ROWS:
                //fetch & see if there is more
                while(ODBX_ROW_NEXT == (result = odbx_row_fetch(op->resultHandle)))
                {
                    //Find a unused row or create one
                    odbxuv_row_t **row = &op->row;

                    do
                    {
                        if(*row == NULL)
                        {
                            odbxuv_row_t *newRow = malloc(sizeof(odbxuv_row_t));
                            memset(newRow, 0, sizeof(odbxuv_row_t));
                            newRow->status = ODBXUV_ROW_STATUS_NONE;
                            *row = newRow;
                            break;
                        }

                        if((*row)->status == ODBXUV_ROW_STATUS_PROCESSED)
                        {
                            break;
                        }

                        row = &(*row)->next;
                    }
                    while(1);

                    (*row)->status = ODBXUV_ROW_STATUS_READING;

                    int i;
                    for(i = 0; i < op->columnCount; i++)
                    {
                        if(op->flags & ODBXUV_QUERY_FETCH_VALUE)
                        {
                            //Lazy init
                            if((*row)->value == NULL)
                            {
                                size_t len = sizeof(char *) * op->columnCount;
                                (*row)->value = malloc(len);
                                memset((*row)->value, 0, len);
                            }

                            if((*row)->value[i] != NULL)
                            {
                                free((*row)->value[i]);
                            }

                            const char *value = odbx_field_value(op->resultHandle, i);

                            if(value)
                            {
                                (*row)->value[i] = malloc(strlen(value)+1);
                                strcpy((*row)->value[i], value);
                            }
                            else
                            {
                                (*row)->value[i] = NULL;
                            }
                        }

                        //TODO fetch lengths
                    }

                    (*row)->status = ODBXUV_ROW_STATUS_READ;

                    //We have inited
                    if(op->asyncStatus == 1)
                    {
                        uv_async_send(&op->async);
                    }

                    if(op->fetchStatus == ODBXUV_FETCH_STATUS_CANCELLED) goto escape;
                }

                MAKE_ODBX_ERR(op, result, {
                    op->error->error = -result;
                    op->fetchStatus = ODBXUV_FETCH_STATUS_ERROR_FETCH;
                });

                continue;
                break;

            default: //Assert on this?
            case ODBX_RES_NOROWS:
            case ODBX_RES_DONE:
                goto escape;
                break;
        }
    }
    while(op->fetchStatus != ODBXUV_FETCH_STATUS_CANCELLED);

    escape:

    if(op->resultHandle != NULL)
    {
        result = odbx_result_finish(op->resultHandle);

        MAKE_ODBX_ERR(op, result, {
            op->error->error = -result;
            op->fetchStatus = ODBXUV_FETCH_STATUS_ERROR_FINISH;
        });
    }

    if(op->asyncStatus == 1)
    {
        uv_async_send(&op->async);
    }

    op->fetchStatus = ODBXUV_FETCH_STATUS_FINISHED;

    return ODBXUV_OP_STATUS_COMPLETED;
}

static odbxuv_operation_status_e _op_escape(odbxuv_op_t *req)
{
    int result;
    odbxuv_op_escape_t *op = (odbxuv_op_escape_t *)req;
    assert(op->type == ODBXUV_HANDLE_TYPE_OP_ESCAPE);

    unsigned long inlen = strlen(op->string);
    unsigned long outlen = 2 * (inlen+1);

    const char *in = op->string;
    char *out = malloc(outlen + 1);
    memset(out, 0, outlen + 1);

    result = odbx_escape(op->connection->handle, in, inlen, out, &outlen);

    op->string = out;

    free((void *)in);

    MAKE_ODBX_ERR(op, result, {

    });

    return 0;
}

/**
 * Initialize an operation from the arguments and default values
 */
static void _init_op(int type, odbxuv_op_t *operation, odbxuv_connection_t *connection, odbxuv_op_fun fun, odbxuv_op_cb callback)
{
    operation->type = type;
    operation->status = ODBXUV_OP_STATUS_NOT_STARTED;
    operation->next = NULL;
    operation->connection = connection;
    operation->operationFunction = fun;
    operation->callback = callback;
    operation->error = NULL;
}

/**
 * Push an operation to the connection's operation queue
 */
static void _con_add_op(odbxuv_connection_t *connection, odbxuv_op_t *operation)
{
    assert(connection->status != ODBXUV_CON_STATUS_DISCONNECTING && "Cannot add operations while disconnecting");

    if(connection->operationQueue == NULL)
    {
        connection->operationQueue = operation;
    }
    else
    {
        odbxuv_op_t *currentOperation = connection->operationQueue;
        while(currentOperation->next != NULL)
        {
            currentOperation = currentOperation->next;
        }
        currentOperation->next = operation;
    }
}

/*
 * API:
 */

int odbxuv_init_connection(odbxuv_connection_t *connection, uv_loop_t *loop)
{
    SET_0_COPY_DATA(connection);
    connection->loop = loop;
    connection->type = ODBXUV_HANDLE_TYPE_CONNECTION;
    connection->workerStatus = ODBXUV_WORKER_IDLE;

    memset(&connection->async, 0, sizeof(uv_async_t));
    connection->async.data = connection;
    uv_async_init(connection->loop, &connection->async, _op_run_callbacks);

    return ODBX_ERR_SUCCESS;
}

int odbxuv_connect(odbxuv_connection_t *connection, odbxuv_op_connect_t *operation, odbxuv_op_connect_cb callback)
{
    assert(connection->status == ODBXUV_CON_STATUS_IDLE || connection->status == ODBXUV_CON_STATUS_DISCONNECTED);
    connection->status = ODBXUV_CON_STATUS_CONNECTING;

    {
        #define _copys(name) \
        char *name = NULL; \
        if (operation->name) \
        { \
            name = malloc(strlen( operation->name )+1); \
            strcpy( name , operation-> name ); \
        }
            _copys(host);
            _copys(port);
            _copys(backend);
            _copys(database);
            _copys(user);
            _copys(password);
        #undef _copys

        int method = operation->method;

        SET_0_COPY_DATA(operation);

        #define _copys(name) \
            operation-> name = name;
            _copys(host);
            _copys(port);
            _copys(backend);
            _copys(database);
            _copys(user);
            _copys(password);
        #undef _copys

        operation->method = method;
    }

    _init_op(ODBXUV_HANDLE_TYPE_OP_CONNECT, (odbxuv_op_t *)operation, connection, _op_connect, (odbxuv_op_cb)callback);

    _con_add_op(connection, (odbxuv_op_t *)operation);

    con_worker_check(connection);

    return ODBX_ERR_SUCCESS;
}

int odbxuv_disconnect(odbxuv_connection_t *connection, odbxuv_op_disconnect_t *operation, odbxuv_op_disconnect_cb callback)
{
    assert(connection->status == ODBXUV_CON_STATUS_CONNECTED);

    SET_0_COPY_DATA(operation);

    _init_op(ODBXUV_HANDLE_TYPE_OP_DISCONNECT, (odbxuv_op_t *)operation, connection, _op_disconnect, (odbxuv_op_cb)callback);

    _con_add_op(connection, (odbxuv_op_t *)operation);
    connection->status = ODBXUV_CON_STATUS_DISCONNECTING;

    con_worker_check(connection);

    return ODBX_ERR_SUCCESS;
}

int odbxuv_capabilities(odbxuv_connection_t *connection, odbxuv_op_capabilities_t *operation, int capabilities, odbxuv_op_capabilities_cb callback)
{
    assert(connection->status == ODBXUV_CON_STATUS_CONNECTED);

    memset(operation, 0, sizeof(&operation));
    _init_op(ODBXUV_HANDLE_TYPE_OP_CAPABILITIES, (odbxuv_op_t *)operation, connection, _op_capabilities, (odbxuv_op_cb)callback);

    operation->capabilities = capabilities;

    _con_add_op(connection, (odbxuv_op_t *)operation);

    con_worker_check(connection);

    return ODBX_ERR_SUCCESS;
}

int odbxuv_query(odbxuv_connection_t *connection, odbxuv_op_query_t *operation, const char *query, odbxuv_query_fetch_e flags, odbxuv_op_query_cb callback)
{
    assert(connection->status == ODBXUV_CON_STATUS_CONNECTED);

    SET_0_COPY_DATA(operation);
    _init_op(ODBXUV_HANDLE_TYPE_OP_QUERY, (odbxuv_op_t *)operation, connection, _op_query, (odbxuv_op_cb)callback);

    operation->flags = flags;

    char *q = malloc(strlen(query) + 1);
    strcpy(q, query);
    operation->query = q;
    operation->fetchStatus = ODBXUV_FETCH_STATUS_NONE;

    _con_add_op(connection, (odbxuv_op_t *)operation);

    con_worker_check(connection);

    return ODBX_ERR_SUCCESS;
}

static void _query_process_close(uv_handle_t *handle)
{
    odbxuv_op_query_t *op = (odbxuv_op_query_t *)handle->data;

    assert(op->type == ODBXUV_HANDLE_TYPE_OP_QUERY && "Close callback for non query.");

    int status = op->error ? op->error->error : 0;
    switch(op->fetchStatus)
    {
        case ODBXUV_FETCH_STATUS_ERROR_FETCH:
        case ODBXUV_FETCH_STATUS_ERROR_FINISH:
        case ODBXUV_FETCH_STATUS_ERROR_RESULT:
            if(status > 0)
            {
                op->error->error = -op->error->error;
                status = op->error->error;
            }
            break;
        default:
            break;
    }

    op->asyncStatus = 3;
    op->cb(op, NULL, status);
}

static void _query_process_cb_real(odbxuv_op_query_t *result)
{
    odbxuv_row_t *row = result->row;

    while(row)
    {
        result->cb(result, row, 0);
        result->fetchCallbackStatus = result->fetchCallbackStatus == ODBXUV_FETCH_CB_STATUS_NONE ? ODBXUV_FETCH_CB_STATUS_CALLED : result->fetchCallbackStatus;
        row = row->next;
    }

    if(result->fetchStatus == ODBXUV_FETCH_STATUS_RUNNING) return;

    if(result->asyncStatus == 1)
    {
        result->asyncStatus = 2;

        //Clean up async
        uv_close((uv_handle_t *)&result->async, _query_process_close);
    }
}

static void _query_process_cb(uv_async_t* handle)
{
    odbxuv_op_query_t *result = (odbxuv_op_query_t *)handle->data;
    _query_process_cb_real(result);
}

int odbxuv_query_process(odbxuv_op_query_t *result, odbxuv_fetch_cb onQueryRow)
{
    assert(result->asyncStatus == 0 && "We are already fetching on this handle");
    memset(&result->async, 0, sizeof(uv_async_t));
    result->async.data = result;
    result->cb = onQueryRow;
    uv_async_init(result->connection->loop, &result->async, _query_process_cb);
    result->asyncStatus = 1;

    _query_process_cb(&result->async);

    return ODBX_ERR_SUCCESS;
}

int odbxuv_escape(odbxuv_connection_t *connection, odbxuv_op_escape_t *operation, const char *string, odbxuv_op_escape_cb callback)
{
    assert(connection->status == ODBXUV_CON_STATUS_CONNECTED);

    SET_0_COPY_DATA(operation);
    _init_op(ODBXUV_HANDLE_TYPE_OP_ESCAPE, (odbxuv_op_t *)operation, connection, _op_escape, (odbxuv_op_cb)callback);

    char *q = malloc(strlen(string) + 1);
    strcpy(q, string);
    operation->string = q;

    _con_add_op(connection, (odbxuv_op_t *)operation);

    con_worker_check(connection);

    return ODBX_ERR_SUCCESS;
}

typedef struct _odbxuv_closing_data_s
{
    odbxuv_connection_t *connection;
    odbxuv_close_cb cb;
    odbxuv_error_t *error;
} _odbxuv_closing_data_t;

static void _close_connection_async(uv_handle_t *handle)
{
    _odbxuv_closing_data_t *data = (_odbxuv_closing_data_t *)handle->data;
    handle->data = NULL;
    data->connection->error = data->error;
    data->cb((odbxuv_handle_t *)data->connection);
    free(data);
}

static void _close_connection(odbxuv_op_disconnect_t *op, int status)
{
    odbxuv_close_cb cb = (odbxuv_close_cb)op->data;
    odbxuv_connection_t *connection = op->connection;

    assert(connection->workerStatus == ODBXUV_WORKER_IDLE && "Worker did not close after disconnect");
    {
        _odbxuv_closing_data_t *data = malloc(sizeof(_odbxuv_closing_data_t));
        data->connection = connection;
        data->cb = cb;
        data->error = op->error;
        connection->async.data = data;//Worker is not running, we can abuse this
        uv_close((uv_handle_t *)&connection->async, _close_connection_async);
    }

    free(op);
}

void odbxuv_close(odbxuv_handle_t* handle, odbxuv_close_cb callback)
{
    switch(handle->type)
    {
        case ODBXUV_HANDLE_TYPE_CONNECTION:
        {
            odbxuv_connection_t *con = (odbxuv_connection_t *)handle;
            odbxuv_op_disconnect_t *op = (odbxuv_op_disconnect_t *)malloc(sizeof(odbxuv_op_disconnect_t));
            op->data = callback;

            if(con->status == ODBXUV_CON_STATUS_CONNECTED)
            {
                odbxuv_disconnect(con, op, _close_connection);
            }
            else
            {
                op->connection = con;
                _close_connection(op, 0);
            }
        }
        break;

        case ODBXUV_HANDLE_TYPE_OP_QUERY:
            callback(handle); //Nothing to do
        break;

        default:
            assert(0 && "Invalid handle type to close");
            callback(handle);
            break;
    }
}

void odbxuv_free_error(odbxuv_handle_t *operation)
{
    if(operation->error != NULL)
    {
        free(operation->error);
        operation->error = NULL;
    }
}

#define ODBXUV_FREE_STRING(var) \
    if(var != NULL)             \
    {                           \
        free((void *)var);      \
        var = NULL;             \
    }

void odbxuv_free_handle(odbxuv_handle_t* handle)
{
    switch(handle->type)
    {
        case ODBXUV_HANDLE_TYPE_OP_QUERY:
        {
            odbxuv_op_query_t *query = (odbxuv_op_query_t *)handle;
            assert(query->fetchStatus != ODBXUV_FETCH_STATUS_RUNNING && "Can't run free while fetching");
            ODBXUV_FREE_STRING(query->query);

            odbxuv_row_t *row = query->row;
            query->row = NULL;

            while(row)
            {
                odbxuv_row_t *next = row->next;

                if(row->value)
                {
                    int i;
                    for(i = 0; i < query->columnCount; i++)
                    {
                        if(row->value[i])
                        {
                            free(row->value[i]);
                        }
                    }

                    free(row->value);
                }

                free(row);
                row = next;
            }

            if(query->columns)
            {
                int i;
                for(i = 0; i < query->columnCount; i++)
                {
                    free(query->columns[i].name);
                }
                free(query->columns);
            }
        }
        break;

        case ODBXUV_HANDLE_TYPE_OP_ESCAPE:
        {
            odbxuv_op_escape_t *op = (odbxuv_op_escape_t *)handle;

            ODBXUV_FREE_STRING(op->string);
        }
        break;

        case ODBXUV_HANDLE_TYPE_CONNECTION:
            break;

        case ODBXUV_HANDLE_TYPE_OP_CONNECT:
        {
            odbxuv_op_connect_t *op = (odbxuv_op_connect_t *)handle;
            ODBXUV_FREE_STRING(op->host);
            ODBXUV_FREE_STRING(op->port);
            ODBXUV_FREE_STRING(op->backend);
            ODBXUV_FREE_STRING(op->database);
            ODBXUV_FREE_STRING(op->user);
            ODBXUV_FREE_STRING(op->password);
        }
        break;

        case ODBXUV_HANDLE_TYPE_OP_DISCONNECT:
            // Nothing to do
            break;

        default:
            assert(0 && "Invalid handle type");
            break;
    }
}
