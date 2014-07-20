#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "daemon.h"

pid_t _daemon_pid = -1;
char *_pid_file = NULL;

int check_process_running(const char *comm, char *pidfile)
{
    FILE *f = NULL;
    char buffer[256];
    unsigned long int pid;

    if ((f = fopen(pidfile, "r")) == NULL)
        return -1;

    fgets(buffer, 256, f);
    fclose(f);

    pid = strtoul(buffer, NULL, 10);
    if (pid == 0)
        return -1;

    sprintf(buffer, "/proc/%lu/comm", pid);
    if (access(buffer, F_OK) != 0)
        return -1;

    if ((f = fopen(buffer, "r")) == NULL)
        return -1;

    fgets(buffer, 256, f);
    fclose(f);

    size_t len = strlen(buffer);
    if (len == 0)
        return -1;

    if (buffer[len - 1] == '\n')
        buffer[len - 1] = 0;

    if (strcmp(buffer, comm) == 0)
        return 0;

    return -1;
}

pid_t start_daemon(const char *comm, char *pidfile)
{
    if (pidfile && pidfile[0] != '\0') {
        _pid_file = strdup(pidfile);
    }
    else {
        fprintf(stderr, "no pidfile given, not forking\n");
        return -1;
    }

    if (access(pidfile, F_OK) == 0) {
        fprintf(stderr, "pidfile exists, check if process with name \"%s\" is running.\n", comm);
        if (check_process_running(comm, pidfile) == 0)
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
