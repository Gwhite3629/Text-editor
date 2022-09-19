#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdint.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "utils.h"
#include "env.h"

struct termios def;

void disableRaw(void)
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &def);
}

void enableRaw(void)
{
    tcgetattr(STDIN_FILENO, &def);
    atexit(disableRaw);

    struct termios raw = def;
    raw.c_iflag &= ~(IXON);
    raw.c_lflag &= ~(ECHO | ICANON);

    raw.c_cc[VMIN] = 2; // Set to 0 for polling on faster systems
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

err init_env(struct environment **env, bool v, char *fname)
{
    err ret = SUCCESS;
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

    MEM((*env), 1, struct environment);

    if (v) {
        MEM((*env)->file_name, strlen(fname), char);
        memcpy((*env)->file_name, fname, strlen(fname)*sizeof(char));
        (*env)->valid_fname = 1;
    } else {
        (*env)->file_name = NULL;
        (*env)->valid_fname = 0;
    }

    MEM((*env)->file_data, 1, struct line);
    MEM((*env)->tmp1, 1, struct line);
    MEM((*env)->tmp2, 1, struct line);
    (*env)->alloc_size = 1;
    (*env)->max = 0;
    (*env)->index = 0;
    (*env)->new = 1;
    (*env)->quit = 0;
    (*env)->top = 0;
    (*env)->bottom = (unsigned long)w.ws_row - 1;
    init_line(env);

    if ((*env)->valid_fname)
        read_file(env);

    pthread_spin_init(&(*env)->lock, 0);

exit:

    return ret;
}

void init_line(struct environment **env)
{
    (*env)->file_data[(*env)->index].index = 0;
    (*env)->file_data[(*env)->index].max = 0;
    memset((*env)->file_data[(*env)->index].ldata, '\0', 4096);
    (*env)->file_data[(*env)->index].ldata[0] = ' ';
    memset((*env)->file_data[(*env)->index].tmp1, '\0', 4096);
    memset((*env)->file_data[(*env)->index].tmp2, '\0', 4096);
}

void editorRefreshScreen(void) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
}

void destroy_env(struct environment **env)
{
    SFREE((*env)->file_name);
    SFREE((*env)->file_data);
    SFREE((*env)->tmp1);
    SFREE((*env)->tmp2);
    SFREE((*env));
}

err draw(struct environment **env)
{
    pthread_spin_lock(&(*env)->lock);

    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

    unsigned long r = (unsigned long)w.ws_row - 1;

    if (((*env)->bottom - (*env)->top) < r) {
        (*env)->bottom += (r - ((*env)->bottom - (*env)->top));
    }

    if ((*env)->index < (*env)->top) {
        (*env)->top--;
        (*env)->bottom--;
    }

    if ((*env)->index >= (*env)->bottom) {
        (*env)->top++;
        (*env)->bottom++;
    }

    editorRefreshScreen();

    for (unsigned long i = (*env)->top; i < (*env)->bottom; i++) {
        if (i <= (*env)->max) {
            printf("%3ld: ", i);
            //write(STDOUT_FILENO, ": ", 2);
            if (i == ((*env)->index)) {
                for (unsigned long j = 0; j <= (*env)->file_data[i].max; j++) {
                    if (j == ((*env)->file_data[i].index)){
                        if ((*env)->file_data[i].index == (*env)->file_data[i].max) {
                            printf("\033[30m\033[47m ");
                            //write(STDOUT_FILENO, "\033[30m\033[47m ", 11);
                            printf("\033[0m");
                            //write(STDOUT_FILENO, "\033[0m", 4);
                        } else {
                            printf("\033[30m\033[47m%c", (*env)->file_data[i].ldata[j]);
                            //write(STDOUT_FILENO, "\033[0m\033[47m", 9);
                            //write(STDOUT_FILENO, &(*env)->file_data[i].ldata[j], 1);
                            printf("\033[0m");
                            //write(STDOUT_FILENO, "\033[0m", 4);
                        }
                    } else {
                        printf("%c", (*env)->file_data[i].ldata[j]);
                        //write(STDOUT_FILENO, &(*env)->file_data[i].ldata[j], 1);
                    }
                }
                printf("\n");
                //write(STDOUT_FILENO, "\n", 1);
            } else {
                printf("%s\n", (*env)->file_data[i].ldata);
                //write(STDOUT_FILENO, (*env)->file_data[i].ldata, (*env)->file_data[i].max);
                //write(STDOUT_FILENO, "\n", 1);
            }
        } else {
            printf("~\n");
            //write(STDOUT_FILENO, "~\n", 2);
        }
    }

    (*env)->new = 0;

    pthread_spin_unlock(&(*env)->lock);

    return SUCCESS;
}

