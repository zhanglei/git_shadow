#ifndef ZDPOPS_H
#define ZDPOPS_H

#ifndef _Z_BSD
    #ifndef _XOPEN_SOURCE
    #define _XOPEN_SOURCE 700
    #endif

    #ifndef _DEFAULT_SOURCE
    #define _DEFAULT_SOURCE
    #endif

    #ifndef _BSD_SOURCE
    #define _BSD_SOURCE
    #endif
#endif

#include <pthread.h>  //  该头文件内部已使用 #define _PTHREAD_H 避免重复
#include <semaphore.h>  //  该头文件内部已使用 #define _SEMAPHORE_H 避免重复
#include <libpq-fe.h>  //  该头文件内部已使用 #define LIBPQ_FE_H 避免重复

#include "zCommon.h"
#include "zNetUtils.h"
#include "zLibSsh.h"
#include "zLibGit.h"
#include "zNativeOps.h"
#include "zNativeUtils.h"
#include "zPosixReg.h"
#include "zPgSQL.h"
#include "zThreadPool.h"
#include "cJSON.h"

#define zGlobRepoNumLimit 256  // 可以管理的代码库数量上限
#define zGlobRepoIdLimit 10 * 256  // 代码库 ID 上限
#define zCacheSiz 64  // 顶层缓存单元数量取值不能超过 IOV_MAX
#define zDpTraficLimit 256  // 同一项目可同时发出的 push 连接数量上限
#define zDpHashSiz 1009  // 布署状态HASH的大小，不要取 2 的倍数或指数，会导致 HASH 失效，应使用 奇数
#define zSendUnitSiz 8  // sendmsg 单次发送的单元数量，在 Linux 平台上设定为 <=8 的值有助于提升性能
#define zForecastedHostNum 200  // 预测的目标主机数量上限

#define zGlobCommonBufSiz 1024

#define zDpUnLock 0
#define zDpLocked 1

#define zRepoGood 0
#define zRepoDamaged 1

#define zIsCommitDataType 0
#define zIsDpDataType 1

typedef struct __zThreadPool__ {
    pthread_t selfTid;
    pthread_cond_t condVar;

    void * (* func) (void *);
    void *p_param;
} zThreadPool__;

typedef struct __zDpCcur__ {
    zThreadPool__ *p_threadSource_;  // 必须放置在首位
    _i repoId;
    char *p_hostIpStrAddr;  // 单个目标机 Ip，如："10.0.0.1"
    char *p_hostServPort;  // 字符串形式的端口号，如："22"
    char *p_cmd;  // 需要执行的指令集合

    _i authType;
    const char *p_userName;
    const char *p_pubKeyPath;  // 公钥所在路径，如："/home/git/.ssh/id_rsa.pub"
    const char *p_privateKeyPath;  // 私钥所在路径，如："/home/git/.ssh/id_rsa"
    const char *p_passWd;  // 登陆密码或公钥加密密码

    char *p_remoteOutPutBuf;  // 获取远程返回信息的缓冲区
    _ui remoteOutPutBufSiz;

    pthread_cond_t *p_ccurCond;  // 线程同步条件变量
    pthread_mutex_t *p_ccurLock;  // 同步锁
    _ui *p_taskCnt;  // SSH 任务完成计数
} zDpCcur__;

typedef struct __zDpRes__ {
    _ui clientAddr;  // 无符号整型格式的IPV4地址：0xffffffff
    _i dpState;  // 布署状态：已返回确认信息的置为1，否则保持为 -1
    _i initState;  // 远程主机初始化状态：已返回确认信息的置为1，否则保持为 -1
    char errMsg[256];  // 存放目标主机返回的错误信息
    struct __zDpRes__ *p_next;
} zDpRes__;

/* 在zSend__之外，添加了：本地执行操作时需要，但对前端来说不必要的数据段 */
typedef struct __zRefData__ {
    struct __zVecWrap__ *p_subVecWrap_;  // 传递给 sendmsg 的下一级数据
    char *p_data;  // 实际存放数据正文的地方
} zRefData__;

/* 对 struct iovec 的封装，用于 zsendmsg 函数 */
typedef struct __zVecWrap__ {
    _i vecSiz;
    struct iovec *p_vec_;  // 此数组中的每个成员的 iov_base 字段均指向 p_refData_ 中对应的 p_data 字段
    struct __zRefData__ *p_refData_;
} zVecWrap__;

