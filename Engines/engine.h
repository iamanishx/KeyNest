#ifndef ENGINE_H
#define ENGINE_H

typedef struct Entry Entry;
typedef struct Engine Engine;

// Main API functions
Engine* engine_init();
int engine_set(Engine *e, const char *key, const char *value);
char* engine_get(Engine *e, const char *key);
int engine_delete(Engine *e, const char *key);
void engine_close(Engine *e);

#endif 