void *input(void *arg)
{
    struct environment **env = (struct environment **)arg;

    err ret = SUCCESS;
    char c = '\0';
    char str[9] = "\0\0\0\0\0\0\0\0\0";
    while(!(*env)->quit) {
        read(STDIN_FILENO,&c,1);
        // Not updated
        if ((c >= 0x20) & (c < 0x7F)) {
            pthread_spin_lock(&(*env)->lock);
            /*if ((*env)->file_data[(*env)->index].max == 79) {
                (*env)->alloc_size++;
                MEM_((*env)->file_data, (*env)->alloc_size, struct line);
                MEM_((*env)->tmp1, (*env)->alloc_size, struct line);
                MEM_((*env)->tmp2, (*env)->alloc_size, struct line);
                if ((*env)->index == ((*env)->max)) {
                    (*env)->max++;
                    (*env)->index++;
                    init_line(env);
                } else {
                    (*env)->max++;
                    for (unsigned long i = (*env)->max; i > (*env)->index; i--) {
                        (*env)->file_data[i] = (*env)->file_data[i-1];
                    }
                    init_line(env);
                    (*env)->index++;
                }
                (*env)->new = 1;
            }*/
            if ((*env)->file_data[(*env)->index].index == (*env)->file_data[(*env)->index].max) {
                (*env)->file_data[(*env)->index].ldata[(*env)->file_data[(*env)->index].index] = c;
            } else {
                memcpy((*env)->file_data[(*env)->index].tmp1, (*env)->file_data[(*env)->index].ldata, (*env)->file_data[(*env)->index].index);
                memcpy((*env)->file_data[(*env)->index].tmp2, (*env)->file_data[(*env)->index].ldata + (*env)->file_data[(*env)->index].index, (*env)->file_data[(*env)->index].max - (*env)->file_data[(*env)->index].index);
                sprintf((*env)->file_data[(*env)->index].ldata, "%s%c%s", (*env)->file_data[(*env)->index].tmp1, c, (*env)->file_data[(*env)->index].tmp2);
                memset((*env)->file_data[(*env)->index].tmp1, '\0', 4096);
                memset((*env)->file_data[(*env)->index].tmp2, '\0', 4096);
            }
            (*env)->file_data[(*env)->index].max++;
            (*env)->file_data[(*env)->index].index++;
            (*env)->new = 1;
            pthread_spin_unlock(&(*env)->lock);
        // Updated
        } else if (c == 0x7F) {
            pthread_spin_lock(&(*env)->lock);
            if ((*env)->file_data[(*env)->index].index == 0) {
                if ((*env)->file_data[(*env)->index].max == 0) {
                    if ((*env)->index == 0) {
                    } else {
                        (*env)->max--;
                        for (unsigned long i = (*env)->index; i <= (*env)->max; i++) {
                            (*env)->file_data[i] = (*env)->file_data[i + 1];
                        }
                        (*env)->alloc_size--;
                        MEM_((*env)->file_data, (*env)->alloc_size, struct line);
                        MEM_((*env)->tmp1, (*env)->alloc_size, struct line);
                        MEM_((*env)->tmp2, (*env)->alloc_size, struct line);
                        (*env)->index--;
                        (*env)->new = 1;
                    }
                } else if ((*env)->index != 0) {
                    unsigned long nb = (*env)->file_data[(*env)->index].max;
                    memcpy((*env)->file_data[(*env)->index - 1].tmp1, (*env)->file_data[(*env)->index].ldata, nb);
                    (*env)->max--;
                    for (unsigned long i = (*env)->index; i <= (*env)->max; i++) {
                        (*env)->file_data[i] = (*env)->file_data[i + 1];
                    }
                    (*env)->alloc_size--;
                    MEM_((*env)->file_data, (*env)->alloc_size, struct line);
                    MEM_((*env)->tmp1, (*env)->alloc_size, struct line);
                    MEM_((*env)->tmp2, (*env)->alloc_size, struct line);
                    (*env)->index--;
                    memcpy((*env)->file_data[(*env)->index].ldata + (*env)->file_data[(*env)->index].max, (*env)->file_data[(*env)->index].tmp1, nb);
                    (*env)->file_data[(*env)->index].index = (*env)->file_data[(*env)->index].max;
                    (*env)->file_data[(*env)->index].max += nb;
                    memset((*env)->file_data[(*env)->index].tmp1, '\0', 4096);
                    (*env)->new = 1;
                }
            } else if ((*env)->file_data[(*env)->index].index == (*env)->file_data[(*env)->index].max) {
                (*env)->file_data[(*env)->index].ldata[(*env)->file_data[(*env)->index].index - 1] = '\0';
                (*env)->file_data[(*env)->index].index--;
                (*env)->file_data[(*env)->index].max--;
                (*env)->new = 1;
            } else {
                memcpy((*env)->file_data[(*env)->index].tmp1, (*env)->file_data[(*env)->index].ldata, (*env)->file_data[(*env)->index].index - 1);
                memcpy((*env)->file_data[(*env)->index].tmp2, (*env)->file_data[(*env)->index].ldata + (*env)->file_data[(*env)->index].index, (*env)->file_data[(*env)->index].max - (*env)->file_data[(*env)->index].index);
                sprintf((*env)->file_data[(*env)->index].ldata, "%s%s", (*env)->file_data[(*env)->index].tmp1, (*env)->file_data[(*env)->index].tmp2);
                memset((*env)->file_data[(*env)->index].tmp1, '\0', 4096);
                memset((*env)->file_data[(*env)->index].tmp2, '\0', 4096);
                (*env)->file_data[(*env)->index].index--;
                (*env)->file_data[(*env)->index].max--;
                (*env)->new = 1;
            }
            pthread_spin_unlock(&(*env)->lock);
        // Updated
        } else if (c == 0x1B) {
            read(STDIN_FILENO,str,2);
            if (!strcmp(str, "[A")) { // Up Arrow
                pthread_spin_lock(&(*env)->lock);
                if ((*env)->index == 0) {
                } else {
                    if ((*env)->file_data[(*env)->index].index < (*env)->file_data[(*env)->index - 1].index) {
                        (*env)->file_data[(*env)->index - 1].index = (*env)->file_data[(*env)->index].index;
                    } else {
                        (*env)->file_data[(*env)->index - 1].index = (*env)->file_data[(*env)->index - 1].max;
                    }
                    (*env)->index--;
                    (*env)->new = 1;
                }
                pthread_spin_unlock(&(*env)->lock);
            } else if (!strcmp(str, "[D")) { // Left Arrow
                pthread_spin_lock(&(*env)->lock);
                if ((*env)->file_data[(*env)->index].index == 0) {
                } else {
                    (*env)->file_data[(*env)->index].index--;
                    (*env)->new = 1;
                }
                pthread_spin_unlock(&(*env)->lock);
            } else if (!strcmp(str, "[C")) { // Right Arrow
                pthread_spin_lock(&(*env)->lock);
                if ((*env)->file_data[(*env)->index].index == (*env)->file_data[(*env)->index].max) {
                } else {
                    (*env)->file_data[(*env)->index].index++;
                    (*env)->new = 1;
                }
                pthread_spin_unlock(&(*env)->lock);
            } else if (!strcmp(str, "[B")) { // Down Arrow
                pthread_spin_lock(&(*env)->lock);
                if ((*env)->index == ((*env)->max)) {
                } else {
                    if ((*env)->file_data[(*env)->index].index < (*env)->file_data[(*env)->index + 1].index) {
                        (*env)->file_data[(*env)->index + 1].index = (*env)->file_data[(*env)->index].index;
                    } else {
                        (*env)->file_data[(*env)->index + 1].index = (*env)->file_data[(*env)->index + 1].max;
                    }
                    (*env)->index++;
                    (*env)->new = 1;
                }
                pthread_spin_unlock(&(*env)->lock);
            }
        // Updated
        } else if (c == 0x0A) {
            pthread_spin_lock(&(*env)->lock);
            (*env)->alloc_size++;
            MEM_((*env)->file_data, (*env)->alloc_size, struct line);
            MEM_((*env)->tmp1, (*env)->alloc_size, struct line);
            MEM_((*env)->tmp2, (*env)->alloc_size, struct line);
            /*if ((*env)->index == ((*env)->max)) {
                (*env)->max++;
                (*env)->index++;
                init_line(env);
            } else*/ if ((*env)->file_data[(*env)->index].index == 0) {
                (*env)->max++;
                for (unsigned long i = (*env)->max; i > (*env)->index; i--) {
                    (*env)->file_data[i] = (*env)->file_data[i-1];
                }
                init_line(env);
                (*env)->index++;
            } else if ((*env)->file_data[(*env)->index].index == (*env)->file_data[(*env)->index].max) {
                for (unsigned long i = (*env)->max; i > ((*env)->index + 1); i--) {
                    (*env)->file_data[i] = (*env)->file_data[i-1];
                }
                (*env)->max++;
                (*env)->index++;
                init_line(env);
            } else {
                (*env)->max++;
                unsigned long nb = (*env)->file_data[(*env)->index].max - (*env)->file_data[(*env)->index].index;
                memcpy((*env)->file_data[(*env)->index].tmp1, (*env)->file_data[((*env)->index)].ldata + (*env)->file_data[(*env)->index].index, nb);
                for (unsigned long i = (*env)->max; i > ((*env)->index + 1); i--) {
                    (*env)->file_data[i] = (*env)->file_data[i-1];
                }
                (*env)->file_data[(*env)->index].ldata[(*env)->file_data[(*env)->index].index] = '\0';
                (*env)->file_data[(*env)->index].max = (*env)->file_data[(*env)->index].index;
                (*env)->index++;
                init_line(env);
                memcpy((*env)->file_data[(*env)->index].ldata, (*env)->file_data[(*env)->index-1].tmp1, nb);
                (*env)->file_data[(*env)->index].index = 0;
                (*env)->file_data[(*env)->index].max = nb;
                memset((*env)->file_data[(*env)->index].tmp1, '\0', 4096);
            }
            (*env)->new = 1;
            pthread_spin_unlock(&(*env)->lock);
        } else if (c == 0x09) {
            pthread_spin_lock(&(*env)->lock);
            if ((*env)->file_data[(*env)->index].index == (*env)->file_data[(*env)->index].max) {
                (*env)->file_data[(*env)->index].ldata[(*env)->file_data[(*env)->index].index] = c;
            } else {
                memcpy((*env)->file_data[(*env)->index].tmp1, (*env)->file_data[(*env)->index].ldata, (*env)->file_data[(*env)->index].index);
                memcpy((*env)->file_data[(*env)->index].tmp2, (*env)->file_data[(*env)->index].ldata + (*env)->file_data[(*env)->index].index, (*env)->file_data[(*env)->index].max - (*env)->file_data[(*env)->index].index);
                sprintf((*env)->file_data[(*env)->index].ldata, "%s%c%s", (*env)->file_data[(*env)->index].tmp1, c, (*env)->file_data[(*env)->index].tmp2);
                memset((*env)->file_data[(*env)->index].tmp1, '\0', 4096);
                memset((*env)->file_data[(*env)->index].tmp2, '\0', 4096);
            }
            (*env)->file_data[(*env)->index].max++;
            (*env)->file_data[(*env)->index].index++;
            (*env)->new = 1;
            pthread_spin_unlock(&(*env)->lock);
        } else if (c == 0x13) {
            pthread_spin_lock(&(*env)->lock);
            CHECK(ret = write_file(env));
            pthread_spin_unlock(&(*env)->lock);
        } else if (c == 0x11) {
            pthread_spin_lock(&(*env)->lock);
            char t = 'n';
            printf("Save? [y/n]\n");
            while(!read(STDIN_FILENO, &t, 1));
            if (t == 'y')
                CHECK(ret = write_file(env));
            (*env)->quit = 1;
            pthread_spin_unlock(&(*env)->lock);
        }
        c = '\0';
	memset(str, '\0', 9);
    }
exit:
    (*env)->quit = 1;
    pthread_exit(&ret);
}

