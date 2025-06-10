#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <microhttpd.h>
#include <json-c/json.h>
#include "handler.h"
#include "../Engines/engine.h"

struct RequestData {
    char *data;
    size_t size;
};

// Create JSON response with status and message
static struct MHD_Response* create_json_response(const char *status, const char *message) {
    struct json_object *response_obj = json_object_new_object();
    json_object_object_add(response_obj, "status", json_object_new_string(status));
    json_object_object_add(response_obj, "message", json_object_new_string(message));
    
    const char *json_str = json_object_to_json_string(response_obj);
    struct MHD_Response *response = MHD_create_response_from_buffer(
        strlen(json_str),
        (void*)json_str,
        MHD_RESPMEM_MUST_COPY
    );
    
    MHD_add_response_header(response, "Content-Type", "application/json");
    json_object_put(response_obj);
    
    return response;
}

// Handle POST requests to /set endpoint
static int handle_set(struct MHD_Connection *connection, const char *data) {
    struct json_object *json = json_tokener_parse(data);
    if (!json) {
        struct MHD_Response *response = create_json_response("error", "Invalid JSON");
        int ret = MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, response);
        MHD_destroy_response(response);
        return ret;
    }
    
    struct json_object *key_obj, *value_obj;
    if (!json_object_object_get_ex(json, "key", &key_obj) || 
        !json_object_object_get_ex(json, "value", &value_obj)) {
        struct MHD_Response *response = create_json_response(
            "error", "Missing key or value field");
        int ret = MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, response);
        MHD_destroy_response(response);
        json_object_put(json);
        return ret;
    }
    
    const char *key = json_object_get_string(key_obj);
    const char *value = json_object_get_string(value_obj);
    
    Engine *engine = get_engine();
    int result = engine_set(engine, key, value);
    
    struct MHD_Response *response;
    int status_code;
    
    if (result == 0) {
        response = create_json_response("success", "Key set successfully");
        status_code = MHD_HTTP_OK;
    } else {
        response = create_json_response("error", "Failed to set key");
        status_code = MHD_HTTP_INTERNAL_SERVER_ERROR;
    }
    
    int ret = MHD_queue_response(connection, status_code, response);
    MHD_destroy_response(response);
    json_object_put(json);
    
    return ret;
}

// Handle GET requests to /get endpoint
static int handle_get(struct MHD_Connection *connection) {
    const char *key = MHD_lookup_connection_value(
        connection, MHD_GET_ARGUMENT_KIND, "key");
    
    if (!key) {
        struct MHD_Response *response = create_json_response(
            "error", "Missing key parameter");
        int ret = MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, response);
        MHD_destroy_response(response);
        return ret;
    }
    
    Engine *engine = get_engine();
    char *value = engine_get(engine, key);
    
    struct MHD_Response *response;
    int status_code;
    
    if (value) {
        struct json_object *resp_obj = json_object_new_object();
        json_object_object_add(resp_obj, "status", json_object_new_string("success"));
        json_object_object_add(resp_obj, "value", json_object_new_string(value));
        
        const char *json_str = json_object_to_json_string(resp_obj);
        response = MHD_create_response_from_buffer(
            strlen(json_str), (void*)json_str, MHD_RESPMEM_MUST_COPY);
        MHD_add_response_header(response, "Content-Type", "application/json");
        status_code = MHD_HTTP_OK;
        
        json_object_put(resp_obj);
        free(value);
    } else {
        response = create_json_response("error", "Key not found");
        status_code = MHD_HTTP_NOT_FOUND;
    }
    
    int ret = MHD_queue_response(connection, status_code, response);
    MHD_destroy_response(response);
    
    return ret;
}

// Handle POST requests to /delete endpoint
static int handle_delete(struct MHD_Connection *connection, const char *data) {
    struct json_object *json = json_tokener_parse(data);
    if (!json) {
        struct MHD_Response *response = create_json_response("error", "Invalid JSON");
        int ret = MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, response);
        MHD_destroy_response(response);
        return ret;
    }
    
    struct json_object *key_obj;
    if (!json_object_object_get_ex(json, "key", &key_obj)) {
        struct MHD_Response *response = create_json_response(
            "error", "Missing key field");
        int ret = MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, response);
        MHD_destroy_response(response);
        json_object_put(json);
        return ret;
    }
    
    const char *key = json_object_get_string(key_obj);
    Engine *engine = get_engine();
    int result = engine_delete(engine, key);
    
    struct MHD_Response *response;
    int status_code;
    
    if (result == 0) {
        response = create_json_response("success", "Key deleted successfully");
        status_code = MHD_HTTP_OK;
    } else {
        response = create_json_response("error", "Failed to delete key");
        status_code = MHD_HTTP_INTERNAL_SERVER_ERROR;
    }
    
    int ret = MHD_queue_response(connection, status_code, response);
    MHD_destroy_response(response);
    json_object_put(json);
    
    return ret;
}

// Main request handler function
enum MHD_Result handle_request(void *cls, struct MHD_Connection *connection,
                  const char *url, const char *method,
                  const char *version, const char *upload_data,
                  size_t *upload_data_size, void **con_cls) {
    
    struct RequestData *request_data;
    
    if (*con_cls == NULL) {
        request_data = calloc(1, sizeof(struct RequestData));
        if (!request_data)
            return MHD_NO;
        *con_cls = (void*)request_data;
        return MHD_YES;
    }
    
    request_data = *con_cls;
    
    if (strcmp(method, "POST") == 0) {
        if (*upload_data_size != 0) {
            if (!request_data->data) {
                request_data->data = malloc(*upload_data_size);
                request_data->size = *upload_data_size;
                memcpy(request_data->data, upload_data, *upload_data_size);
            } else {
                char *new_data = realloc(request_data->data, 
                                         request_data->size + *upload_data_size);
                memcpy(new_data + request_data->size, upload_data, *upload_data_size);
                request_data->data = new_data;
                request_data->size += *upload_data_size;
            }
            *upload_data_size = 0;
            return MHD_YES;
        } else if (!request_data->data) {
            // Empty POST request
            struct MHD_Response *response = create_json_response(
                "error", "Empty request body");
            int ret = MHD_queue_response(connection, MHD_HTTP_BAD_REQUEST, response);
            MHD_destroy_response(response);
            free(request_data);
            *con_cls = NULL;
            return ret;
        }
        
        // Ensure null termination
        char *body = malloc(request_data->size + 1);
        memcpy(body, request_data->data, request_data->size);
        body[request_data->size] = '\0';
        
        int ret;
        if (strcmp(url, "/set") == 0) {
            ret = handle_set(connection, body);
        } else if (strcmp(url, "/delete") == 0) {
            ret = handle_delete(connection, body);
        } else {
            struct MHD_Response *response = create_json_response(
                "error", "Endpoint not found");
            ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
            MHD_destroy_response(response);
        }
        
        free(body);
        free(request_data->data);
        free(request_data);
        *con_cls = NULL;
        return ret;
    }
    else if (strcmp(method, "GET") == 0) {
        if (strcmp(url, "/get") == 0) {
            return handle_get(connection);
        } else {
            struct MHD_Response *response = create_json_response(
                "error", "Endpoint not found");
            int ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
            MHD_destroy_response(response);
            free(request_data);
            *con_cls = NULL;
            return ret;
        }
    }
    
    // Method not allowed
    struct MHD_Response *response = create_json_response(
        "error", "Method not allowed");
    int ret = MHD_queue_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED, response);
    MHD_destroy_response(response);
    free(request_data);
    *con_cls = NULL;
    return ret;
}