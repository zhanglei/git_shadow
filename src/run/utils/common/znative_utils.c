#include "znative_utils.h"

#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <signal.h>

#include <ftw.h>
#include <pthread.h>

#include <time.h>
#include <errno.h>

#include "zrun.h"

extern struct zRun__ zRun_;
extern zRepo__ *zpRepo_;

static void zclose_fds(pid_t zPid, _i zKeepSd);
static void zdaemonize(const char *zpWorkDir);

static void * zget_one_line(char *zpBufOUT, _i zSiz, FILE *zpFile);
static _i zget_str_content(char *zpBufOUT, size_t zSiz, FILE *zpFile);

static void zsleep(_d zSecs);
static void * zthread_system(void *zpCmd);

static _i zdel_linebreak(char *zpStr);

static _i zpath_del(char *zpPath);
static _i zpath_cp(char *zpDestpath, char *zpSrcPath);

struct zNativeUtils__ zNativeUtils_ = {
    .close_fds = zclose_fds,
    .daemonize = zdaemonize,

    .sleep = zsleep,

    .system = zthread_system,

    .read_line = zget_one_line,
    .read_hunk = zget_str_content,

    .del_lb = zdel_linebreak,

    .path_del = zpath_del,
    .path_cp = zpath_cp
};

// /*
//  * Functions for base64 coding [and decoding(TO DO)]
//  */
// char zBase64Dict[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
// char *
// zstr_to_base64(const char *zpOrig) {
//     _i zOrigLen = strlen(zpOrig);
//     _i zMax = (0 == zOrigLen % 3) ? (zOrigLen / 3 * 4) : (1 + zOrigLen / 3 * 4);
//     _i zResLen = zMax + (4- (zMax % 4));
//
//     char zRightOffset[zMax], zLeftOffset[zMax];
//
//     char *zRes;
//     zMEM_ALLOC(zRes, char, zResLen);
//
//     _i i, j;
//
//     for (i = j = 0; i < zMax; i++) {
//         if (3 == (i % 4)) {
//             zRightOffset[i] = 0;
//             zLeftOffset[i] = 0;
//         } else {
//             zRightOffset[i] = zpOrig[j]>>(2 * ((j % 3) + 1));
//             zLeftOffset[i] = zpOrig[j]<<(2 * (2 - (j % 3)));
//             j++;
//         }
//     }
//
//     _c mask = 63;
//     zRes[0] = zRightOffset[0] & mask;
//
//     for (i = 1; i < zMax; i++) { zRes[i] = (zRightOffset[i]|zLeftOffset[i-1]) & mask; }
//     zRes[zMax - 1] = zLeftOffset[zMax - 2] & mask;
//
//     for (i = 0; i < zMax; i++) { zRes[i] = zBase64Dict[(_i)zRes[i]]; }
//     for (i = zMax; i < zResLen; i++) { zRes[i] = '='; }
//
//     return zRes;
// }

/*
 * Daemonize a linux process to daemon.
 */
static void
zclose_fds(pid_t zPid, _i zKeepSd) {
    struct dirent *zpDir_;
    char zPath[64];

    sprintf(zPath, "/proc/%d/fd", zPid);

    _i zFd;
    DIR *zpDir = opendir(zPath);

    while (NULL != (zpDir_ = readdir(zpDir))) {
        zFd = strtol(zpDir_->d_name, NULL, 10);
        if (zFd != zKeepSd) {
            close(zFd);
        }
    }

    closedir(zpDir);
}

static void
zdaemonize(const char *zpWorkDir) {
    zIGNORE_ALL_SIGNAL();

//  sigset_t zSigToBlock;
//  sigfillset(&zSigToBlock);
//  pthread_sigmask(SIG_BLOCK, &zSigToBlock, NULL);

    umask(0);
    zCHECK_NEGATIVE_RETURN(chdir(NULL == zpWorkDir? "/" : zpWorkDir),);

    pid_t zPid = fork();
    zCHECK_NEGATIVE_EXIT(zPid);

    if (zPid > 0) {
        exit(0);
    }

    setsid();
    zPid = fork();
    zCHECK_NEGATIVE_RETURN(zPid,);

    if (zPid > 0) {
        exit(0);
    }

    zclose_fds(getpid(), -1);

    _i zFD = open("/dev/null", O_RDWR);
    dup2(zFD, 1);
//  dup2(zFD, 2);
}

