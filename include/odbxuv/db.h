#ifdef __cplusplus
extern "C"
{
#endif
    /**
     * \file odbxuv/db.h
     * OpenDBX UV
     */

    #include <odbx.h>
    #include "uv.h"

    typedef struct odbxuv_op_s odbxuv_op_t;

    /**
     * All the states a connection can be in.
     */
    typedef enum odbxuv_connection_status_enum
    {
        ODBXUV_CON_STATUS_IDLE = 0,
        ODBXUV_CON_STATUS_CONNECTING,
        ODBXUV_CON_STATUS_CONNECTED,
        ODBXUV_CON_STATUS_DISCONNECTING,
        ODBXUV_CON_STATUS_FAILED
    } odbxuv_connection_status_e;

    /**
     * The statuses a worker can be in.
     */
    typedef enum odbxuv_worker_status_enum
    {
        ODBXUV_WORKER_IDLE = 0,
        ODBXUV_WORKER_RUNNING,
    } odbxuv_worker_status_e;

    /**
     * The statuses a row can be in.
     */
    typedef enum odbxuv_row_status_enum
    {
        ODBXUV_ROW_STATUS_NONE = 0,
        ODBXUV_ROW_STATUS_READING,
        ODBXUV_ROW_STATUS_READ,
        ODBXUV_ROW_STATUS_PROCESSING,
        ODBXUV_ROW_STATUS_PROCESSED
    } odbxuv_row_status_e;

    /**
     * The different meta fetch modes
     */
    typedef enum odbxuv_query_fetch_enum
    {
        ODBXUV_QUERY_FETCH_NAME         = 1 << 0,
        ODBXUV_QUERY_FETCH_TYPE         = 1 << 1,
        ODBXUV_QUERY_FETCH_VALUE       = 1 << 2,
    } odbxuv_query_fetch_e;

    /**
     * A connection object.
     * This represents the connection to the database and contains all the worker information.
     */
    typedef struct odbxuv_connection_s
    {
        /**
         * The current status of the connection.
         * \note Read only
         */
        odbxuv_connection_status_e status;

        /**
         * The hanlde to the OpenDBX connection.
         * \private
         */
        odbx_t *handle;

        /**
         * The queue of the worker.
         * \private
         */
        odbxuv_op_t *operationQueue;

        /**
         * The main loop in which callbacks are fired
         */
        uv_loop_t *loop;

        /**
         * The async callback caller.
         * \private
         */
        uv_async_t async;

        /**
         * The worker that runs the OpenDBX calls in the background.
         * \private
         */
        uv_work_t worker;

        /**
         * The current status of the worker
         * \note Read only
         */
        odbxuv_worker_status_e workerStatus;

        /**
         * Userdata
         * \public
         */
        void *data;
    } odbxuv_connection_t;

    /**
     * All the different types of operations
     * Use \p ODBXUV_OP_CUSTOM to add custom operation types
     */
    typedef enum odbxuv_operation_type_enum
    {
        ODBXUV_OP_TYPE_NONE = 0,
        ODBXUV_OP_TYPE_CONNECT,
        ODBXUV_OP_TYPE_DISCONNECT,
        ODBXUV_OP_TYPE_CAPABILITIES,
        ODBXUV_OP_TYPE_QUERY,
        ODBXUV_OP_TYPE_FETCH,
        ODBXUV_OP_TYPE_ESCAPE,
        ODBXUV_OP_TYPE_FINISH_RESULT,
        ODBXUV_OP_TYPE_CUSTOM,
    } odbxuv_operation_type_e;

    /**
     * All the different statuses an operation can be in.
     * When an operation is fired it usually starts with \p ODBXUV_OP_STATUS_NOT_STARTED.
     * Once the worker starts running the operation it is set to \p ODBXUV_OP_STATUS_IN_PROGRESS
     * And then when finished it is left as \p ODBXUV_OP_STATUS_COMPLETED
     */
    typedef enum odbxuv_operation_status_enum
    {
        ODBXUV_OP_STATUS_NONE = 0,
        ODBXUV_OP_STATUS_NOT_STARTED,
        ODBXUV_OP_STATUS_IN_PROGRESS,
        ODBXUV_OP_STATUS_COMPLETED,
    } odbxuv_operation_status_e;

    typedef struct odbxuv_op_s odbxuv_op_t;
    typedef struct odbxuv_op_connect_s odbxuv_op_connect_t;
    typedef struct odbxuv_op_disconnect_s odbxuv_op_disconnect_t;
    typedef struct odbxuv_op_capabilities_s odbxuv_op_capabilities_t;
    typedef struct odbxuv_op_escape_s odbxuv_op_escape_t;
    typedef struct odbxuv_op_query_s odbxuv_op_query_t;
    typedef struct odbxuv_result_s odbxuv_result_t;
    typedef struct odbxuv_row_s odbxuv_row_t;
    typedef struct odbxuv_column_info_s odbxuv_column_info_t;
    typedef struct odbxuv_op_finish_result_s odbxuv_op_finish_result_t;

    /**
     * The function that runs the actual odbxuv operation.
     * This is invoked by the background worker.
     */
    typedef odbxuv_operation_status_e (*odbxuv_op_fun)(odbxuv_op_t *);

    /**
     * A general callback function.
     * Status contains the value of \p op->error for easy error checking.
     * \warning Don't forget to call ::odbxuv_free_error when necessary
     */
    typedef void (*odbxuv_op_cb) (odbxuv_op_t *op, int status);

    /**
     * Operation callback invoked after a connection attempt.
     * \sa odbxuv_op_cb
     */
    typedef void (*odbxuv_op_connect_cb) (odbxuv_op_connect_t *op, int status);

    /**
     * Operation callback invoked after disconnect attempt.
     * \sa odbxuv_op_cb
     */
    typedef void (*odbxuv_op_disconnect_cb) (odbxuv_op_disconnect_t *op, int status);

    /**
     * Operation callback invoked after querying the capabilities.
     * \sa odbxuv_op_cb
     */
    typedef void (*odbxuv_op_capabilities_cb) (odbxuv_op_capabilities_t *op, int status);

    /**
     * Operation callback invoked after escaping a string.
     * \warning Don't forget to call ::odbxuv_op_escape_free_escape in the callback
     * \sa odbxuv_op_cb
     */
    typedef void (*odbxuv_op_escape_cb) (odbxuv_op_escape_t *op, int status);

    /**
     * Operation callback invoked after querying the database
     * \warning Don't forget to call ::odbxuv_op_query_free_query and ::odbxuv_finish_result in the callback
     * \sa odbxuv_op_cb
     */
    typedef void (*odbxuv_op_query_cb) (odbxuv_op_query_t *op, int status);
    typedef void (*odbxuv_fetch_cb) (odbxuv_result_t *result, odbxuv_row_t *row);


    /**
     * The default fields of an operation
     * \internal
     */
    #define ODBXUV_OP_BASE_FIELDS               \
        /**                                     \
         * The type of the operation            \
         * \note Read only                      \
         */                                     \
        odbxuv_operation_type_e type;           \
        \
        /**                                     \
         * The current status of the operation  \
         * \note Read only                      \
         */                                     \
        odbxuv_operation_status_e status;       \
        \
        /**                                     \
         * The current status/error code        \
         * \note Read only                      \
         */                                     \
        int error;                              \
        \
        /**                                     \
         * The type of the error                \
         * \note Read only                      \
         */                                     \
        int errorType;                          \
        \
        /**                                     \
         * The current error string or \p NULL  \
         * \note Read only                      \
         * \sa odbxuv_free_error                \
         */                                     \
        char *errorString;                      \
        \
        /**                                     \
         * The next operation in queue.         \
         * \private                             \
         */                                     \
        odbxuv_op_t *next;                      \
        \
        /**                                     \
         * The current connection               \
         * \private                             \
         */                                     \
        odbxuv_connection_t *connection;        \
        \
        /**                                     \
         * The function that runs the actual operation. \
         * \private                             \
         */                                     \
        odbxuv_op_fun operationFunction;        \
        \
        /**                                     \
         * The callback function                \
         * \private                             \
         */                                     \
        odbxuv_op_cb callback;                  \
        \
        /**                                     \
         * Userdata                             \
         * \public                              \
         */                                     \
        void *data;


    /**
     * A basic operation
     */
    typedef struct odbxuv_op_s
    {
        ODBXUV_OP_BASE_FIELDS
    } odbxuv_op_t;

    /**
     * A disconnect operation
     */
    typedef struct odbxuv_op_disconnect_s
    {
        ODBXUV_OP_BASE_FIELDS
    } odbxuv_op_disconnect_t;

    /**
     * A connect operation
     * \warning Don't forget to run ::odbxuv_op_connect_free_info
     * \sa odbxuv_op_connect_free_info
     */
    typedef struct odbxuv_op_connect_s
    {
        ODBXUV_OP_BASE_FIELDS

        /**
         * The hostname to connect to.
         */
        const char *host;

        /**
         * The port to connect to as string/
         */
        const char *port;

        /**
         * The backend name to connect to.
         */
        const char *backend;

        /**
         * The database to connect to
         */
        const char *database;

        /**
         * The user to connect to the database with
         */
        const char *user;

        /**
         * The password to use
         */
        const char *password;

        /**
         * The method to connect to the database
         */
        int method;
    } odbxuv_op_connect_t;

    /**
     * A capabilities request operation.
     * The \p error field contains the status of the capability.
     */
    typedef struct odbxuv_op_capabilities_s
    {
        ODBXUV_OP_BASE_FIELDS

        /**
         * The capabilities to query for.
         */
        int capabilities;
    } odbxuv_op_capabilities_t;

    /**
     * A string escape operation
     * \warning Don't forget to call ::odbxuv_op_escape_free_escape afterwards
     */
    typedef struct odbxuv_op_escape_s
    {
        ODBXUV_OP_BASE_FIELDS

        /**
         * The string to escape
         */
        const char *string;
    } odbxuv_op_escape_t;

    /**
     * A query operation
     * \warning Don't forget to call ::odbxuv_op_query_free_query and ::odbxuv_finish_result afterwards
     */
    typedef struct odbxuv_op_query_s
    {
        ODBXUV_OP_BASE_FIELDS

        /**
         * The query string.
         */
        const char *query;

        /**
         * The result object when the query succeeded.
         */
        odbxuv_result_t *queryResult;

        /**
         * Query fetch flags
         */
        odbxuv_query_fetch_e flags;

        /**
         * The amount of rows to fetch at once
         */
        int chunkSize;
    } odbxuv_op_query_t;

    typedef struct odbxuv_result_s
    {
        odbxuv_connection_t *connection;
        odbx_result_t *queryResult;
        unsigned int columnCount;
        unsigned int affectedCount;
        odbxuv_column_info_t *columns;
        odbxuv_row_t *row;
        odbxuv_fetch_cb cb;
        uv_async_t async;
        int finished;
        void *data;
    } odbxuv_result_t;

    typedef struct odbxuv_column_info_s
    {
        char *name;
        int type;
    } odbxuv_column_info_t;

    typedef struct odbxuv_row_s
    {
        odbxuv_row_status_e status;
        char **value;
        odbxuv_row_t *next;
    } odbxuv_row_t;
    /**
     * Finishes the query
     */
    typedef struct odbxuv_op_finish_result_s
    {
        ODBXUV_OP_BASE_FIELDS
        /**
         * The query result to finish
         */
        odbx_result_t *queryResult;
    } odbxuv_op_finish_result_t;

    /**
     * \defgroup odbxuv Odbxuv global functions
     * \{
     */

    /**
     * Initializes the connection.
     * \public
     */
    int odbxuv_init_connection(odbxuv_connection_t *connection, uv_loop_t *loop);

    /**
     * Removes the connection from the loop
     * \public
     */
    int odbxuv_unref_connection(odbxuv_connection_t *connection);

    /**
     * Frees the error string
     * \public
     */
    int odbxuv_free_error(odbxuv_op_t *operation);

    /**
     * Connects to the database using the credentials specified in the \p operation.
     * \note all the credentials are internally copied
     * \public
     */
    int odbxuv_connect(odbxuv_connection_t *connection, odbxuv_op_connect_t *operation, odbxuv_op_connect_cb callback);

    /**
     * Frees the connection credentials from the \p operation
     * \public
     */
    void odbxuv_op_connect_free_info(odbxuv_op_connect_t *operation);

    /**
     * Disconnects from the database.
     * \note This does not call ::odbxuv_unref_connection
     * \public
     */
    int odbxuv_disconnect(odbxuv_connection_t *connection, odbxuv_op_disconnect_t *operation, odbxuv_op_disconnect_cb callback);

    /**
     * Requests if a capability is available.
     * \public
     */
    int odbxuv_capabilities(odbxuv_connection_t *connection, odbxuv_op_capabilities_t *operation, int capabilities, odbxuv_op_capabilities_cb callback);

    /**
     * Escapes a string
     * \note String is internally copied
     * \public
     */
    int odbxuv_escape(odbxuv_connection_t *connection, odbxuv_op_escape_t *operation, const char *string, odbxuv_op_escape_cb callback);

    /**
     * Frees the value from the escape
     * \public
     */
    void odbxuv_op_escape_free_escape(odbxuv_op_escape_t *op);

    /**
     * Inits the connection with a result object and fetch flags
     * \public
     */
    int odbxuv_init_query(odbxuv_op_query_t *operation, odbxuv_result_t *result, odbxuv_query_fetch_e flags);

    /**
     * Runs a connection on the database
     * \note The query string is internally copied
     * \public
     */
    int odbxuv_query(odbxuv_connection_t *connection, odbxuv_op_query_t *operation, const char *query, odbxuv_op_query_cb callback);
    int odbxuv_query_process(odbxuv_result_t *result, odbxuv_fetch_cb onQueryRow);
    int odbxuv_result_free(odbxuv_result_t *result);

    /**
     * Frees the query
     * \public
     */
    void odbxuv_op_query_free_query(odbxuv_op_query_t *op);

    /**
     * \}
     */

#ifdef __cplusplus
}
#endif