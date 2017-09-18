#include <stdio.h>

    #include <stdlib.h>
    #include <string.h>
    #include <errno.h>
    #include <sys/signal.h>

#include "zregex.c"

_i
main(_i zArgc, char **zppArgv) {
    zRegInitInfo zRegInitIf;
    zRegResInfo zRegResIf;

    zreg_compile(&zRegInitIf, zppArgv[1]);
    zreg_match(&zRegResIf, &zRegInitIf, zppArgv[2]);

    printf("%d\n", zRegResIf.cnt);
    for (_i zCnter = 0; zCnter < zRegResIf.cnt; zCnter++) {
        printf("%s\n", zRegResIf.p_rets[zCnter]);
    }

    zreg_free_tmpsource(&zRegResIf);
    zreg_free_metasource(&zRegInitIf);
}