/*
 * Fork a child process to exec an outer command.
 * The "zppArgv" must end with a "NULL"
 */
// void
// zfork_do_exec(const char *zpCommand, char **zppArgv) {
//     pid_t zPid = fork();
//     zCHECK_NEGATIVE_EXIT(zPid);
//
//     if (0 == zPid) {
//         execve(zpCommand, zppArgv, NULL);
//     } else {
//         waitpid(zPid, NULL, 0);
//     }
// }

/*
 * 以返回是否是 NULL 为条件判断是否已读完所有数据
 * 可重入，可用于线程
 * 适合按行读取分别处理的场景
 */
static void *
zget_one_line(char *zpBufOUT, _i zSiz, FILE *zpFile) {
    char *zpRes = fgets(zpBufOUT, zSiz, zpFile);
    if (NULL == zpRes && (0 == feof(zpFile))) {
        zPRINT_ERR_EASY("<fgets> ERROR!");
        exit(1);
    }
    return zpRes;
}

/*
 * 以返回值小于 zSiz 为条件判断是否到达末尾（读完所有数据 )
 * 可重入，可用于线程
 * 适合一次性大量读取所有文本内容的场景
 */
static _i
zget_str_content(char *zpBufOUT, size_t zSiz, FILE *zpFile) {
    size_t zCnt;
    zCHECK_NEGATIVE_EXIT(
            zCnt = read(fileno(zpFile), zpBufOUT, zSiz)
            );
    return zCnt;
}

// // 注意：fread 版的实现会将行末的换行符处理掉
// _i
// zget_str_content_1(char *zpBufOUT, size_t zSiz, FILE *zpFile) {
//     size_t zCnt = fread(zpBufOUT, zBYTES(1), zSiz, zpFile);
//     if (zCnt < zSiz && (0 == feof(zpFile))) {
//         zPRINT_ERR_EASY("<fread> ERROR!");
//         exit(1);
//     }
//     return zCnt;
// }

/*
 * 纳秒级sleep，小数点形式赋值
 */
static void
zsleep(_d zSecs) {
    struct timespec zNanoSec_;
    zNanoSec_.tv_sec = (_i) zSecs;
    zNanoSec_.tv_nsec  = (zSecs - zNanoSec_.tv_sec) * 1000000000;
    nanosleep( &zNanoSec_, NULL );
}

/*
 * 纳秒时间，用于两个时间之间精确差值[ 计数有问题，且 CentOS-6 上不可用 ]
 */
// _d
// zreal_time() {
//     struct timespec zNanoSec_;
//     if (0 > clock_gettime(CLOCK_REALTIME, &zNanoSec_)) {
//         return -1.0;
//     } else {
//         return (zNanoSec_.tv_sec + (((_d) zNanoSec_.tv_nsec) / 1000000000));
//     }
// }

/*
 * 用于在单独线程中执行外部命令，如：定时拉取远程代码时，可以避免一个拉取动作卡住，导致后续的所有拉取都被阻塞
 */
static void *
zthread_system(void *zpCmd) {
    if (NULL != zpCmd) {
        system((char *) zpCmd);
    }

    return NULL;
}

// /*
//  *  检查一个目录是否已存在
//  *  返回：1表示已存在，0表示不存在，-1表示出错
//  */
// _i
// zCheck_Dir_Existence(char *zpDirPath) {
//     _i zFd;
//     if (-1 == (zFd = open(zpDirPath, O_RDONLY|O_DIRECTORY))) {
//         if (EEXIST == errno) {
//             return 1;
//         } else {
//             return -1;
//         }
//     }
//     close(zFd);
//     return 0;
// }

/*
 * 去除用字符串末尾的一个或多个换行符LB (Line Break)
 * 返回新的字符串长度，不含最后的 '\0'
 */
static _i
zdel_linebreak(char *zpStr) {
    char *zpStrPtr = zpStr;
    _ui zStrLen = strlen(zpStr);

    while ('\n' == zpStrPtr[zStrLen - 1]) { zStrLen--; }
    zpStrPtr[zStrLen] = '\0';

    return zStrLen;
}


