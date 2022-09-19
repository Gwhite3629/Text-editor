#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdint.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>

#include "utils.h"
#include "env.h"

int main(int argc, char *argv[])
{
    err ret = SUCCESS;
    struct environment *env;
    pthread_t thr;

    switch (argc) {
        case 1:
            CHECK(ret = init_env(&env, 0, NULL));
            break;
        case 2:
            //init_env(&env, 1, argv[1]);
            //break;
        default:
            CHECK(ret = init_env(&env, 1, argv[1]));
    }

    HANDLE_ERR(pthread_create(&thr, NULL, &input, &env), "pthread_create");

    enableRaw();

    while(!env->quit) {
        if (env->new == 1) {
            draw(&env);
        }
    }

exit:

    HANDLE_ERR(pthread_join(thr, NULL), "pthread_join");

    destroy_env(&env);

    return ret;
}