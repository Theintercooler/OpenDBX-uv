#include "odbxuv/db.h"
#include <assert.h>
#include <string.h>
#include <malloc.h>

    /**
     * Removes the finished tasks from the list and then runs callbacks for them.
     */
    static void _op_run_callbacks(uv_async_t *handle, int status)
    {
        odbxuv_connection_t *con = (odbxuv_connection_t *)handle->data;
        
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

    /**
     * Runs the pending operations on the connection
     */
    static void _op_run_operations(uv_work_t *req)
    {
        odbxuv_connection_t *con = (odbxuv_connection_t *)req->data;
        odbxuv_op_t *operation = con->operationQueue;
        
        while(operation)
        {
            if(operation->status == ODBXUV_OP_STATUS_NOT_STARTED)
            {
                operation->status = ODBXUV_OP_STATUS_IN_PROGRESS;
                operation->operationFunction(operation);
                operation->status = ODBXUV_OP_STATUS_COMPLETED;
                
                uv_async_send(&con->async);
            }
            
            operation = operation->next;
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
            return; \
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
   
    static void _op_connect(odbxuv_op_t *req)
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
    }
    
    static void _op_disconnect(odbxuv_op_t *req)
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
    }
    
    static void _op_capabilities(odbxuv_op_t *req)
    {
        int result;
        odbxuv_op_capabilities_t *op = (odbxuv_op_capabilities_t *)req;
        assert(op->type == ODBXUV_OP_TYPE_CAPABILITIES);
        
        result = odbx_capabilities(op->connection->handle, op->capabilities);
        
        MAKE_ODBX_ERR(op, result, {
            
        });
        
        op->error = result;
    }
    
    static void _op_query(odbxuv_op_t *req)
    {
        int result;
        odbxuv_op_query_t *op = (odbxuv_op_query_t *)req;
        assert(op->type == ODBXUV_OP_TYPE_QUERY);
        
        result = odbx_query(op->connection->handle, op->query, 0);
        
        MAKE_ODBX_ERR(op, result, {
            
        });
        
        result = odbx_result(
            op->connection->handle,
            &op->queryResult,
            NULL,
            0);
        
        MAKE_ODBX_ERR(op, result, {
            
        });
        
        op->error = result;
    }
    
    static void _op_escape(odbxuv_op_t *req)
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
    }
    
    static void _op_finish_result(odbxuv_op_t *req)
    {
        int result;
        odbxuv_op_finish_result_t *op = (odbxuv_op_finish_result_t *)req;
        assert(op->type == ODBXUV_OP_TYPE_FINISH_RESULT);
        
        result = odbx_result_finish(op->queryResult);
        
        MAKE_ODBX_ERR(op, result, {
            
        });
        
        op->error = result;
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
        connection->async.data = connection;
        
        uv_async_init(loop, &connection->async, _op_run_callbacks);
        uv_unref((uv_handle_t *)&connection->async);
        
        return ODBX_ERR_SUCCESS;
    }
    
    int odbxuv_unref_connection(odbxuv_connection_t *connection)
    {
        assert(connection->status != ODBXUV_CON_STATUS_CONNECTED && "Cannot unref a connection in progress");
        

        if(connection->workerStatus == ODBXUV_WORKER_RUNNING)
        {
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
    
    int odbxuv_query(odbxuv_connection_t *connection, odbxuv_op_query_t *operation, const char *query, odbxuv_op_query_cb callback)
    {
        assert(connection->status == ODBXUV_CON_STATUS_CONNECTED);
        
        memset(operation, 0, sizeof(&operation));
        _init_op(ODBXUV_OP_TYPE_QUERY, (odbxuv_op_t *)operation, connection, _op_query, (odbxuv_op_cb)callback);
        
        char *q = malloc(strlen(query) + 1);
        strcpy(q, query);
        operation->query = q;
        
        _con_add_op(connection, (odbxuv_op_t *)operation);

        con_worker_check(connection);
        
        return ODBX_ERR_SUCCESS;
    }
    
    void odbxuv_op_query_free_query(odbxuv_op_query_t* op)
    {
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

    int odbxuv_finish_result(odbxuv_connection_t *connection, odbxuv_op_finish_result_t *operation, odbx_result_t *queryResult, odbxuv_op_finish_result_cb callback)
    {
        assert(connection->status == ODBXUV_CON_STATUS_CONNECTED);
        
        memset(operation, 0, sizeof(&operation));
        _init_op(ODBXUV_OP_TYPE_FINISH_RESULT, (odbxuv_op_t *)operation, connection, _op_finish_result, (odbxuv_op_cb)callback);
        
        operation->queryResult = queryResult;
        
        _con_add_op(connection, (odbxuv_op_t *)operation);

        con_worker_check(connection);
        
        return ODBX_ERR_SUCCESS;
    }