/* 用于存放每个项目的元信息，同步锁不要紧挨着定义，在X86平台上可能会带来伪共享问题降低并发性能 */
typedef struct {
    zThreadPool__ *p_threadSource_;  // 必须放置在首位
    _i repoId;  // 项目代号
    time_t  cacheId;  // 即：最新一次布署的时间戳(初始化为1000000000)
    char *p_repoPath;  // 项目路径，如："/home/git/miaopai_TEST"
    _i repoPathLen;  // 项目路径长度，避免后续的使用者重复计算
    _i maxPathLen;  // 项目最大路径长度：相对于项目根目录的值（由底层文件系统决定），用于度量git输出的差异文件相对路径长度

    char *p_sourceUrl;
    char *p_pullRefs;  // 例如：refs/remotes/origin/master:refs/heads/server99
    pthread_mutex_t pullLock;  // 保证同一时间同一个项目只有一个git pull进程在运行
    char needPull;  // 置为 'N' 表示该项目会主动推送代码到中控机，不需要拉取远程代码

    char initFinished;  /* 仓库是否已经初始化完成：N 代表动作尚未完成，Y 代表已完成 */

    /* 用于区分是布署动作占用写锁还是生成缓存占用写锁：1 表示布署占用，0 表示生成缓存占用 */
    _i whoGetWrLock;
    /* 远程主机初始化或布署动作开始时间，用于统计每台目标机器大概的布署耗时*/
    time_t  dpBaseTimeStamp;
    /* 布署超时上限 */
    time_t dpTimeWaitLimit;
    /* 目标机在重要动作执行前回发的keep alive消息 */
    time_t dpKeepAliveStamp;

    /* 本项目 git 库全局 Handler */
    git_repository *p_gitRepoHandler;

    /* 本项目 pgSQL 全局 Handler */
    zPgConnHd__ *p_pgConnHd_;

    /* 用于控制并发流量的信号量 */
    sem_t dpTraficControl;

    /* libssh2 与 libgit2 共用的并发同步锁与条件变量 */
    pthread_mutex_t dpSyncLock;
    pthread_cond_t dpSyncCond;
    _ui totalHost;  // 每个项目的目标主机总数量，此值不能修改
    _ui dpTotalTask;  // 用于统计总任务数，可动态修改
    _ui dpTaskFinCnt;  // 用于统计任务完成数，仅代表执行函数返回
    _ui dpReplyCnt;  // 用于统计最终状态返回

    /* 0：非锁定状态，允许布署或撤销、更新ip数据库等写操作 */
    /* 1：锁定状态，拒绝执行布署、撤销、更新ip数据库等写操作，仅提供查询功能 */
    _c dpLock;
    /* 代码库状态，若上一次布署／撤销失败，此项置为 zRepoDamaged 状态，用于提示用户看到的信息可能不准确 */
    _c repoState;
    _c resType[2];  // 用于标识收集齐的结果是全部成功，还是其中有异常返回而增加的计数：[0] 远程主机初始化 [1] 布署

    char lastDpSig[44];  // 存放最近一次布署的 40 位 SHA1 sig
    char dpingSig[44];  // 正在布署过程中的版本号，用于布署耗时分析
    char jsonPrefix[256];  // 网络交互时用到的 json 格式前驱信息
    _s jsonPrefixLen;  // 实际的 json 前驱数据长度

    pthread_mutex_t replyCntLock;  // 用于保证 ReplyCnt 计数的正确性

    zDpCcur__ dpCcur_[zForecastedHostNum];
    zDpCcur__ *p_dpCcur_;
    zDpRes__ *p_dpResList_;  // 1、更新 IP 时对比差异；2、收集布署状态
    zDpRes__ *p_dpResHash_[zDpHashSiz];  // 对上一个字段每个值做的散列

    pthread_rwlock_t rwLock;  // 每个代码库对应一把全局读写锁，用于写日志时排斥所有其它的写操作
    //pthread_rwlockattr_t zRWLockAttr;  // 全局锁属性：写者优先
    pthread_mutex_t dpRetryLock;  // 用于分离失败重试布署与生成缓存之间的锁竞争

    zVecWrap__ commitVecWrap_;  // 存放 commit 记录的原始队列信息
    struct iovec commitVec_[zCacheSiz];
    zRefData__ commitRefData_[zCacheSiz];

    zVecWrap__ sortedCommitVecWrap_;  // 存放经过排序的 commit 记录的缓存队列信息，提交记录总是有序的，不需要再分配静态空间

    zVecWrap__ dpVecWrap_;  // 存放 deploy 记录的原始队列信息
    struct iovec dpVec_[zCacheSiz];
    zRefData__ dpRefData_[zCacheSiz];

    zVecWrap__ sortedDpVecWrap_;  // 存放经过排序的 deploy 记录的缓存（从文件里直接取出的是旧的在前面，需要逆向排序）
    struct iovec sortedDpVec_[zCacheSiz];

    void *p_memPool;  // 线程内存池，预分配 16M 空间，后续以 8M 为步进增长
    pthread_mutex_t memLock;  // 内存池锁
    _ui memPoolOffSet;  // 动态指示下一次内存分配的起始地址
} zRepo__;