err read_file(struct environment **env)
{
    err ret = SUCCESS;
    FILE *f = NULL;

    VALID(f = fopen((*env)->file_name, "a+"), FILE_ERROR, READ_ERROR);

    unsigned int i = 0;

    while(fgets((*env)->file_data[i].ldata, 4096, f)) {
        (*env)->alloc_size++;
        (*env)->max++;
        (*env)->file_data[i].index = 0;
        (*env)->file_data[i].max = strlen((*env)->file_data[i].ldata) - 1;
        (*env)->file_data[i].ldata[(*env)->file_data[i].max] = '\0';
        //printf("%d: %ld\n", i, (*env)->file_data[i].max);
        MEM_((*env)->file_data, (*env)->alloc_size, struct line);
        i++;
        (*env)->index++;
        init_line(env);
    }
    (*env)->new = 1;
    (*env)->index = 0;
    MEM_((*env)->tmp1, (*env)->alloc_size, struct line);
    MEM_((*env)->tmp2, (*env)->alloc_size, struct line);

exit:
    if (f)
        fclose(f);

    return ret;
}

err write_file(struct environment **env)
{
    err ret = SUCCESS;

    FILE *f;

    char c = '\0';
    unsigned long l = 0;

    if (!(*env)->valid_fname) {
        printf("Enter file name\n");
        while(c != '\n') {
            if (read(STDIN_FILENO, &c, 1)) {
                if (c != '\n') {
                    l++;
                    MEM_((*env)->file_name, l, char);
                    (*env)->file_name[l-1] = c;
                }
            }
        }
        (*env)->valid_fname = 1;
    }

    VALID(f = fopen((*env)->file_name, "w+"), FILE_ERROR, WRITE_ERROR);

    for (unsigned long i = 0; i <= (*env)->max; i++) {
        fwrite((*env)->file_data[i].ldata, sizeof(char), (*env)->file_data[i].max, f);
        fprintf(f, "\n");
    }

exit:
    if (f)
        fclose(f);

    return ret;
}
