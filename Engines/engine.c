#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include "uthash.h"


#define DATA_FILE "data.txt"
#define DELETE_FILE "delete.txt"
#define KEY_SEP ' '
#define COMPACT_INTERVAL 5    
#define DELETE_INTERVAL 5    
#define MAX_LINE_LEN 1024

typedef struct Entry {
    char *key;            
    long offset;           
    UT_hash_handle hh;   
} Entry;

typedef struct {
    Entry *map;            
    FILE *data_fp;
    FILE *del_fp;
    pthread_mutex_t data_mu;
    pthread_mutex_t del_mu;
} Engine;

static void* compaction_thread(void *arg);
static void* deletion_thread(void *arg);
static void engine_restore(Engine *e);

// Initialize the engine, restore data from file, and start background threads
Engine* engine_init() {
    Engine *e = malloc(sizeof(Engine));
    if (!e) return NULL;
    e->map = NULL;
    pthread_mutex_init(&e->data_mu, NULL);
    pthread_mutex_init(&e->del_mu, NULL);

    e->data_fp = fopen(DATA_FILE, "a+");
    if (!e->data_fp) {
        perror("fopen data");
        free(e);
        return NULL;
    }
    e->del_fp = fopen(DELETE_FILE, "a+");
    if (!e->del_fp) {
        perror("fopen delete");
        fclose(e->data_fp);
        free(e);
        return NULL;
    }
    engine_restore(e);
    pthread_t comp_t, del_t;
    pthread_create(&comp_t, NULL, compaction_thread, e);
    pthread_detach(comp_t);
    pthread_create(&del_t, NULL, deletion_thread, e);
    pthread_detach(del_t);
    return e;
}

// Restore map from data file
static void engine_restore(Engine *e) {
    char line[MAX_LINE_LEN];
    long offset = 0;
    rewind(e->data_fp);
    while (fgets(line, sizeof(line), e->data_fp)) {
        char *sep = strchr(line, KEY_SEP);
        if (!sep) continue;
        *sep = '\0';
        char *key = strdup(line);
        Entry *ent;
        HASH_FIND_STR(e->map, key, ent);
        if (ent) {
            // update offset
            ent->offset = offset;
            free(key);
        } else {
            ent = malloc(sizeof(Entry));
            ent->key = key;
            ent->offset = offset;
            HASH_ADD_KEYPTR(hh, e->map, ent->key, strlen(ent->key), ent);
        }
        offset = ftell(e->data_fp);
    }
}

// Set a key-value pair
int engine_set(Engine *e, const char *key, const char *value) {
    if (strchr(key, KEY_SEP)) return -1;
    pthread_mutex_lock(&e->data_mu);
    fseek(e->data_fp, 0, SEEK_END);
    long offset = ftell(e->data_fp);
    fprintf(e->data_fp, "%s %s\n", key, value);
    fflush(e->data_fp);
    // update map
    Entry *ent;
    HASH_FIND_STR(e->map, key, ent);
    if (ent) {
        ent->offset = offset;
    } else {
        ent = malloc(sizeof(Entry));
        ent->key = strdup(key);
        ent->offset = offset;
        HASH_ADD_KEYPTR(hh, e->map, ent->key, strlen(ent->key), ent);
    }
    pthread_mutex_unlock(&e->data_mu);
    return 0;
}

// Get a value by key (caller must free returned string)
char* engine_get(Engine *e, const char *key) {
    pthread_mutex_lock(&e->data_mu);
    Entry *ent;
    HASH_FIND_STR(e->map, key, ent);
    if (!ent) {
        pthread_mutex_unlock(&e->data_mu);
        return NULL;
    }
    fseek(e->data_fp, ent->offset, SEEK_SET);
    char line[MAX_LINE_LEN];
    if (!fgets(line, sizeof(line), e->data_fp)) {
        pthread_mutex_unlock(&e->data_mu);
        return NULL;
    }
    pthread_mutex_unlock(&e->data_mu);
    // parse value after the space
    char *sep = strchr(line, KEY_SEP);
    if (!sep) return NULL;
    char *val = strdup(sep + 1);
    // remove trailing newline
    char *nl = strchr(val, '\n'); if (nl) *nl = '\0';
    return val;
}

