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
     * \defgroup odbxuv Odbxuv global functions
     * \{
     * \defgroup enums Odbxuv enums
     * \{
     */

    /**
     * All the states a connection can be in.
     */
    typedef enum odbxuv_connection_status_enum
    {
        ODBXUV_CON_STATUS_IDLE = 0,
        ODBXUV_CON_STATUS_CONNECTING,
        ODBXUV_CON_STATUS_CONNECTED,
        ODBXUV_CON_STATUS_DISCONNECTING,
        ODBXUV_CON_STATUS_DISCONNECTED,
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
     * All the different types of operations
     * Use \p ODBXUV_OP_CUSTOM to add custom operation types
     */
    typedef enum odbxuv_handle_type_enum
    {
        ODBXUV_HANDLE_TYPE_NONE = 0,
        ODBXUV_HANDLE_TYPE_CONNECTION,
        ODBXUV_HANDLE_TYPE_OP_CONNECT,
        ODBXUV_HANDLE_TYPE_OP_DISCONNECT,
        ODBXUV_HANDLE_TYPE_OP_CAPABILITIES,
        ODBXUV_HANDLE_TYPE_OP_QUERY,
        ODBXUV_HANDLE_TYPE_OP_FETCH,
        ODBXUV_HANDLE_TYPE_OP_ESCAPE,
        ODBXUV_HANDLE_TYPE_OP_CUSTOM,
    } odbxuv_handle_type_e;

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

    typedef enum odbxuv_fetch_status_enum
    {
        ODBXUV_FETCH_STATUS_NONE = 0,
        ODBXUV_FETCH_STATUS_RUNNING,
        ODBXUV_FETCH_STATUS_ERROR_BEFORE,
        ODBXUV_FETCH_STATUS_ERROR_RESULT,
        ODBXUV_FETCH_STATUS_ERROR_FETCH,
        ODBXUV_FETCH_STATUS_ERROR_FINISH,
        ODBXUV_FETCH_STATUS_FINISHED,
        ODBXUV_FETCH_STATUS_CANCELLED
    } odbxuv_fetch_status_e;

    typedef enum odbxuv_fetch_cb_status_enum
    {
        ODBXUV_FETCH_CB_STATUS_NONE = 0,
        ODBXUV_FETCH_CB_STATUS_FIRST,
        ODBXUV_FETCH_CB_STATUS_CALLED
    } odbxuv_fetch_cb_status_e;

    /**
     * \}
     * \}
     */

    /**
     * \defgroup odbxuv Odbxuv global functions
     * \{
     * \defgroup datatypes Odbxuv data types
     * \{
     */

    typedef struct odbxuv_error_s
    {
        /**
         * The current status/error code
         * \note Read only
         */
        int error;

         /**
          * The type of the error
          * \note Read only
          */
        int errorType;

        /**
         * The current error string or \p NULL
         * \note Read only
         * \sa odbxuv_free_error
         */
        char *errorString;
    } odbxuv_error_t;

    #define ODBXUV_HANDLE_BASE_FIELDS           \
        /**                                     \
         * The type of the handle               \
         * \note Read only                      \
         */                                     \
        odbxuv_handle_type_e type;              \
        /**                                     \
         * Userdata                             \
         * \public                              \
         */                                     \
         void *data;                            \
         /**                                    \
          * Error info                          \
          * \public                             \
          */                                    \
        odbxuv_error_t *error;


    typedef struct odbxuv_handle_s
    {
        ODBXUV_HANDLE_BASE_FIELDS
    } odbxuv_handle_t;

    /**
     * A connection object.
     * This represents the connection to the database and contains all the worker information.
     */
    typedef struct odbxuv_connection_s
    {
        ODBXUV_HANDLE_BASE_FIELDS

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
    } odbxuv_connection_t;



    typedef struct odbxuv_op_connect_s odbxuv_op_connect_t;
    typedef struct odbxuv_op_disconnect_s odbxuv_op_disconnect_t;
    typedef struct odbxuv_op_capabilities_s odbxuv_op_capabilities_t;
    typedef struct odbxuv_op_escape_s odbxuv_op_escape_t;
    typedef struct odbxuv_op_query_s odbxuv_op_query_t;
    typedef struct odbxuv_row_s odbxuv_row_t;
    typedef struct odbxuv_column_info_s odbxuv_column_info_t;
    /**
     * \}
     * \}
     */

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
     * \warning Don't forget to call ::odbxuv_op_query_free_query in the callback
     * \sa odbxuv_op_cb
     */
    typedef void (*odbxuv_op_query_cb) (odbxuv_op_query_t *op, int status);

    /**
     * Callback invoked once per fetched row
     * Is called with NULL as row after the last row
     * Status may be 1 to indicate this is the first time we call the fetch callback
     * \warning Make sure you cleanup the result using ::odbxuv_result_free and free when row is NULL
     */
    typedef void (*odbxuv_fetch_cb) (odbxuv_op_query_t *result, odbxuv_row_t *row, int status);

    /**
     * Closing callback called when the hande has been closed and it is safe to free
     */
    typedef void (*odbxuv_close_cb) (odbxuv_handle_t *);


    /**
     * The default fields of an operation
     * \internal
     * \private
     */
    #define ODBXUV_OP_BASE_FIELDS               \
        ODBXUV_HANDLE_BASE_FIELDS                 \
        \
        /**                                     \
         * The current status of the operation  \
         * \note Read only                      \
         */                                     \
        odbxuv_operation_status_e status;       \
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


    /**
     * A basic operation
     */
    struct odbxuv_op_s
    {
        ODBXUV_OP_BASE_FIELDS
    };

    /**
     * A disconnect operation
     */
    struct odbxuv_op_disconnect_s
    {
        ODBXUV_OP_BASE_FIELDS
    };

    /**
     * A connect operation
     * \warning Don't forget to run ::odbxuv_op_free
     */
    struct odbxuv_op_connect_s
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
    };

    /**
     * A capabilities request operation.
     * The \p error field contains the status of the capability.
     */
    struct odbxuv_op_capabilities_s
    {
        ODBXUV_OP_BASE_FIELDS

        /**
         * The capabilities to query for.
         */
        int capabilities;

        /**
         * The result
         */
        int result;
    };

    /**
     * A string escape operation
     * \warning Don't forget to call ::odbxuv_op_escape_free_escape afterwards
     */
    struct odbxuv_op_escape_s
    {
        ODBXUV_OP_BASE_FIELDS

        /**
         * The string to escape
         */
        char *string;
    };

    /**
     * A query operation
     * \warning Don't forget to call ::odbxuv_op_query_free_query afterwards
     */
    struct odbxuv_op_query_s
    {
        ODBXUV_OP_BASE_FIELDS

        /**
         * The query string.
         */
        char *query;

        /**
         * Query fetch flags
         */
        odbxuv_query_fetch_e flags;

        /**
         * The amount of rows to fetch at once
         */
        int chunkSize;

        /**
         * The odbx result handle
         */
        odbx_result_t *resultHandle;

        /**
         * Amount of columns fetched
         */
        unsigned int columnCount;

        /**
         * Amount of rows affected
         */
        unsigned int affectedCount;

        /**
         * An array of column info
         * May be NULL when hasn't been set
         */
        odbxuv_column_info_t *columns;

        /**
         * The list of rows that have been fetched
         */
        odbxuv_row_t *row;

        /**
         * The callback to the fetch function
         */
        odbxuv_fetch_cb cb;

        /**
         * Async handle to call the fetch callback on the event loop
         * \private
         */
        uv_async_t async;

        unsigned char asyncStatus : 2;

        /**
         * Werther the query fetching has been finished
         * \private
         */
        odbxuv_fetch_status_e fetchStatus : 4;

        /**
         * Werther the fetch callback has been called with fetch_first status yet
         * \private
         */
        odbxuv_fetch_cb_status_e fetchCallbackStatus : 2;
    };

    /**
     * Info about a column
     * Is part of a query result, is automatically freed and allocated
     */
    struct odbxuv_column_info_s
    {
        /**
         * The name of the column
         * NULL if not set
         * \note Read only
         */
        char *name;

        /**
         * The type of the column
         * 0 if not set
         * \note Read only
         */
        int type;
    };

    /**
     * A fetched row
     */
    struct odbxuv_row_s
    {
        /**
         * The status of the current row
         * Mainly whether the row has been processed yet and can therefore be reused.
         * \private
         */
        odbxuv_row_status_e status;

        /**
         * Array of field values
         * NULL if not available or the value is NULL
         * \note Read only
         */
        char **value;

        /**
         * Pointer to the next result
         * \private
         */
        odbxuv_row_t *next;
    };

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
     * Closes an odbx handle
     * \public
     */
    void odbxuv_close(odbxuv_handle_t *handle, odbxuv_close_cb callback);

    /**
     * Frees the error string
     * \public
     */
    void odbxuv_free_error(odbxuv_handle_t *handle);

    /**
     * Frees the internal allocated information of a handle
     * Should be called just before free()
     * \public
     */
    void odbxuv_free_handle(odbxuv_handle_t *result);

    /**
     * Connects to the database using the credentials specified in the \p operation.
     * \note all the credentials are internally copied
     * \public
     */
    int odbxuv_connect(odbxuv_connection_t *connection, odbxuv_op_connect_t *operation, odbxuv_op_connect_cb callback);

    /**
     * Disconnects from the database.
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
     * Runs a query on the database
     * \note The query string is internally copied
     * \public
     */
    int odbxuv_query(odbxuv_connection_t *connection, odbxuv_op_query_t *operation, const char *query, odbxuv_query_fetch_e flags, odbxuv_op_query_cb callback);

    /**
     * Starts processing the rows of a query
     * Should be called inside the ::odbxuv_op_query_cb callback
     * \public
     */
    int odbxuv_query_process(odbxuv_op_query_t *result, odbxuv_fetch_cb onQueryRow);


    /**
     * \}
     */

#ifdef __cplusplus
}
#endif