#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <microhttpd.h>
#include "server.h"
#include "handler.h"
#include "../Engines/engine.h"

#define PORT 8080

static struct MHD_Daemon *http_daemon;
static Engine *engine;

int server_start() {
    engine = engine_init();
    if (!engine) {
        fprintf(stderr, "Error initializing engine\n");
        return 1;
    }
    
    http_daemon = MHD_start_daemon(
        MHD_USE_SELECT_INTERNALLY, PORT, NULL, NULL,
        (MHD_AccessHandlerCallback)&handle_request, NULL,
        MHD_OPTION_END
    );
    
    if (NULL == http_daemon) {
        fprintf(stderr, "Failed to create HTTP server\n");
        engine_close(engine);
        return 1;
    }
    
    printf("Server running at http://localhost:%d\n", PORT);
    
    printf("Press Enter to stop the server...\n");
    getchar();
    server_stop();
    return 0;
}

void server_stop() {
    if (http_daemon) {
        MHD_stop_daemon(http_daemon);
        http_daemon = NULL;
    }
    if (engine) {
        engine_close(engine);
        engine = NULL;
    }
    printf("Server stopped\n");
}

Engine* get_engine() {
    return engine;
}