// Delete a key (marks in delete file)
int engine_delete(Engine *e, const char *key) {
    pthread_mutex_lock(&e->del_mu);
    fprintf(e->del_fp, "%s\n", key);
    fflush(e->del_fp);
    pthread_mutex_unlock(&e->del_mu);
    // remove from map
    pthread_mutex_lock(&e->data_mu);
    Entry *ent;
    HASH_FIND_STR(e->map, key, ent);
    if (ent) {
        HASH_DEL(e->map, ent);
        free(ent->key);
        free(ent);
    }
    pthread_mutex_unlock(&e->data_mu);
    return 0;
}

// Periodically compact data file
static void* compaction_thread(void *arg) {
    Engine *e = arg;
    while (1) {
        sleep(COMPACT_INTERVAL);
        pthread_mutex_lock(&e->data_mu);
        // read current map entries
        Entry *ent, *tmp;
        // create temp file
        FILE *tmp_fp = tmpfile();
        HASH_ITER(hh, e->map, ent, tmp) {
            // get value
            fseek(e->data_fp, ent->offset, SEEK_SET);
            char line[MAX_LINE_LEN];
            if (!fgets(line, sizeof(line), e->data_fp)) continue;
            // write to tmp file
            long new_offset = ftell(tmp_fp);
            fprintf(tmp_fp, "%s", line);
            ent->offset = new_offset;
        }
        // replace data file content
        freopen(DATA_FILE, "w+", e->data_fp);
        rewind(tmp_fp);
        char buff[MAX_LINE_LEN];
        while (fgets(buff, sizeof(buff), tmp_fp)) fputs(buff, e->data_fp);
        fflush(e->data_fp);
        fclose(tmp_fp);
        pthread_mutex_unlock(&e->data_mu);
        printf("Compaction done.\n");
    }
    return NULL;
}

// Periodically delete keys from data file
static void* deletion_thread(void *arg) {
    Engine *e = arg;
    while (1) {
        sleep(DELETE_INTERVAL);
        pthread_mutex_lock(&e->del_mu);
        // read delete keys
        rewind(e->del_fp);
        char *del_keys[1024];
        size_t del_count = 0;
        char line[MAX_LINE_LEN];
        while (fgets(line, sizeof(line), e->del_fp) && del_count < 1024) {
            char *nl = strchr(line, '\n'); if (nl) *nl = '\0';
            del_keys[del_count++] = strdup(line);
        }
        // clear delete file
        freopen(DELETE_FILE, "w+", e->del_fp);
        pthread_mutex_unlock(&e->del_mu);

        if (del_count == 0) continue;
        // rebuild data file without deleted keys
        pthread_mutex_lock(&e->data_mu);
        FILE *tmp_fp = tmpfile();
        rewind(e->data_fp);
        // long offset = 0;
        while (fgets(line, sizeof(line), e->data_fp)) {
            char *sep = strchr(line, KEY_SEP);
            if (!sep) continue;
            *sep = '\0';
            int to_delete = 0;
            for (size_t i = 0; i < del_count; i++) {
                if (strcmp(line, del_keys[i]) == 0) { to_delete = 1; break; }
            }
            *sep = KEY_SEP;
            if (!to_delete) {
                long new_offset = ftell(tmp_fp);
                fputs(line, tmp_fp);
                // update map offset for this key
                Entry *ent;
                // separate key
                char *key = strndup(line, sep - line);
                HASH_FIND_STR(e->map, key, ent);
                if (ent) ent->offset = new_offset;
                free(key);
            }
        }
        // replace data file content
        freopen(DATA_FILE, "w+", e->data_fp);
        rewind(tmp_fp);
        while (fgets(line, sizeof(line), tmp_fp)) fputs(line, e->data_fp);
        fflush(e->data_fp);
        fclose(tmp_fp);
        pthread_mutex_unlock(&e->data_mu);
        for (size_t i = 0; i < del_count; i++) free(del_keys[i]);
        printf("Deletion from file done.\n");
    }
    return NULL;
}

void engine_close(Engine *e) {
    fclose(e->data_fp);
    fclose(e->del_fp);
    Entry *ent, *tmp;
    HASH_ITER(hh, e->map, ent, tmp) {
        HASH_DEL(e->map, ent);
        free(ent->key);
        free(ent);
    }
    pthread_mutex_destroy(&e->data_mu);
    pthread_mutex_destroy(&e->del_mu);
    free(e);
}
