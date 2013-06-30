#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "daemon.h"

pid_t _daemon_pid = -1;
char *_pid_file = NULL;

pid_t start_daemon(char *pidfile)
{
    if (pidfile && pidfile[0] != '\0') {
        _pid_file = strdup(pidfile);
    }
    else {
        fprintf(stderr, "no pidfile given, not forking\n");
        return -1;
    }

    if (access(pidfile, F_OK) == 0) {
        fprintf(stderr, "pidfile exists, not forking\n");
        return -1;
    }

    _daemon_pid = fork();

    if (_daemon_pid != -1 && _daemon_pid) {
        /* parent */
        FILE *f = fopen(pidfile, "w");
        if (f) {
            fprintf(f, "%d", _daemon_pid);
            fclose(f);
        }
        else {
            fprintf(stderr, "failed to create pidfile\n");
        }
        return _daemon_pid;
    }
    else {
        return _daemon_pid;
    }
}

int stop_daemon(void)
{
    if (_pid_file && _pid_file[0] != '\0') {
        unlink(_pid_file);
        free(_pid_file);
        _pid_file = NULL;
    }
    return 0;
}
