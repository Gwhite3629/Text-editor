#ifndef _ENV_H_
#define _ENV_H_

#include "utils.h"

#include <pthread.h>

#define TABSIZE 8

struct environment {
    char *file_name; // Name of output file
    bool valid_fname; // Indicates if user specified file at startup
    bool new; // Indicates if there is a new character to write
    struct line *file_data; // File contents
    unsigned long alloc_size; // Current size of file
    unsigned long index; // Current line number
    unsigned long max; // Max number of lines typed
    pthread_spinlock_t lock; // Lock for threading
    unsigned long top;
    unsigned long bottom;
    struct line *tmp1;
    struct line *tmp2;
    atomic_bool quit; // Indicates if the quit signal has been sent
};

struct line {
    char ldata[4096]; // Characters in current line
    unsigned long index; // Current index in line
    unsigned long max; // Max index in line
    char tmp1[4096];
    char tmp2[4096];
};

void disableRaw(void);

void enableRaw(void);

err init_env(struct environment **env, bool v, char *fname);

void destroy_env(struct environment **env);

void init_line(struct environment **env);

void editorRefreshScreen(void);

err draw(struct environment **env);

void *input(void *env);

err read_file(struct environment **env);

err write_file(struct environment **env);

#endif // _ENV_H_