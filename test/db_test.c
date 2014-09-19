#include "odbxuv/db.h"
#include <assert.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include "uv.h"

uv_loop_t *loop;
odbxuv_connection_t connection;

void onDisconnect(odbxuv_handle_t *handle)
{
    //odbxuv_connection_t *con = (odbxuv_connection_t *)handle;
    printf("Disconnected %p\n", handle);
    //free(con); No need to free here, we did not allocate the connection on the heap
}

static int queriesFinished = 0;

void onQueryRow(odbxuv_op_query_t *result, odbxuv_row_t *row, int status)
{
    printf("onQueryRow\n");

    if(status < ODBX_ERR_SUCCESS)
    {
        printf("Fetch error %i (%i) %s (status:%i)\n", result->error->error, result->error->errorType, result->error->errorString, result->fetchStatus);
        odbxuv_free_error((odbxuv_handle_t *)result);
    }

    if(row)
    {
        printf("Row!:");
        int i;
        for(i = 0; i < result->columnCount; i++)
        {
            printf("%s(%i)(%i) = %s; ", result->columns[i].name, result->columns[i].type, i, row->value[i]);
        }
        printf("\n");
    }
    else
    {
        queriesFinished ++;

        if(queriesFinished == 10)
        {
            odbxuv_close((odbxuv_handle_t *)result->connection, onDisconnect);
        }

        printf("Finished query %p\n", result);
        odbxuv_free_handle((odbxuv_handle_t *)result);
        free(result);
    }
}

void onQuery(odbxuv_op_query_t *req, int status)
{
    if(status < ODBX_ERR_SUCCESS)
    {
        printf("Failed to query status: %i (%i)-> %s\n", req->error->error, req->error->errorType, req->error->errorString);
        odbxuv_free_handle((odbxuv_handle_t *)req);
        free(req);

        queriesFinished ++;

        if(queriesFinished == 10)
        {
            odbxuv_close((odbxuv_handle_t *)req->connection, onDisconnect);
        }
    }
    else
    {
        odbxuv_query_process(req, onQueryRow);
    }
}

void onEscape(odbxuv_op_escape_t *req, int status)
{
    if(status < ODBX_ERR_SUCCESS)
    {
        printf("Failed to escape status: %i (%i)-> %s\n", req->error->error, req->error->errorType, req->error->errorString);
        odbxuv_free_error((odbxuv_handle_t *)req);
    }

    printf("Escaped to: %s(%lu)\n", req->string, strlen(req->string));

    int i = 0;
    for(; i < 10; i++)
    {
        odbxuv_op_query_t *op = (odbxuv_op_query_t *)malloc(sizeof(odbxuv_op_query_t));
        const char *string = "SHOW DATABASES;";
        odbxuv_query(req->connection, op, string, ~0, onQuery);
    }

    odbxuv_free_handle((odbxuv_handle_t *)req);
    free(req);
}

int i =0;
void onCapatibilities(odbxuv_op_capabilities_t *req, int status)
{
    i++;
    if(status < ODBX_ERR_SUCCESS)
    {
        printf("compatibility status: %i (%i)-> %s\n", req->error->error, req->error->errorType, req->error->errorString);
        odbxuv_free_error((odbxuv_handle_t *)req);
    }
    else
    {
        printf("I have %i = %s(%i)\n", req->capabilities, req->result == ODBX_ENABLE ? "enabled" : (req->result == ODBX_DISABLE ? "disabled" : "unkown"), req->result);
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
        printf("Connect status: %i (%i)-> %s\n", req->error->error, req->error->errorType, req->error->errorString);
        odbxuv_free_error((odbxuv_handle_t *)req);
        odbxuv_close((odbxuv_handle_t *)req->connection, onDisconnect);
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

    odbxuv_free_handle((odbxuv_handle_t *)req);
}

static void _walk_cb(uv_handle_t *handle, void *data)
{
    printf("Still open: %lu %i\n", (ulong)handle, handle->type);
    assert(0);
}

int main()
{
    loop = uv_default_loop();

    odbxuv_init_connection(&connection, loop);

    odbxuv_op_connect_t op;

    //NOTE: sqlite doesn't seem to generate proper errors.
    op.backend = "mysql";
    op.host = "";
    op.port = "";

    op.database = "test";
    op.user = "test";
    op.password = "test";

    op.method = ODBX_BIND_SIMPLE;

    odbxuv_connect(&connection, &op, onConnect);

    uv_run(loop, UV_RUN_DEFAULT);

    uv_walk(loop, _walk_cb, NULL);

    uv_loop_delete(loop);
    return 0;
}