/* 既有的项目 ID 最大值 */
extern _i zGlobMaxRepoId;

/* 系统 CPU 与 MEM 负载监控：以 0-100 表示 */
extern pthread_mutex_t zGlobCommonLock;
extern pthread_cond_t zGlobCommonCond;  // 系统由高负载降至可用范围时，通知等待的线程继续其任务(注：使用全局通用锁与之配套)
extern _ul zGlobMemLoad;  // 高于 80 拒绝布署，同时 git push 的过程中，若高于 80 则剩余任阻塞等待
extern char zGlobPgConnInfo[2048];  // postgreSQL 全局统一连接方式：所有布署相关数据存放于一个数据库中

/* 指定服务端自身的Ip地址与端口 */
typedef struct {
    char *p_ipAddr;  // 字符串形式的ip点分格式地式
    char *p_port;  // 字符串形式的端口，如："80"
    _i servType;  // 网络服务类型：TCP/UDP
} zNetSrv__;

extern zNetSrv__ zNetSrv_;

/* 全局 META HASH */
extern zRepo__ *zpGlobRepo_[zGlobRepoIdLimit];

typedef struct __zMeta__ {
    _i opsId;  // 网络交互时，代表操作指令（从0开始的连续排列的非负整数）
    _i repoId;  // 项目代号（从0开始的连续排列的非负整数）
    _i commitId;  // 版本号（对应于svn或git的单次提交标识）
    _i fileId;  // 单个文件在差异文件列表中index
    _ui hostId;  // 32位IPv4地址转换而成的无符号整型格式
    _l cacheId;  // 缓存版本代号（最新一次布署的时间戳）
    _i dataType;  // 缓存类型，zIsCommitDataType/zIsDpDataType
    char *p_data;  // 数据正文，发数据时可以是版本代号、文件路径等(此时指向zRefData__的p_data)等，收数据时可以是接IP地址列表(此时额外分配内存空间)等
    _i dataLen;  // 不能使和 _ui 类型，recv 返回 -1 时将会导致错误
    char *p_extraData;  // 附加数据，如：字符串形式的UNIX时间戳、IP总数量等
    _i extraDataLen;

    /* 以下为 Tree 专属数据 */
    struct __zMeta__ *p_father;  // Tree 父节点
    struct __zMeta__ *p_left;  // Tree 左节点
    struct __zMeta__ *p_firstChild;  // Tree 首子节点：父节点唯一直接相连的子节点
    struct __zMeta__ **pp_resHash;  // Tree 按行号对应的散列
    _i lineNum;  // 行号
    _i offSet;  // 纵向偏移
} zMeta__;

struct zDpOps__ {
    _i (* show_meta) (cJSON *, _i);

    _i (* print_revs) (cJSON *, _i);
    _i (* print_diff_files) (cJSON *, _i);
    _i (* print_diff_contents) (cJSON *, _i);

    _i (* creat) (cJSON *, _i);
    _i (* req_dp) (cJSON *, _i);

    _i (* dp) (cJSON *, _i);
    _i (* state_confirm) (cJSON *, _i);

    _i (* lock) (cJSON *, _i);
    _i (* unlock) (cJSON *zpJRoot, _i zSd);

    _i (* req_file) (cJSON *, _i);

    void * (* route) (void *);
};

#endif  //  #ifndef ZDPOPS_H