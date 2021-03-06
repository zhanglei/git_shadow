#ifndef ZNETUTILS_H
#define ZNETUTILS_H

#include "zcommon.h"
#include <netdb.h>

struct zNetUtils__ {
    _i (* gen_serv_sd) (char *, char *, char *, znet_proto_t);

    _i (* conn) (char *, char *, char *, znet_proto_t);

    _i (* send) (_i, void *, size_t);
    _i (* sendto) (_i, void *, size_t, struct sockaddr *, zip_t);
    _i (* sendmsg) (_i, struct iovec *, size_t, struct sockaddr *, zip_t);
    _i (* recv_all) (_i, void *, size_t, struct sockaddr *, socklen_t *);

    _i (* to_numaddr) (const char *, zip_t, _ull *);
    _i (* to_straddr) (_ull *, zip_t, char *);

    _i (* send_fd) (const _i, const _i, void *, _i);
    _i (* recv_fd) (const _i);
};

#endif  // #ifndef ZNETUTILS_H
