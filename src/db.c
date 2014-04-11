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
                    currentOperation->callback(currentOperation, currentOperation->error);
                }
            }
        }
    }

    static void _op_run_callbacks(uv_async_t *handle, int status)
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
            int res;
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

            if(res != ODBXUV_OP_STATUS_COMPLETED && operation->type != ODBXUV_OP_TYPE_DISCONNECT)
            {
                break;
            }

            operation = next;
        }
    }


    #define MAKE_ODBX_ERR(operation, result, func) \
        if(result < ODBX_ERR_SUCCESS) \
        { \
            operation ->error = result; \
            operation ->errorType = odbx_error_type( operation ->connection->handle, result ); \
            const char *error = odbx_error( operation ->connection->handle, result ); \
            operation ->errorString = malloc(strlen(error)+1); \
            strcpy( operation ->errorString,  error); \
            func; \
            return 0; \
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
        _op_run_callbacks_real(con); //Make sure we run, maby uv_async is lazy
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
                connection->async.data = connection;
                memset(&connection->worker, 0, sizeof(connection->worker));
                connection->worker.data = connection;

                uv_queue_work(connection->loop, &connection->worker, _op_run_operations, _op_after_run_operations);
                connection->workerStatus = ODBXUV_WORKER_RUNNING;
            }
        }
    }

    static odbxuv_operation_status_e _op_connect(odbxuv_op_t *req)
    {
        int result;
        odbxuv_op_connect_t *op = (odbxuv_op_connect_t *)req;
        assert(op->type == ODBXUV_OP_TYPE_CONNECT);

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
        assert(op->type == ODBXUV_OP_TYPE_DISCONNECT);

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

        op->connection->status = ODBXUV_CON_STATUS_IDLE;

        return 0;
    }

    static odbxuv_operation_status_e _op_capabilities(odbxuv_op_t *req)
    {
        int result;
        odbxuv_op_capabilities_t *op = (odbxuv_op_capabilities_t *)req;
        assert(op->type == ODBXUV_OP_TYPE_CAPABILITIES);

        result = odbx_capabilities(op->connection->handle, op->capabilities);

        MAKE_ODBX_ERR(op, result, {

        });

        op->error = result;

        return 0;
    }

    static odbxuv_operation_status_e _op_query(odbxuv_op_t *req)
    {
        int result;
        odbxuv_op_query_t *op = (odbxuv_op_query_t *)req;
        assert(op->type == ODBXUV_OP_TYPE_QUERY);

        result = odbx_query(op->connection->handle, op->query, 0);

        MAKE_ODBX_ERR(op, result, {
            op->queryResult->finished = 1;
        });

        odbxuv_connection_t *connection = op->connection;
        odbxuv_result_t *queryResult = op->queryResult;
        int chunkSize = op->chunkSize;
        odbxuv_query_fetch_e flags = op->flags;
        op->status = ODBXUV_OP_STATUS_COMPLETED;
        op = NULL;
        //TODO: fix error handling
        uv_async_send(&connection->async);

        unsigned char didGetInfo = 0;
        do
        {
            if(queryResult->queryResult != NULL)
            {
                result = odbx_result_finish(queryResult->queryResult);

                MAKE_ODBX_ERR(op, result, {

                });
            }

            result = odbx_result(
                connection->handle,
                &queryResult->queryResult,
                NULL,
                chunkSize);

            if (result < ODBX_ERR_SUCCESS)
            {
                if (odbx_error_type(connection->handle, result) > 0)
                {
                    continue; //Once more
                }
                else
                {
                    MAKE_ODBX_ERR(op, result, {

                    });
                }
            }

            if(didGetInfo == 0)
            {
                didGetInfo = 1;
                queryResult->columnCount = odbx_column_count(queryResult->queryResult);
                queryResult->affectedCount = odbx_rows_affected(queryResult->queryResult);

                if(queryResult->columns == NULL)
                {
                    {
                        size_t len = sizeof(odbxuv_column_info_t) * queryResult->columnCount;
                        queryResult->columns = malloc(len);
                        memset(queryResult->columns, 0, len); //TODO: manually init with 0 if we need it ?
                    }

                    int i;
                    for(i = 0; i < queryResult->columnCount; i++)
                    {
                        if(flags & ODBXUV_QUERY_FETCH_NAME)
                        {
                            const char *name = odbx_column_name(queryResult->queryResult, i);

                            queryResult->columns[i].name = malloc(strlen(name)+1);
                            strcpy(queryResult->columns[i].name, name);
                        }
                        if(flags & ODBXUV_QUERY_FETCH_TYPE)
                        {
                            queryResult->columns[i].type = odbx_column_type(queryResult->queryResult, i);
                        }
                    }
                }
            }

            switch(result)
            {
                case ODBX_RES_ROWS:
                    //fetch & see if there is more
                    while(ODBX_ROW_NEXT == (result = odbx_row_fetch(queryResult->queryResult)))
                    {
                        //Find an used row or create one
                        odbxuv_row_t **row = &queryResult->row;

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
                        for(i = 0; i < queryResult->columnCount; i++)
                        {
                            if(flags & ODBXUV_QUERY_FETCH_VALUE)
                            {
                                //Lazy init
                                if((*row)->value == NULL)
                                {
                                    size_t len = sizeof(char *) * queryResult->columnCount;
                                    (*row)->value = malloc(len);
                                    memset((*row)->value, 0, len);
                                }

                                if((*row)->value[i] != NULL)
                                {
                                    free((*row)->value[i]);
                                }

                                const char *value = odbx_field_value(queryResult->queryResult, i);

                                (*row)->value[i] = malloc(strlen(value)+1);
                                strcpy((*row)->value[i], value);
                            }

                            //TODO fetch lengths
                        }

                        (*row)->status = ODBXUV_ROW_STATUS_READ;

                        //We have inited
                        if(queryResult->async.data == queryResult)
                        {
                            uv_async_send(&queryResult->async);
                        }
                    }

                    MAKE_ODBX_ERR(op, result, {
                        
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
        while(1);

        escape:

        if(queryResult->queryResult != NULL)
        {
            result = odbx_result_finish(queryResult->queryResult);

            MAKE_ODBX_ERR(op, result, {

            });
        }

        if(queryResult->async.data == queryResult)
        {
            uv_async_send(&queryResult->async);
        }

        queryResult->finished = 1;

        return ODBXUV_OP_STATUS_COMPLETED;
    }

    static odbxuv_operation_status_e _op_escape(odbxuv_op_t *req)
    {
        int result;
        odbxuv_op_escape_t *op = (odbxuv_op_escape_t *)req;
        assert(op->type == ODBXUV_OP_TYPE_ESCAPE);

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

        op->error = result;
        return 0;
    }

    static void _init_op(int type, odbxuv_op_t *operation, odbxuv_connection_t *connection, odbxuv_op_fun fun, odbxuv_op_cb callback)
    {
        operation->type = type;
        operation->status = ODBXUV_OP_STATUS_NOT_STARTED;
        operation->next = NULL;
        operation->connection = connection;
        operation->operationFunction = fun;
        operation->callback = callback;
        operation->error = ODBX_ERR_SUCCESS;
        operation->errorString = NULL;
    }

    void _con_add_op(odbxuv_connection_t *connection, odbxuv_op_t *operation)
    {
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
    /**
     * API:
     */

    int odbxuv_init_connection(odbxuv_connection_t *connection, uv_loop_t *loop)
    {
        memset(connection, 0, sizeof(odbxuv_connection_t));
        connection->loop = loop;

        memset(&connection->async, 0, sizeof(uv_async_t));
        connection->async.data = connection;
        uv_async_init(connection->loop, &connection->async, _op_run_callbacks);

        return ODBX_ERR_SUCCESS;
    }

    int odbxuv_unref_connection(odbxuv_connection_t *connection)
    {
        assert(connection->status != ODBXUV_CON_STATUS_CONNECTED && "Cannot unref a connection in progress");


        uv_close((uv_handle_t *)&connection->async, NULL);

        if(connection->workerStatus == ODBXUV_WORKER_RUNNING)
        {
            assert(0);
            uv_cancel((uv_req_t *)&connection->worker);
        }

        return ODBX_ERR_SUCCESS;
    }


    void odbxuv_op_connect_free_info(odbxuv_op_connect_t *op)
    {
        #define freeAttr(name) \
        free((void *)op-> name ); \
        op-> name = NULL;

        freeAttr(host);
        freeAttr(port);

        freeAttr(backend);

        freeAttr(database);
        freeAttr(user);
        freeAttr(password);
    }

    int odbxuv_connect(odbxuv_connection_t *connection, odbxuv_op_connect_t *operation, odbxuv_op_connect_cb callback)
    {
        connection->status = ODBXUV_CON_STATUS_CONNECTING;

        {
            #define _copys(name) \
                char *name = malloc(strlen( operation->name )+1); \
                strcpy( name , operation-> name );
                _copys(host);
                _copys(port);
                _copys(backend);
                _copys(database);
                _copys(user);
                _copys(password);
            #undef _copys

            int method = operation->method;

            memset(operation, 0, sizeof(&operation));

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


        _init_op(ODBXUV_OP_TYPE_CONNECT, (odbxuv_op_t *)operation, connection, _op_connect, (odbxuv_op_cb)callback);

        _con_add_op(connection, (odbxuv_op_t *)operation);

        con_worker_check(connection);

        return ODBX_ERR_SUCCESS;
    }

    int odbxuv_disconnect(odbxuv_connection_t *connection, odbxuv_op_disconnect_t *operation, odbxuv_op_disconnect_cb callback)
    {
        connection->status = ODBXUV_CON_STATUS_DISCONNECTING;

        memset(operation, 0, sizeof(&operation));

        _init_op(ODBXUV_OP_TYPE_DISCONNECT, (odbxuv_op_t *)operation, connection, _op_disconnect, (odbxuv_op_cb)callback);

        _con_add_op(connection, (odbxuv_op_t *)operation);

        con_worker_check(connection);

        return ODBX_ERR_SUCCESS;
    }

    int odbxuv_free_error(odbxuv_op_t *operation)
    {
        if(operation->errorString)
        {
            free(operation->errorString);
            operation->errorString = NULL;
        }

        return ODBX_ERR_SUCCESS;
    }

    int odbxuv_capabilities(odbxuv_connection_t *connection, odbxuv_op_capabilities_t *operation, int capabilities, odbxuv_op_capabilities_cb callback)
    {
        assert(connection->status == ODBXUV_CON_STATUS_CONNECTED);

        memset(operation, 0, sizeof(&operation));
        _init_op(ODBXUV_OP_TYPE_CAPABILITIES, (odbxuv_op_t *)operation, connection, _op_capabilities, (odbxuv_op_cb)callback);

        operation->capabilities = capabilities;

        _con_add_op(connection, (odbxuv_op_t *)operation);

        con_worker_check(connection);

        return ODBX_ERR_SUCCESS;
    }

    int odbxuv_init_query(odbxuv_op_query_t *operation, odbxuv_result_t *result, odbxuv_query_fetch_e flags)
    {
        memset(result, 0, sizeof(*result));
        memset(operation, 0, sizeof(*operation));
        operation->queryResult = result;
        operation->flags = flags;
    }

    int odbxuv_query(odbxuv_connection_t *connection, odbxuv_op_query_t *operation, const char *query, odbxuv_op_query_cb callback)
    {
        assert(connection->status == ODBXUV_CON_STATUS_CONNECTED);
        assert(operation->queryResult != NULL && "Query was not initialized.");

        _init_op(ODBXUV_OP_TYPE_QUERY, (odbxuv_op_t *)operation, connection, _op_query, (odbxuv_op_cb)callback);

        char *q = malloc(strlen(query) + 1);
        strcpy(q, query);
        operation->query = q;

        operation->queryResult->connection = operation->connection;

        _con_add_op(connection, (odbxuv_op_t *)operation);

        con_worker_check(connection);

        return ODBX_ERR_SUCCESS;
    }

    static void _close_process(uv_handle_t *handle)
    {
        odbxuv_result_t *result = (odbxuv_result_t *)handle->data;
        result->cb(result, NULL);
    }

    static void _query_process_cb_real(odbxuv_result_t *result)
    {
        odbxuv_row_t *row = result->row;
        
        while(row)
        {
            result->cb(result, row);
            row = row->next;
        }

        if(result->finished == 1)
        {
            result->finished = 2;
            //Clean up async
            uv_close((uv_handle_t *)&result->async, _close_process);
        }
    }

    static void _query_process_cb(uv_async_t* handle, int status)
    {
        //TODO: run read callbacks here
        odbxuv_result_t *result = (odbxuv_result_t *)handle->data;
        _query_process_cb_real(result);
    }

    int odbxuv_query_process(odbxuv_result_t *result, odbxuv_fetch_cb onQueryRow)
    {
        memset(&result->async, 0, sizeof(uv_async_t));
        result->async.data = result;
        result->cb = onQueryRow;
        uv_async_init(result->connection->loop, &result->async, _query_process_cb);
        if(result->row != NULL)
        {
            _query_process_cb(&result->async, 0);
        }
    }

    int odbxuv_result_free(odbxuv_result_t *result)
    {
        assert(result->finished);
        odbxuv_row_t *row = result->row;
        result->row = NULL;

        while(row)
        {
            odbxuv_row_t *next = row->next;

            if(row->value)
            {
                int i;
                for(i = 0; i < result->columnCount; i++)
                {
                    free(row->value[i]);
                }

                free(row->value);
            }

            free(row);
            row = next;
        }

        if(result->columns)
        {
            int i;
            for(i = 0; i < result->columnCount; i++)
            {
                free(result->columns[i].name);
            }
            free(result->columns);
        }
    }

    void odbxuv_op_query_free_query(odbxuv_op_query_t* op)
    {
        if(op->queryResult && op->queryResult->finished)
        {
            _query_process_cb_real(op->queryResult);
        }

        if(op->query)
        {
            free((void *)op->query);
            op->query = NULL;
        }
    }

    int odbxuv_escape(odbxuv_connection_t *connection, odbxuv_op_escape_t *operation, const char *string, odbxuv_op_escape_cb callback)
    {
        assert(connection->status == ODBXUV_CON_STATUS_CONNECTED);

        memset(operation, 0, sizeof(&operation));
        _init_op(ODBXUV_OP_TYPE_ESCAPE, (odbxuv_op_t *)operation, connection, _op_escape, (odbxuv_op_cb)callback);

        char *q = malloc(strlen(string) + 1);
        strcpy(q, string);
        operation->string = q;

        _con_add_op(connection, (odbxuv_op_t *)operation);

        con_worker_check(connection);

        return ODBX_ERR_SUCCESS;
    }

    void odbxuv_op_escape_free_escape(odbxuv_op_escape_t* op)
    {
        if(op->string)
        {
            free((void *)op->string);
            op->string = NULL;
        }
    }