/*
 * 递归删除指定路径及其下所有子路径与文件
 */
static _i
zpath_del_cb(const char *zpPath, const struct stat *zpS __attribute__ ((__unused__)),
        int zType, struct FTW *zpF __attribute__ ((__unused__))) {
    _c zErrNo = 0;

    if (FTW_F == zType || FTW_SL == zType || FTW_SLN == zType) {
        if (0 != unlink(zpPath)) {
            zPRINT_ERR_EASY(zpPath);
            zErrNo = -1;
        }
    } else if (FTW_DP == zType) {
        if (0 != rmdir(zpPath)) {
            zPRINT_ERR_EASY(zpPath);
            zErrNo = -1;
        }
    } else {
        zPRINT_ERR_EASY("Unknown file type");
    }

    return zErrNo;
}

static _i
zpath_del(char *zpPath) {
    return nftw(zpPath, zpath_del_cb, 124, FTW_PHYS|FTW_DEPTH);
}


/*
 * 递归复制指定路径自身及其下所有子路径、文件到新的位置
 * 多线程环境必须持锁
 */
static pthread_mutex_t zPathCopyLock = PTHREAD_MUTEX_INITIALIZER;
static _i zDestFd;
static _i zRdFd, zWrFd;
static _i zRdLen;
static char zCopyBuf[4096];

static _i
zpath_copy_cb(const char *zpPath, const struct stat *zpS,
        int zType, struct FTW *zpF __attribute__ ((__unused__))) {
    if (FTW_D == zType || FTW_DNR == zType) {
        /* 排除 '.' */
        if ('\0' != zpPath[1]) {
            return 0;
        }

        zCHECK_NEGATIVE_RETURN(mkdirat(zDestFd, zpPath + 2, zpS->st_mode), -1);
    } else if (FTW_F == zType) {
        zCHECK_NEGATIVE_RETURN(zRdFd = open(zpPath, O_RDONLY), -1);

        if (0 > (zWrFd = openat(zDestFd, zpPath + 2,
                        O_WRONLY|O_CREAT|O_TRUNC|O_EXCL, zpS->st_mode))) {
            close(zRdFd);
            zPRINT_ERR_EASY(zpPath + 2);
            return -1;
        }

        while (0 < (zRdLen = read(zRdFd, zCopyBuf, 4096))) {
            if (zRdLen != write(zWrFd, zCopyBuf, zRdLen)) {
                close(zRdFd);
                close(zWrFd);
                zPRINT_ERR_EASY(zpPath + 2);
                return -1;
            }
        }

        close(zRdFd);
        close(zWrFd);
    } else if (FTW_SL == zType || FTW_SLN == zType) {
        zCHECK_NEGATIVE_RETURN(zRdLen = readlink(zpPath, zCopyBuf, 4096), -1);
        zCopyBuf[zRdLen] = '\0';

        zCHECK_NEGATIVE_RETURN(symlinkat(zCopyBuf, zDestFd, zpPath + 2), -1);
    } else {
        /* 文件类型无法识别 */
        zPRINT_ERR_EASY(zpPath);
        return -1;
    }

    return 0;
}

static _i
zpath_cp(char *zpDestpath, char *zpSrcPath) {
    _i zErrNo = 0;

    /* 首先切换至源路径 */
    zCHECK_NEGATIVE_RETURN(chdir(zpSrcPath), -1);

    /* 尝试创建目标路径，不必关心结果 */
    mkdir(zpDestpath, 0755);

    /*
     * 取得目标路径的 fd
     * 使用了全局变量，需要加锁，直到路径完全复制完成
     */
    pthread_mutex_lock( & zPathCopyLock );
    if (0 > (zDestFd = open(zpDestpath, O_RDONLY|O_DIRECTORY))) {
        zErrNo = -1;
        zPRINT_ERR_EASY_SYS();
    } else {
        /* 此处必须使用相对路径 "." */
        if (0 != nftw(".", zpath_copy_cb, 124, FTW_PHYS)) {
            zErrNo = -1;
            zPRINT_ERR_EASY_SYS();
        }

        close(zDestFd);
    }

    pthread_mutex_unlock( & zPathCopyLock );

    return zErrNo;
}
