#ifndef ZLOCALUTILS_H
#define ZLOCALUTILS_H

#include "zcommon.h"
#include <stdio.h>

struct zNativeUtils__ {
    void (* close_fds) (pid_t, _i);
    void (* daemonize) (const char *);
    void (* sleep) (_d);
    void * (* system) (void *);

    void * (* read_line) (char *, _i, FILE *);
    _i (* read_hunk) (char *, size_t, FILE *);

    _i (* del_lb) (char *);

    _i (* path_del) (char *);
    _i (* path_cp) (char *, char *);
};

#endif  // #ifndef ZLOCALUTILS_H
