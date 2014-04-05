#include "odbxuv/db.h"
#include <assert.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include "uv.h"

uv_loop_t *loop;
odbxuv_connection_t connection;

void onDisconnect(odbxuv_op_disconnect_t *req, int status)
{
    if(status < ODBX_ERR_SUCCESS)
    {
        printf("Failed to disconnect status: %i (%i)-> %s\n", status, req->errorType, req->errorString);
        odbxuv_free_error((odbxuv_op_t *)req);
    }
    
    odbxuv_unref_connection(req->connection);
    free(req);
}

void onFinishResult(odbxuv_op_finish_result_t *req, int status)
{
    if(status < ODBX_ERR_SUCCESS)
    {
        printf("Failed to query status: %i (%i)-> %s\n", status, req->errorType, req->errorString);
        odbxuv_free_error((odbxuv_op_t *)req);
    }
    
    static int cbNum = 0;
    cbNum ++;
    
    if(cbNum == 10)
    {
        odbxuv_op_disconnect_t *op = (odbxuv_op_disconnect_t *)malloc(sizeof(odbxuv_op_disconnect_t));
        odbxuv_disconnect(req->connection, op, onDisconnect);
    }
    
    free(req);
}

void onQuery(odbxuv_op_query_t *req, int status)
{
    if(status < ODBX_ERR_SUCCESS)
    {
        printf("Failed to query status: %i (%i)-> %s\n", status, req->errorType, req->errorString);
        odbxuv_free_error((odbxuv_op_t *)req);
    }
    
    int columnCount = odbx_column_count(req->queryResult);
    
    printf("Column count: %i\n", columnCount);
    
    int i = 0;
    while(odbx_row_fetch(req->queryResult))
    {
        printf("Row #%i:\n", i);
        
        int j = 0;
        for(; j < columnCount; j++)
        {
            printf("\tField #%i: %s\n", j, odbx_field_value(req->queryResult, j));
        }
        
        i++;
    }
    
    odbxuv_op_query_free_query(req);
    
    {
        odbxuv_op_finish_result_t *op = (odbxuv_op_finish_result_t *)malloc(sizeof(odbxuv_op_finish_result_t));
        odbxuv_finish_result(req->connection, op, req->queryResult, onFinishResult);
    }
    
    free(req);
}

void onEscape(odbxuv_op_escape_t *req, int status)
{
    if(status < ODBX_ERR_SUCCESS)
    {
        printf("Failed to escape status: %i (%i)-> %s\n", status, req->errorType, req->errorString);
        odbxuv_free_error((odbxuv_op_t *)req);
    }
    
    printf("Escaped to: %s(%lu)\n", req->string, strlen(req->string));
    
    int i = 0;
    for(; i < 10; i++)
    {
        odbxuv_op_query_t *op = (odbxuv_op_query_t *)malloc(sizeof(odbxuv_op_query_t));
        const char *string = "SELECT * FROM test";
        odbxuv_query(req->connection, op, string, onQuery);
    }
    
    odbxuv_op_escape_free_escape(req);    
    free(req);
}

int i =0;
void onCapatibilities(odbxuv_op_capabilities_t *req, int status)
{
    i++;
    if(status < ODBX_ERR_SUCCESS)
    {
        printf("compatibility status: %i (%i)-> %s\n", status, req->errorType, req->errorString);
        odbxuv_free_error((odbxuv_op_t *)req);
    }
    else
    {
        printf("I have %i = %s(%i)\n", req->capabilities, status == ODBX_ENABLE ? "enabled" : (status == ODBX_DISABLE ? "disabled" : "unkown"), status);
    }
    
    if(i == 2)
    {
        odbxuv_op_escape_t *op = (odbxuv_op_escape_t *)malloc(sizeof(odbxuv_op_escape_t));
        odbxuv_escape(req->connection, op, "\"hello world\"", onEscape);
    }
    
    free(req);
}

void onConnect(odbxuv_op_connect_t *req, int status)
{
    if(status < ODBX_ERR_SUCCESS)
    {
        printf("Connect status: %i (%i)-> %s\n", status, req->errorType, req->errorString);
        odbxuv_free_error((odbxuv_op_t *)req);
    }
    else
    {
        {
            odbxuv_op_capabilities_t *op = (odbxuv_op_capabilities_t *)malloc(sizeof(odbxuv_op_capabilities_t));
            odbxuv_capabilities(req->connection, op, ODBX_CAP_BASIC, onCapatibilities);
        }
        
        {
            odbxuv_op_capabilities_t *op = (odbxuv_op_capabilities_t *)malloc(sizeof(odbxuv_op_capabilities_t));
            odbxuv_capabilities(req->connection, op, ODBX_CAP_LO, onCapatibilities);
        }
    }
    
    odbxuv_op_connect_free_info(req);
}

int main()
{
    loop = uv_default_loop();
    
    odbxuv_init_connection(&connection, loop);
    
    odbxuv_op_connect_t op;
    
    op.backend = "mysql";
    op.host = "192.168.1.190";
    op.port = "";
    
    op.database = "test";
    op.user = "test";
    op.password = "test";
    
    op.method = ODBX_BIND_SIMPLE;
    
    odbxuv_connect(&connection, &op, onConnect);
    
    uv_run(loop, UV_RUN_DEFAULT);
    
    uv_loop_delete(uv_default_loop());
    return 0;
}
