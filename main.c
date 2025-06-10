#include <stdio.h>
#include <stdlib.h>
#include "server/server.h"

int main() {
    printf("Starting C-DB server...\n");
    
    if (server_start() != 0) {
        fprintf(stderr, "Failed to start server\n");
        return 1;
    }
    
    return 0;
}