#include <microhttpd.h>
#include <stdio.h>
#include <string.h>
//compilation gcc -o server server.c -lmicrohttpd


#define PORT 8080

static int answer_to_connection(void *cls, struct MHD_Connection *connection, 
                                const char *url, const char *method, 
                                const char *version, const char *upload_data, 
                                size_t *upload_data_size, void **con_cls) {
    const char *response_text = "{\"message\": \"Hello from C REST server\"}";
    struct MHD_Response *response;
    int ret;

    response = MHD_create_response_from_buffer(strlen(response_text), 
                                               (void *) response_text, 
                                               MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(response, "Content-Type", "application/json");

    ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);

    return ret;
}

int main() {
    struct MHD_Daemon *daemon;

daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, PORT, NULL, NULL, 
                          (MHD_AccessHandlerCallback)&answer_to_connection, 
                          NULL, MHD_OPTION_END);
    
    if (!daemon) {
        fprintf(stderr, "Failed to start server\n");
        return 1;
    }

    printf("Server running on port %d...\n", PORT);
    getchar(); // Attendre un input pour arrêter le serveur

    MHD_stop_daemon(daemon);
    return 0;
}

