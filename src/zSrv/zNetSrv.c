#include "zNetSrv.h"

/***********
 * NET OPS *
 ***********/
/* 检查 CommitId 是否合法，宏内必须解锁 */
#define zCheck_CommitId() do {\
    if ((0 > zpMetaIf->CommitId)\
            || ((zCacheSiz - 1) < zpMetaIf->CommitId)\
            || (NULL == zpTopVecWrapIf->p_RefDataIf[zpMetaIf->CommitId].p_data)) {\
        pthread_rwlock_unlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock));\
        zpMetaIf->p_data[0] = '\0';\
        zpMetaIf->p_ExtraData[0] = '\0';\
        return -3;\
    }\
} while(0)

/* 检查 FileId 是否合法，宏内必须解锁 */
#define zCheck_FileId() do {\
    if ((0 > zpMetaIf->FileId)\
            || (NULL == zpTopVecWrapIf->p_RefDataIf[zpMetaIf->CommitId].p_SubVecWrapIf)\
            || ((zpTopVecWrapIf->p_RefDataIf[zpMetaIf->CommitId].p_SubVecWrapIf->VecSiz - 1) < zpMetaIf->FileId)) {\
        pthread_rwlock_unlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock));\
        zpMetaIf->p_data[0] = '\0';\
        zpMetaIf->p_ExtraData[0] = '\0';\
        return -4;\
    }\
} while(0)

/* 检查缓存中的CacheId与全局CacheId是否一致，若不一致，返回错误，此处不执行更新缓存的动作，宏内必须解锁 */
#define zCheck_CacheId() do {\
    if (zpGlobRepoIf[zpMetaIf->RepoId]->CacheId != zpMetaIf->CacheId) {\
        pthread_rwlock_unlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock));\
        zpMetaIf->p_data[0] = '\0';\
        zpMetaIf->p_ExtraData[0] = '\0';\
        zpMetaIf->CacheId = zpGlobRepoIf[zpMetaIf->RepoId]->CacheId;\
        return -8;\
    }\
} while(0)

/* 如果当前代码库处于写操作锁定状态，则解写锁，然后返回错误代码 */
#define zCheck_Lock_State() do {\
    if (zDpLocked == zpGlobRepoIf[zpMetaIf->RepoId]->DpLock) {\
        pthread_rwlock_unlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock));\
        zpMetaIf->p_data[0] = '\0';\
        zpMetaIf->p_ExtraData[0] = '\0';\
        return -6;\
    }\
} while(0)

/*
 * 1：添加新项目（代码库）
 */
_i
zadd_repo(zMeta__ *zpMetaIf, _i zSd) {
    _i zErrNo;
    if (0 == (zErrNo = zinit_one_repo_env(zpMetaIf->p_data))) {
        zsendto(zSd, "[{\"OpsId\":0}]", sizeof("[{\"OpsId\":0}]") - 1, 0, NULL);
    }

    return zErrNo;
}

/*
 * 5：显示所有项目及其元信息
 * 6：显示单个项目及其元信息
 */
_i
zshow_all_repo_meta(zMeta__ *zpMetaIf __attribute__ ((__unused__)), _i zSd) {
    char zSendBuf[zGlobBufSiz];

    zsendto(zSd, "[", zBytes(1), 0, NULL);  // 凑足json格式
    for(_i zCnter = 0; zCnter <= zGlobMaxRepoId; zCnter++) {
        if (NULL == zpGlobRepoIf[zCnter] || 0 == zpGlobRepoIf[zCnter]->zInitRepoFinMark) { continue; }

        if (0 != pthread_rwlock_tryrdlock(&(zpGlobRepoIf[zCnter]->RwLock))) {
            sprintf(zSendBuf, "{\"OpsId\":-11,\"data\":\"Id %d\"},", zCnter);
            zsendto(zSd, zSendBuf, strlen(zSendBuf), 0, NULL);
            continue;
        };

        sprintf(zSendBuf, "{\"OpsId\":0,\"data\":\"Id: %d\nPath: %s\nPermitDp: %s\nLastDpedRev: %s\nLastDpState: %s\nTotalHost: %d\nHostIPs:\"},",
                zCnter,
                zpGlobRepoIf[zCnter]->p_RepoPath,
                zDpLocked == zpGlobRepoIf[zCnter]->DpLock ? "No" : "Yes",
                '\0' == zpGlobRepoIf[zCnter]->zLastDpSig[0] ? "_" : zpGlobRepoIf[zCnter]->zLastDpSig,
                zRepoDamaged == zpGlobRepoIf[zCnter]->RepoState ? "fail" : "success",
                zpGlobRepoIf[zCnter]->TotalHost
                );
        zsendto(zSd, zSendBuf, strlen(zSendBuf), 0, NULL);

        pthread_rwlock_unlock(&(zpGlobRepoIf[zCnter]->RwLock));
    }

    zsendto(zSd, "{\"OpsId\":0,\"data\":\"__END__\"}]", sizeof("{\"OpsId\":0,\"data\":\"__END__\"}]") - 1, 0, NULL);  // 凑足json格式，同时防止内容为空时，前端无法解析
    return 0;
}

/*
 * 6：显示单个项目及其元信息
 */
_i
zshow_one_repo_meta(zMeta__ *zpParam, _i zSd) {
    zMeta__ *zpMetaIf = (zMeta__ *) zpParam;
    char zSendBuf[zGlobBufSiz];

    if (0 != pthread_rwlock_tryrdlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock))) {
        if (0 == zpGlobRepoIf[zpMetaIf->RepoId]->zWhoGetWrLock) {
            sprintf(zpMetaIf->p_data, "系统正在刷新缓存，请 2 秒后重试");
        } else {
            sprintf(zpMetaIf->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                    (0 == zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit) ? 2.0 : zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit / 60.0);
        }

        return -11;
    };

    sprintf(zSendBuf, "[{\"OpsId\":0,\"data\":\"Id %d\nPath: %s\nPermitDp: %s\nLastDpedRev: %s\nLastDpState: %s\nTotalHost: %d\nHostIPs:\"}]",
            zpMetaIf->RepoId,
            zpGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath,
            zDpLocked == zpGlobRepoIf[zpMetaIf->RepoId]->DpLock ? "No" : "Yes",
            '\0' == zpGlobRepoIf[zpMetaIf->RepoId]->zLastDpSig[0] ? "_" : zpGlobRepoIf[zpMetaIf->RepoId]->zLastDpSig,
            zRepoDamaged == zpGlobRepoIf[zpMetaIf->RepoId]->RepoState ? "fail" : "success",
            zpGlobRepoIf[zpMetaIf->RepoId]->TotalHost
            );
    zsendto(zSd, zSendBuf, strlen(zSendBuf), 0, NULL);

    pthread_rwlock_unlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock));
    return 0;
}

/*
 * 全量刷新：只刷新版本号列表
 * 需要继承下层已存在的缓存
 */
_i
zrefresh_cache(zMeta__ *zpMetaIf) {
//    _i zCnter[2];
//    struct iovec zOldVecIf[zpGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf.VecSiz];
//    zRefData__ zOldRefDataIf[zpGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf.VecSiz];
//
//    for (zCnter[0] = 0; zCnter[0] < zpGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf.VecSiz; zCnter[0]++) {
//        zOldVecIf[zCnter[0]].iov_base = zpGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf.p_VecIf[zCnter[0]].iov_base;
//        zOldVecIf[zCnter[0]].iov_len = zpGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf.p_VecIf[zCnter[0]].iov_len;
//        zOldRefDataIf[zCnter[0]].p_data  = zpGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf.p_RefDataIf[zCnter[0]].p_data;
//        zOldRefDataIf[zCnter[0]].p_SubVecWrapIf = zpGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf.p_RefDataIf[zCnter[0]].p_SubVecWrapIf;
//    }

    zgenerate_cache(zpMetaIf);  // 复用了 zops_route 函数传下来的 Meta__ 结构体(栈内存)

//    zCnter[1] = zpGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf.VecSiz;
//    if (zCnter[1] > zCnter[0]) {
//        for (zCnter[0]--, zCnter[1]--; zCnter[0] >= 0; zCnter[0]--, zCnter[1]--) {
//            if (NULL == zOldRefDataIf[zCnter[0]].p_SubVecWrapIf) { continue; }
//            if (NULL == zpGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf.p_RefDataIf[zCnter[1]].p_SubVecWrapIf) { break; }  // 若新内容为空，说明已经无法一一对应，后续内容无需再比较
//            if (0 == (strcmp(zOldRefDataIf[zCnter[0]].p_data, zpGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf.p_RefDataIf[zCnter[1]].p_data))) {
//                zpGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf.p_VecIf[zCnter[1]].iov_base = zOldVecIf[zCnter[0]].iov_base;
//                zpGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf.p_VecIf[zCnter[1]].iov_len = zOldVecIf[zCnter[0]].iov_len;
//                zpGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf.p_RefDataIf[zCnter[1]].p_SubVecWrapIf = zOldRefDataIf[zCnter[0]].p_SubVecWrapIf;
//            } else {
//                break;  // 若不能一一对应，则中断
//            }
//        }
//    }

    return 0;
}

/*
 * 7：列出版本号列表，要根据DataType字段判定请求的是提交记录还是布署记录
 */
_i
zprint_record(zMeta__ *zpMetaIf, _i zSd) {
    zVecWrap__ *zpSortedTopVecWrapIf;

    if (0 != pthread_rwlock_tryrdlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock))) {
        if (0 == zpGlobRepoIf[zpMetaIf->RepoId]->zWhoGetWrLock) {
            sprintf(zpMetaIf->p_data, "系统正在刷新缓存，请 2 秒后重试");
        } else {
            sprintf(zpMetaIf->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                    (0 == zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit) ? 2.0 : zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit / 60.0);
        }

        return -11;
    };

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpSortedTopVecWrapIf = &(zpGlobRepoIf[zpMetaIf->RepoId]->SortedCommitVecWrapIf);
        /*
         * 如果该项目被标记为被动拉取模式（相对的是主动推送模式），则：
         *     若距离最近一次 “git pull“ 的时间间隔超过 10 秒，尝试拉取远程代码
         *     放在取得读写锁之后执行，防止与布署过程中的同类运作冲突
         *     取到锁，则拉取；否则跳过此步，直接打印列表
         *     打印布署记录时不需要执行
         */
        if (10 < (time(NULL) - zpGlobRepoIf[zpMetaIf->RepoId]->LastPullTime)) {
            if ((0 == zpGlobRepoIf[zpMetaIf->RepoId]->SelfPushMark)
                    && (0 == pthread_mutex_trylock( &(zpGlobRepoIf[zpMetaIf->RepoId]->PullLock) ))) {

                system(zpGlobRepoIf[zpMetaIf->RepoId]->p_PullCmd);  /* 不能多线程，因为多个 git pull 会产生文件锁竞争 */
                zpGlobRepoIf[zpMetaIf->RepoId]->LastPullTime = time(NULL); /* 以取完远程代码的时间重新赋值 */

                zGitRevWalk__ *zpRevWalker;
                char zCommonBuf[64] = {'\0'};
                sprintf(zCommonBuf, "refs/heads/server%d", zpMetaIf->RepoId);
                if (NULL != (zpRevWalker = zgit_generate_revwalker(zpGlobRepoIf[zpMetaIf->RepoId]->p_GitRepoHandler, zCommonBuf, 0))) {
                    zgit_get_one_commitsig_and_timestamp(zCommonBuf, zpGlobRepoIf[zpMetaIf->RepoId]->p_GitRepoHandler, zpRevWalker);
                    zgit_destroy_revwalker(zpRevWalker);
                }
                pthread_mutex_unlock( &(zpGlobRepoIf[zpMetaIf->RepoId]->PullLock) );

                if ((NULL == zpGlobRepoIf[zpMetaIf->RepoId]->CommitRefDataIf[0].p_data)
                        || (0 != strncmp(zCommonBuf, zpGlobRepoIf[zpMetaIf->RepoId]->CommitRefDataIf[0].p_data, 40))) {
                    zpMetaIf->DataType = zIsCommitDataType;

                    /* 此处进行换锁：读锁与写锁进行两次互换 */
                    pthread_rwlock_unlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock));
                    if (0 != pthread_rwlock_trywrlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock))) {
                        if (0 == zpGlobRepoIf[zpMetaIf->RepoId]->zWhoGetWrLock) {
                            sprintf(zpMetaIf->p_data, "系统正在刷新缓存，请 2 秒后重试");
                        } else {
                            sprintf(zpMetaIf->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                                    (0 == zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit) ? 2.0 : zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit / 60.0);
                        }

                        return -11;
                    };

                    zrefresh_cache(zpMetaIf);

                    pthread_rwlock_unlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock));
                    if (0 != pthread_rwlock_tryrdlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock))) {
                        if (0 == zpGlobRepoIf[zpMetaIf->RepoId]->zWhoGetWrLock) {
                            sprintf(zpMetaIf->p_data, "系统正在刷新缓存，请 2 秒后重试");
                        } else {
                            sprintf(zpMetaIf->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                                    (0 == zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit) ? 2.0 : zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit / 60.0);
                        }

                        return -11;
                    };
                }
            }
        }
    } else if (zIsDpDataType == zpMetaIf->DataType) {
        zpSortedTopVecWrapIf = &(zpGlobRepoIf[zpMetaIf->RepoId]->SortedDpVecWrapIf);
    } else {
        pthread_rwlock_unlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock));
        return -10;
    }

    /* 版本号级别的数据使用队列管理，容量固定，最大为 IOV_MAX */
    if (0 < zpSortedTopVecWrapIf->VecSiz) {
        if (0 < zsendmsg(zSd, zpSortedTopVecWrapIf, 0, NULL)) {
            zsendto(zSd, "]", zBytes(1), 0, NULL);  // 二维json结束符
        } else {
            pthread_rwlock_unlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock));
            return -70;
        }
    }

    pthread_rwlock_unlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock));
    return 0;
}

/*
 * 10：显示差异文件路径列表
 */
_i
zprint_diff_files(zMeta__ *zpMetaIf, _i zSd) {
    zVecWrap__ *zpTopVecWrapIf, zSendVecWrapIf;
    _i zSplitCnt;

    /* 若上一次布署是部分失败的，返回 -13 错误 */
    if (zRepoDamaged == zpGlobRepoIf[zpMetaIf->RepoId]->RepoState) {
        zpMetaIf->p_data = "====上一次布署失败，请重试布署====";
        return -13;
    }

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf= &(zpGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf);
        zpMetaIf->DataType = zIsCommitDataType;
    } else if (zIsDpDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zpGlobRepoIf[zpMetaIf->RepoId]->DpVecWrapIf);
        zpMetaIf->DataType = zIsDpDataType;
    } else {
        return -10;
    }

    /* get rdlock */
    if (0 != pthread_rwlock_tryrdlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock))) {
        if (0 == zpGlobRepoIf[zpMetaIf->RepoId]->zWhoGetWrLock) {
            sprintf(zpMetaIf->p_data, "系统正在刷新缓存，请 2 秒后重试");
        } else {
            sprintf(zpMetaIf->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                    (0 == zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit) ? 2.0 : zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit / 60.0);
        }

        return -11;
    }

    zCheck_CacheId();  // 宏内部会解锁

    zCheck_CommitId();  // 宏内部会解锁
    if (NULL == zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)) {
        if ((void *) -1 == zget_file_list(zpMetaIf)) {
            pthread_rwlock_unlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock));
            zpMetaIf->p_data = "==== 无差异 ====";
            return -71;
        }
    } else {
        /* 检测缓存是否正在生成过程中 */
        if (-7 == zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->VecSiz) {
            pthread_rwlock_unlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock));

            if (0 == zpGlobRepoIf[zpMetaIf->RepoId]->zWhoGetWrLock) {
                sprintf(zpMetaIf->p_data, "系统正在刷新缓存，请 2 秒后重试");
            } else {
                sprintf(zpMetaIf->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                        (0 == zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit) ? 2.0 : zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit / 60.0);
            }

            return -11;
        }
    }

    zSendVecWrapIf.VecSiz = 0;
    zSendVecWrapIf.p_VecIf = zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf;
    zSplitCnt = (zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->VecSiz - 1) / zSendUnitSiz  + 1;
    for (_i zCnter = zSplitCnt; zCnter > 0; zCnter--) {
        if (1 == zCnter) {
            zSendVecWrapIf.VecSiz = (zpTopVecWrapIf->p_RefDataIf[zpMetaIf->CommitId].p_SubVecWrapIf->VecSiz - 1) % zSendUnitSiz + 1;
        } else {
            zSendVecWrapIf.VecSiz = zSendUnitSiz;
        }

        zsendmsg(zSd, &zSendVecWrapIf, 0, NULL);
        zSendVecWrapIf.p_VecIf += zSendVecWrapIf.VecSiz;
    }
    zsendto(zSd, "]", zBytes(1), 0, NULL);  // 前端 PHP 需要的二级json结束符

    pthread_rwlock_unlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock));
    return 0;
}

/*
 * 11：显示差异文件内容
 */
_i
zprint_diff_content(zMeta__ *zpMetaIf, _i zSd) {
    zVecWrap__ *zpTopVecWrapIf, zSendVecWrapIf;
    _i zSplitCnt;

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf= &(zpGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf);
        zpMetaIf->DataType = zIsCommitDataType;
    } else if (zIsDpDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf= &(zpGlobRepoIf[zpMetaIf->RepoId]->DpVecWrapIf);
        zpMetaIf->DataType = zIsDpDataType;
    } else {
        return -10;
    }

    if (0 != pthread_rwlock_tryrdlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock))) {
        if (0 == zpGlobRepoIf[zpMetaIf->RepoId]->zWhoGetWrLock) {
            sprintf(zpMetaIf->p_data, "系统正在刷新缓存，请 2 秒后重试");
        } else {
            sprintf(zpMetaIf->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                    (0 == zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit) ? 2.0 : zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit / 60.0);
        }

        return -11;
    };

    zCheck_CacheId();  // 宏内部会解锁

    zCheck_CommitId();  // 宏内部会解锁
    if (NULL == zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)) {
        if ((void *) -1 == zget_file_list(zpMetaIf)) {
            pthread_rwlock_unlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock));
            zpMetaIf->p_data = "==== 无差异 ====";
            return -71;
        }
    } else {
        /* 检测缓存是否正在生成过程中 */
        if (-7 == zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->VecSiz) {
            pthread_rwlock_unlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock));

            if (0 == zpGlobRepoIf[zpMetaIf->RepoId]->zWhoGetWrLock) {
                sprintf(zpMetaIf->p_data, "系统正在刷新缓存，请 2 秒后重试");
            } else {
                sprintf(zpMetaIf->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                        (0 == zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit) ? 2.0 : zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit / 60.0);
            }

            return -11;
        }
    }

    zCheck_FileId();  // 宏内部会解锁
    if (NULL == zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)) {
        if ((void *) -1 == zget_diff_content(zpMetaIf)) {
            pthread_rwlock_unlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock));
            return -72;
        }
    } else {
        /* 检测缓存是否正在生成过程中 */
        if (-7 == zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->VecSiz) {
            pthread_rwlock_unlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock));

            if (0 == zpGlobRepoIf[zpMetaIf->RepoId]->zWhoGetWrLock) {
                sprintf(zpMetaIf->p_data, "系统正在刷新缓存，请 2 秒后重试");
            } else {
                sprintf(zpMetaIf->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                        (0 == zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit) ? 2.0 : zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit / 60.0);
            }

            return -11;
        }
    }

    zSendVecWrapIf.VecSiz = 0;
    zSendVecWrapIf.p_VecIf = zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->p_VecIf;
    zSplitCnt = (zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->VecSiz - 1) / zSendUnitSiz  + 1;
    for (_i zCnter = zSplitCnt; zCnter > 0; zCnter--) {
        if (1 == zCnter) {
            zSendVecWrapIf.VecSiz = (zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->VecSiz - 1) % zSendUnitSiz + 1;
        }
        else {
            zSendVecWrapIf.VecSiz = zSendUnitSiz;
        }

        /* 差异文件内容直接是文本格式 */
        zsendmsg(zSd, &zSendVecWrapIf, 0, NULL);
        zSendVecWrapIf.p_VecIf += zSendVecWrapIf.VecSiz;
    }

    pthread_rwlock_unlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock));
    return 0;
}

/*
 * 注：完全内嵌于 zdeploy() 中，不再需要读写锁
 */
#define zConfig_Dp_Host_Ssh_Cmd(zpCmdBuf) do {\
    sprintf(zpCmdBuf,\
            "rm -f %s %s_SHADOW;"\
            "mkdir -p %s %s_SHADOW;"\
            "rm -f %s/.git/index.lock %s_SHADOW/.git/index.lock;"\
            "cd %s_SHADOW && rm -f .git/hooks/post-update; git init . && git config user.name _ && git config user.email _;"\
            "cd %s && git init . && git config user.name _ && git config user.email _;"\
            "echo ${____zSelfIp} >/home/git/.____zself_ip_addr_%d.txt;"\
\
            "exec 777<>/dev/tcp/%s/%s;"\
            "printf \"{\\\"OpsId\\\":14,\\\"ProjId\\\":%d,\\\"data\\\":%s_SHADOW/tools/post-update}\" >&777;"\
            "rm -f .git/hooks/post-update;"\
            "cat <&777 >.git/hooks/post-update;"\
            "chmod 0755 .git/hooks/post-update;"\
            "exec 777>&-;"\
            "exec 777<&-;",\
\
            zpGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath + 9, zpGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath + 9,\
            zpGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath + 9, zpGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath + 9,\
            zpGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath + 9, zpGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath + 9,\
            zpGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath + 9,\
            zpGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath + 9,\
            zpMetaIf->RepoId,\
\
            zNetServIf.p_IpAddr, zNetServIf.p_port,\
            zpMetaIf->RepoId, zpGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath\
            );\
} while(0)

_i
zupdate_ip_db_all(zMeta__ *zpMetaIf, char *zpCommonBuf, zRegRes__ **zppRegResIfOUT) {
    zDpRes__ *zpOldDpResListIf, *zpTmpDpResIf, *zpOldDpResHashIf[zDpHashSiz];

    zRegInit__ zRegInitIf[1];
    zRegRes__ *zpRegResIf, zRegResIf[1] = {{.RepoId = zpMetaIf->RepoId}};  // 使用项目内存池
    zpRegResIf = zRegResIf;

    zreg_compile(zRegInitIf , "([0-9]{1,3}\\.){3}[0-9]{1,3}");
    zreg_match(zRegResIf, zRegInitIf, zpMetaIf->p_data);
    zReg_Free_Metasource(zRegInitIf);
    *zppRegResIfOUT = zpRegResIf;

    if (strtol(zpMetaIf->p_ExtraData, NULL, 10) != zRegResIf->cnt) { return -28; }

    if (zForecastedHostNum < zRegResIf->cnt) {
        /* 若指定的目标主机数量大于预测的主机数量，则另行分配内存 */
        /* 加空格最长16字节，如："123.123.123.123 " */
        zpGlobRepoIf[zpMetaIf->RepoId]->p_DpCcurIf = zalloc_cache(zpMetaIf->RepoId, zRegResIf->cnt * sizeof(zDpCcur__));
    } else {
        zpGlobRepoIf[zpMetaIf->RepoId]->p_DpCcurIf = zpGlobRepoIf[zpMetaIf->RepoId]->DpCcurIf;
    }

    /* 暂留旧数据 */
    zpOldDpResListIf = zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf;
    memcpy(zpOldDpResHashIf, zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResHashIf, zDpHashSiz * sizeof(zDpRes__ *));

    /*
     * 下次更新时要用到旧的 HASH 进行对比查询，因此不能在项目内存池中分配
     * 分配清零的空间，用于重置状态及检查重复 IP
     */
    zMem_C_Alloc(zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf, zDpRes__, zRegResIf->cnt);

    /* 重置各项状态 */
    zpGlobRepoIf[zpMetaIf->RepoId]->TotalHost = zRegResIf->cnt;
    zpGlobRepoIf[zpMetaIf->RepoId]->DpTotalTask = zpGlobRepoIf[zpMetaIf->RepoId]->TotalHost;
    //zpGlobRepoIf[zpMetaIf->RepoId]->DpReplyCnt = 0;
    zpGlobRepoIf[zpMetaIf->RepoId]->DpTaskFinCnt = 0;
    zpGlobRepoIf[zpMetaIf->RepoId]->ResType[0] = 0;
    zpGlobRepoIf[zpMetaIf->RepoId]->DpBaseTimeStamp = time(NULL);
    memset(zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResHashIf, 0, zDpHashSiz * sizeof(zDpRes__ *));  /* Clear hash buf before reuse it!!! */
    for (_ui zCnter = 0; zCnter < zpGlobRepoIf[zpMetaIf->RepoId]->TotalHost; zCnter++) {
        zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].InitState = 0;
    }

    /* 生成 SSH 动作内容，缓存区使用上层调用者传入的静态内存区 */
    zConfig_Dp_Host_Ssh_Cmd(zpCommonBuf);

    for (_ui zCnter = 0; zCnter < zRegResIf->cnt; zCnter++) {
        /* 检测是否存在重复IP */
        if (0 != zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].ClientAddr) {
            strcpy(zpMetaIf->p_data, zRegResIf->p_rets[zCnter]);
            zpMetaIf->p_ExtraData[0] = '\0';
            return -19;
        }

        /* 注：需要全量赋值，因为后续的布署会直接复用；否则会造成只布署新加入的主机及内存访问错误 */
        zpGlobRepoIf[zpMetaIf->RepoId]->p_DpCcurIf[zCnter].zpThreadSourceIf = NULL;
        zpGlobRepoIf[zpMetaIf->RepoId]->p_DpCcurIf[zCnter].RepoId = zpMetaIf->RepoId;
        zpGlobRepoIf[zpMetaIf->RepoId]->p_DpCcurIf[zCnter].p_HostIpStrAddr = zRegResIf->p_rets[zCnter];
        zpGlobRepoIf[zpMetaIf->RepoId]->p_DpCcurIf[zCnter].p_Cmd = zpCommonBuf;
        zpGlobRepoIf[zpMetaIf->RepoId]->p_DpCcurIf[zCnter].p_CcurLock = &zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncLock;
        zpGlobRepoIf[zpMetaIf->RepoId]->p_DpCcurIf[zCnter].p_CcurCond = &zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncCond;
        zpGlobRepoIf[zpMetaIf->RepoId]->p_DpCcurIf[zCnter].p_TaskCnt = &zpGlobRepoIf[zpMetaIf->RepoId]->DpTaskFinCnt;

        /* 线性链表斌值；转换字符串点分格式 IPv4 为 _ui 型 */
        zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].ClientAddr = zconvert_ip_str_to_bin(zRegResIf->p_rets[zCnter]);
        zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].InitState = 0;
        zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].p_next = NULL;

        /* 更新HASH */
        zpTmpDpResIf = zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResHashIf[(zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].ClientAddr) % zDpHashSiz];
        if (NULL == zpTmpDpResIf) {  /* 若顶层为空，直接指向数组中对应的位置 */
            zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResHashIf[(zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].ClientAddr) % zDpHashSiz]
                = &(zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter]);
        } else {
            while (NULL != zpTmpDpResIf->p_next) { zpTmpDpResIf = zpTmpDpResIf->p_next; }
            zpTmpDpResIf->p_next = &(zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter]);
        }

        zpTmpDpResIf = zpOldDpResHashIf[zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].ClientAddr % zDpHashSiz];
        while (NULL != zpTmpDpResIf) {
            /* 若 IPv4 address 已存在，则跳过初始化远程主机的环节 */
            if (zpTmpDpResIf->ClientAddr == zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].ClientAddr) {
                /* 先前已被初始化过的主机，状态置 1，防止后续收集结果时误报失败 */
                zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].InitState = 1;
                /* 从总任务数中去除已经初始化的主机数 */
                zpGlobRepoIf[zpMetaIf->RepoId]->DpTotalTask--;
                goto zExistMark;
            }
            zpTmpDpResIf = zpTmpDpResIf->p_next;
        }

        /* 对新加入的目标机执行初始化动作 */
        zAdd_To_Thread_Pool(zssh_ccur_simple_init_host, &(zpGlobRepoIf[zpMetaIf->RepoId]->p_DpCcurIf[zCnter]));
zExistMark:;
    }

    /* 释放资源 */
    if (NULL != zpOldDpResListIf) { free(zpOldDpResListIf); }

    /* 等待所有 SSH 任务完成 */
    pthread_mutex_lock(&zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncLock);
    while (zpGlobRepoIf[zpMetaIf->RepoId]->DpTaskFinCnt < zpGlobRepoIf[zpMetaIf->RepoId]->DpTotalTask) {
        pthread_cond_wait(&zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncCond, &zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncLock);
    }
    pthread_mutex_unlock(&zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncLock);

    /* 检测执行结果，并返回失败列表 */
    if ((-1 == zpGlobRepoIf[zpMetaIf->RepoId]->ResType[0])
            || (zpGlobRepoIf[zpMetaIf->RepoId]->DpTaskFinCnt < zpGlobRepoIf[zpMetaIf->RepoId]->DpTotalTask)) {
        char zIpStrAddrBuf[INET_ADDRSTRLEN];
        _ui zFailHostCnt = 0;
        _i zOffSet = sprintf(zpMetaIf->p_data, "无法连接的主机:");
        for (_ui zCnter = 0; (zOffSet < zpMetaIf->DataLen) && (zCnter < zpGlobRepoIf[zpMetaIf->RepoId]->TotalHost); zCnter++) {
            if (1 != zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].InitState) {
                zconvert_ip_bin_to_str(zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].ClientAddr, zIpStrAddrBuf);
                zOffSet += sprintf(zpMetaIf->p_data + zOffSet, " %s", zIpStrAddrBuf);
                zFailHostCnt++;

                /* 未返回成功状态的主机IP清零，以备下次重新初始化，必须在取完对应的失败IP之后执行 */
                zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].ClientAddr = 0;
            }
        }

        /* 主机数超过 10 台，且失败率低于 10% 返回成功，否则返回失败 */
        if ((10 < zpGlobRepoIf[zpMetaIf->RepoId]->TotalHost) && ( zFailHostCnt < zpGlobRepoIf[zpMetaIf->RepoId]->TotalHost / 10)) { return 0; }
        else { return -23; }
    }

    return 0;
}

#define zNative_Fail_Confirm() do {\
    _ui ____zHostId = zconvert_ip_str_to_bin(zpDpCcurIf->p_HostIpStrAddr);\
    zDpRes__ *____zpTmpIf = zpGlobRepoIf[zpDpCcurIf->RepoId]->p_DpResHashIf[____zHostId % zDpHashSiz];\
    for (; NULL != ____zpTmpIf; ____zpTmpIf = ____zpTmpIf->p_next) {\
        if (____zHostId == ____zpTmpIf->ClientAddr) {\
            pthread_mutex_lock(&(zpGlobRepoIf[zpDpCcurIf->RepoId]->DpSyncLock));\
            ____zpTmpIf->DpState = -1;\
            zpGlobRepoIf[zpDpCcurIf->RepoId]->DpReplyCnt = zpGlobRepoIf[zpDpCcurIf->RepoId]->DpTotalTask;  /* 发生错误，计数打满，用于通知结束布署等待状态 */\
            zpGlobRepoIf[zpDpCcurIf->RepoId]->ResType[1] = -1;\
            pthread_cond_signal(zpGlobRepoIf[zpDpCcurIf->RepoId]->p_DpCcurIf->p_CcurCond);\
            pthread_mutex_unlock(&(zpGlobRepoIf[zpDpCcurIf->RepoId]->DpSyncLock));\
            break;\
        }\
    }\
} while(0)

static void *
zgit_push_ccur(void *zpIf) {
    zDpCcur__ *zpDpCcurIf = (zDpCcur__ *) zpIf;

    char zRemoteRepoAddrBuf[64 + zpGlobRepoIf[zpDpCcurIf->RepoId]->RepoPathLen];
    char zGitRefsBuf[2][64 + 2 * sizeof("refs/heads/:")], *zpGitRefs[2];
    zpGitRefs[0] = zGitRefsBuf[0];
    zpGitRefs[1] = zGitRefsBuf[1];

    /* git push 流量控制 */
    zCheck_Negative_Exit( sem_wait(&(zpGlobRepoIf[zpDpCcurIf->RepoId]->DpTraficControl)) );

    /* when memory load > 80%，waiting ... */
    pthread_mutex_lock(&zGlobCommonLock);
    while (80 < zGlobMemLoad) {
        pthread_cond_wait(&zSysLoadCond, &zGlobCommonLock);
    }
    pthread_mutex_unlock(&zGlobCommonLock);

    /* generate remote URL */
    sprintf(zRemoteRepoAddrBuf, "ssh://git@%s/%s/.git", zpDpCcurIf->p_HostIpStrAddr, zpGlobRepoIf[zpDpCcurIf->RepoId]->p_RepoPath + 9);

    /* {'+' == git push --force} push TWO branchs together */
    sprintf(zpGitRefs[0], "+refs/heads/master:refs/heads/server%d", zpDpCcurIf->RepoId);
    sprintf(zpGitRefs[1], "+refs/heads/master_SHADOW:refs/heads/server%d_SHADOW", zpDpCcurIf->RepoId);
    if (0 != zgit_push(zpGlobRepoIf[zpDpCcurIf->RepoId]->p_GitRepoHandler, zRemoteRepoAddrBuf, zpGitRefs, 2)) {
        /* if failed, delete '.git', ReInit the remote host */
        char zCmdBuf[1024 + 7 * zpGlobRepoIf[zpDpCcurIf->RepoId]->RepoPathLen];
        sprintf(zCmdBuf,
                "rm -f %s %s_SHADOW;"  /* if symlink, delete it, or do nothing... */
                "mkdir -p %s %s_SHADOW;"
                "cd %s_SHADOW && rm -rf .git; git init . && git config user.name _ && git config user.email _;"
                "cd %s && rm -rf .git; git init . && git config user.name _ && git config user.email _;"
                "echo '%s' >/home/git/.____zself_ip_addr_%d.txt;"

                "exec 777<>/dev/tcp/%s/%s;"
                "printf \"{\\\"OpsId\\\":14,\\\"ProjId\\\":%d,\\\"data\\\":%s_SHADOW/tools/post-update}\" >&777;"
                "cat <&777 >.git/hooks/post-update;"
                "chmod 0755 .git/hooks/post-update;"
                "exec 777>&-;"
                "exec 777<&-;",

                zpGlobRepoIf[zpDpCcurIf->RepoId]->p_RepoPath + 9, zpGlobRepoIf[zpDpCcurIf->RepoId]->p_RepoPath + 9,
                zpGlobRepoIf[zpDpCcurIf->RepoId]->p_RepoPath + 9, zpGlobRepoIf[zpDpCcurIf->RepoId]->p_RepoPath + 9,
                zpGlobRepoIf[zpDpCcurIf->RepoId]->p_RepoPath + 9,
                zpGlobRepoIf[zpDpCcurIf->RepoId]->p_RepoPath + 9,
                zpDpCcurIf->p_HostIpStrAddr, zpDpCcurIf->RepoId,

                zNetServIf.p_IpAddr, zNetServIf.p_port,
                zpDpCcurIf->RepoId, zpGlobRepoIf[zpDpCcurIf->RepoId]->p_RepoPath
                );
        if (0 == zssh_exec_simple(zpDpCcurIf->p_HostIpStrAddr, zCmdBuf, &(zpGlobRepoIf[zpDpCcurIf->RepoId]->DpSyncLock))) {
            /* if init-ops success, then try deploy once more... */
            if (0 !=zgit_push(zpGlobRepoIf[zpDpCcurIf->RepoId]->p_GitRepoHandler, zRemoteRepoAddrBuf, zpGitRefs, 2)) { zNative_Fail_Confirm(); }
        } else {
            zNative_Fail_Confirm();
        }
    }

    /* git push 流量控制 */
    zCheck_Negative_Exit( sem_post(&(zpGlobRepoIf[zpDpCcurIf->RepoId]->DpTraficControl)) );

    return NULL;
}

/*
 * 实际的布署函数，由外壳函数调用
 * 12：布署／撤销
 */
_i
zdeploy(zMeta__ *zpMetaIf, _i zSd, char **zppCommonBuf, zRegRes__ **zppHostStrAddrRegResIfOUT) {
    zVecWrap__ *zpTopVecWrapIf;
    _i zErrNo = 0;
    time_t zRemoteHostInitTimeSpent;

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf= &(zpGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf);
    } else if (zIsDpDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zpGlobRepoIf[zpMetaIf->RepoId]->DpVecWrapIf);
    } else {
        zpMetaIf->p_data = "====[JSON: DataType] 字段指定的数据类型无效====";
        zpMetaIf->p_ExtraData[0] = '\0';
        zErrNo = -10;
        goto zEndMark;
    }

    /* 检查是否允许布署 */
    if (zDpLocked == zpGlobRepoIf[zpMetaIf->RepoId]->DpLock) {
        zpMetaIf->p_data = "====代码库被锁定，不允许布署====";
        zpMetaIf->p_ExtraData[0] = '\0';
        zErrNo = -6;
        goto zEndMark;
    }

    /* 检查缓存中的CacheId与全局CacheId是否一致 */
    if (zpGlobRepoIf[zpMetaIf->RepoId]->CacheId != zpMetaIf->CacheId) {
        zpMetaIf->p_data = "====已产生新的布署记录，请刷新页面====";
        zpMetaIf->p_ExtraData[0] = '\0';
        zpMetaIf->CacheId = zpGlobRepoIf[zpMetaIf->RepoId]->CacheId;
        zErrNo = -8;
        goto zEndMark;
    }
    /* 检查指定的版本号是否有效 */
    if ((0 > zpMetaIf->CommitId)
            || ((zCacheSiz - 1) < zpMetaIf->CommitId)
            || (NULL == zpTopVecWrapIf->p_RefDataIf[zpMetaIf->CommitId].p_data)) {
        zpMetaIf->p_data = "====指定的版本号无效====";
        zpMetaIf->p_ExtraData[0] = '\0';
        zErrNo = -3;
        goto zEndMark;
    }

    /* 预布署动作：须置于 zupdate_ip_db_all(...) 函数之前，因 post-update 会在初始化远程主机时被首先传输 */
    sprintf(zppCommonBuf[1],
            "cd %s; if [[ 0 -ne $? ]]; then exit 1; fi;"\
            "git stash;"\
            "git stash clear;"\
            "\\ls -a | grep -Ev '^(\\.|\\.\\.|\\.git)$' | xargs rm -rf;"\
            "git reset %s; if [[ 0 -ne $? ]]; then exit 1; fi;"\
            \
            "cd %s_SHADOW; if [[ 0 -ne $? ]]; then exit 1; fi;"\
            "rm -rf ./tools;"\
            "cp -R ${zGitShadowPath}/tools ./;"\
            "chmod 0755 ./tools/post-update;"\
            "eval sed -i 's@__PROJ_PATH@%s@g' ./tools/post-update;"\
            "git add --all .;"\
            "git commit --allow-empty -m _;"\
            "git push --force %s/.git master:master_SHADOW",
            zpGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath,  // 中控机上的代码库路径
            zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId),  // SHA1 commit sig
            zpGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath,
            zpGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath + 9,  // 目标机上的代码库路径(即：去掉最前面的 "/home/git" 合计 9 个字符)
            zpGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath
            );

    /* 调用 git 命令执行布署前的环境准备；同时用于测算中控机本机所有动作耗时，用作布署超时基数 */
    zpGlobRepoIf[zpMetaIf->RepoId]->DpBaseTimeStamp = time(NULL);
    if (0 != WEXITSTATUS( system(zppCommonBuf[1]) )) {
        zErrNo = -15;
        goto zEndMark;
    }

    /* 检查布署目标 IPv4 地址库存在性及是否需要在布署之前更新 */
    if ('_' != zpMetaIf->p_data[0]) {
        if (0 > (zErrNo = zupdate_ip_db_all(zpMetaIf, zppCommonBuf[0], zppHostStrAddrRegResIfOUT))) {
            goto zEndMark;
        }
        zRemoteHostInitTimeSpent = time(NULL) - zpGlobRepoIf[zpMetaIf->RepoId]->DpBaseTimeStamp;
    }

    /* 检查部署目标主机集合是否存在 */
    if (0 == zpGlobRepoIf[zpMetaIf->RepoId]->TotalHost) {
        zpMetaIf->p_data = "====指定的目标主机 IP 列表无效====";
        zpMetaIf->p_ExtraData[0] = '\0';
        zErrNo = -26;
        goto zEndMark;
    }

    /* 正在布署的版本号，用于布署耗时分析及目标机状态回复计数；另复制一份供失败重试之用 */
    strncpy(zpGlobRepoIf[zpMetaIf->RepoId]->zDpingSig, zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId), zBytes(40));
    strncpy(zpMetaIf->p_ExtraData, zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId), zBytes(40));

    /* 重置布署相关状态 */
    for (_ui zCnter = 0; zCnter < zpGlobRepoIf[zpMetaIf->RepoId]->TotalHost; zCnter++) {
        zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].DpState = 0;
    }
    zpGlobRepoIf[zpMetaIf->RepoId]->DpTotalTask = zpGlobRepoIf[zpMetaIf->RepoId]->TotalHost;
    zpGlobRepoIf[zpMetaIf->RepoId]->DpReplyCnt = 0;
    zpGlobRepoIf[zpMetaIf->RepoId]->ResType[1] = 0;
    //zpGlobRepoIf[zpMetaIf->RepoId]->DpTaskFinCnt = 0;
    zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit = 0;

    /* 基于 libgit2 实现 zgit_push(...) 函数，在系统负载上限之下并发布署；参数与之前的SSH动作完全相同，此处无需再次赋值 */
    for (_ui zCnter = 0; zCnter < zpGlobRepoIf[zpMetaIf->RepoId]->TotalHost; zCnter++) {
        zAdd_To_Thread_Pool(zgit_push_ccur, &(zpGlobRepoIf[zpMetaIf->RepoId]->p_DpCcurIf[zCnter]));
    }

    /* 测算超时时间 */
    if (('\0' == zpGlobRepoIf[zpMetaIf->RepoId]->zLastDpSig[0])
            || (0 == strcmp(zpGlobRepoIf[zpMetaIf->RepoId]->zLastDpSig, zpGlobRepoIf[zpMetaIf->RepoId]->zDpingSig))) {
        /* 无法测算时: 默认超时时间 ==  60s + 中控机本地所有动作耗时 */
        zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit = 60
            + ((5 > zRemoteHostInitTimeSpent) ? (5 * (1 + zpGlobRepoIf[zpMetaIf->RepoId]->TotalHost / zDpTraficLimit)) : zRemoteHostInitTimeSpent)
            + (time(NULL) - zpGlobRepoIf[zpMetaIf->RepoId]->DpBaseTimeStamp);
    } else {
        /*
         * [基数 = 30s + 中控机本地所有动作耗时之和] + [远程主机初始化时间 + 中控机与目标机上计算SHA1 checksum 的时间] + [网络数据总量每增加 ?M，超时上限递增 1 秒]
         * [网络数据总量 == 主机数 X 每台的数据量]
         * [单位：秒]
         */
        zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit = 30
            + zpGlobRepoIf[zpMetaIf->RepoId]->TotalHost / 10  /* 临时算式：每增加一台目标机，递增 0.1 秒 */
            + ((5 > zRemoteHostInitTimeSpent) ? (5 * (1 + zpGlobRepoIf[zpMetaIf->RepoId]->TotalHost / zDpTraficLimit)) : zRemoteHostInitTimeSpent)
            + (time(NULL) - zpGlobRepoIf[zpMetaIf->RepoId]->DpBaseTimeStamp);  /* 本地动作耗时 */
    }

    /* 最长 10 分钟 */
    if (600 < zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit) { zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit = 600; }

    /* DEBUG */
    fprintf(stderr, "\n\033[31;01m[ DEBUG ] 布署时间测算结果：%zd 秒\033[00m\n\n", zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit);

    /* 耗时预测超过 90 秒的情况，通知前端不必阻塞等待，可异步于布署列表中查询布署结果 */
    if (90 < zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit) {
        _i zSendLen = sprintf(zppCommonBuf[0], "[{\"OpsId\":-14,\"data\":\"本次布署时间最长可达 %zd 秒，请稍后查看布署结果\"}]", zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit);
        zsendto(zSd, zppCommonBuf[0], zSendLen, 0, NULL);
        shutdown(zSd, SHUT_WR);  // shutdown write peer: avoid frontend from long time waiting ...
    }

    /* 等待所有 git push 任务完成或达到超时时间 */
    struct timespec zAbsoluteTimeStampIf;
    pthread_mutex_lock(&zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncLock);

    if (zpGlobRepoIf[zpMetaIf->RepoId]->DpReplyCnt < zpGlobRepoIf[zpMetaIf->RepoId]->DpTotalTask) {
        zAbsoluteTimeStampIf.tv_sec = zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit + time(NULL) + 1;
        zAbsoluteTimeStampIf.tv_nsec = 0;
        pthread_cond_timedwait(&zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncCond, &zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncLock, &zAbsoluteTimeStampIf);
    }

    /* 若 8 秒内收到过 keepalive 消息，则延长超时时间 15 秒*/
    while (8 > (time(NULL) - zpGlobRepoIf[zpMetaIf->RepoId]->DpKeepAliveStamp)) {
        if (zpGlobRepoIf[zpMetaIf->RepoId]->DpReplyCnt < zpGlobRepoIf[zpMetaIf->RepoId]->DpTotalTask) {
            zAbsoluteTimeStampIf.tv_sec = 15 + time(NULL) + 1;
            zAbsoluteTimeStampIf.tv_nsec = 0;
            pthread_cond_timedwait(&zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncCond, &zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncLock, &zAbsoluteTimeStampIf);
        } else {
            break;
        }
    }

    pthread_mutex_unlock(&zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncLock);

    /* 若收到错误，则可确认此次布署一定会失败，进入错误处理环节 */
    if (-1 == zpGlobRepoIf[zpMetaIf->RepoId]->ResType[1]) { goto zErrMark; }

    if (zpGlobRepoIf[zpMetaIf->RepoId]->TotalHost == zpGlobRepoIf[zpMetaIf->RepoId]->DpReplyCnt) {
        zErrNo = 0;
    } else if ( ((10 <= zpGlobRepoIf[zpMetaIf->RepoId]->TotalHost) && ((zpGlobRepoIf[zpMetaIf->RepoId]->TotalHost * 9 / 10) <= zpGlobRepoIf[zpMetaIf->RepoId]->DpReplyCnt))) {
        /*
         * 对于10 台及以上的目标机集群，达到 90％ 的主机状态得到确认即返回成功，未成功的部分，在下次新的版本布署之前，持续重试布署
         * 10 台以下，则须全部确认
         */
        zErrNo = -10000;
    } else {
zErrMark:
        /* 若为部分布署失败，代码库状态置为 "损坏" 状态；若为全部布署失败，则无需此步 */
        if (0 < zpGlobRepoIf[zpMetaIf->RepoId]->DpReplyCnt) {
            //zpGlobRepoIf[zpMetaIf->RepoId]->zLastDpSig[0] = '\0';
            zpGlobRepoIf[zpMetaIf->RepoId]->RepoState = zRepoDamaged;
        }

        /* 顺序遍历线性列表，获取尚未确认状态的客户端ip列表 */
        char zIpStrAddrBuf[INET_ADDRSTRLEN];
        _i zOffSet = 0;
        for (_ui zCnter = 0; (zOffSet < zpMetaIf->DataLen) && (zCnter < zpGlobRepoIf[zpMetaIf->RepoId]->TotalHost); zCnter++) {
            if (1 != zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].DpState) {
                zconvert_ip_bin_to_str(zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].ClientAddr, zIpStrAddrBuf);
                zOffSet += sprintf(zpMetaIf->p_data + zOffSet, "([%s]%s)",
                        zIpStrAddrBuf,
                        '\0' == zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].ErrMsg[0] ? "" : zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].ErrMsg
                        );

                /* 未返回成功状态的主机IP清零，以备下次重新初始化，必须在取完对应的失败IP之后执行 */
                zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].ClientAddr = 0;
            }
        }
        zpMetaIf->p_ExtraData = zpGlobRepoIf[zpMetaIf->RepoId]->zDpingSig;
        zErrNo = -12;
        goto zEndMark;
    }

    /* 若先前测算的布署耗时 <= 90s ，此处向前端返回布署成功消息 */
    if (90 >= zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit) {
        zsendto(zSd, "[{\"OpsId\":0}]", sizeof("[{\"OpsId\":0}]") - 1, 0, NULL);
        shutdown(zSd, SHUT_WR);  // shutdown write peer: avoid frontend from long time waiting ...
    }
    zpGlobRepoIf[zpMetaIf->RepoId]->RepoState = zRepoGood;

    /* 更新最近一次布署的版本号到项目元信息中，复位代码库状态；若请求布署的版本号与最近一次布署的相同，则不必再重复生成缓存 */
    if (0 != strcmp(zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId), zpGlobRepoIf[zpMetaIf->RepoId]->zLastDpSig)) {
        /* 更新最新一次布署版本号，并将本次布署信息写入日志 */
        strcpy(zpGlobRepoIf[zpMetaIf->RepoId]->zLastDpSig, zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId));

        /* 换行符要写入，但'\0' 不能写入 */
        _i zLogStrLen = sprintf(zppCommonBuf[0], "%s_%zd\n", zpGlobRepoIf[zpMetaIf->RepoId]->zLastDpSig, time(NULL));
        if (zLogStrLen != write(zpGlobRepoIf[zpMetaIf->RepoId]->DpSigLogFd, zppCommonBuf[0], zLogStrLen)) {
            zPrint_Err(0, NULL, "日志写入失败： <_SHADOW/log/deploy/meta> !");
            //exit(1);
        }

        /* deploy success, create a new "CURRENT" branch */
        sprintf(zppCommonBuf[0], "cd %s; git branch -f `git log CURRENT -1 --format=%%H`; git branch -f CURRENT", zpGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath);
        if (0 != WEXITSTATUS( system(zppCommonBuf[0])) ) {
            zPrint_Err(0, NULL, "\"CURRENT\" branch refresh failed");
        }

        /* 若已确认全部成功，重置内存池状态 */
        if (0 == zErrNo) { zReset_Mem_Pool_State(zpMetaIf->RepoId); }

        /* 如下部分：更新全局缓存 */
        zpGlobRepoIf[zpMetaIf->RepoId]->CacheId = time(NULL);

        zMeta__ zSubMetaIf;
        zSubMetaIf.RepoId = zpMetaIf->RepoId;

        zSubMetaIf.DataType = zIsCommitDataType;
        zgenerate_cache(&zSubMetaIf);
        zSubMetaIf.DataType = zIsDpDataType;
        zgenerate_cache(&zSubMetaIf);
    }

zEndMark:
    return zErrNo;
}

/*
 * 外壳函数
 * 13：新加入的主机请求布署自身：不拿锁、不刷系统IP列表、不刷新缓存
 */
_i
zself_deploy(zMeta__ *zpMetaIf, _i zSd __attribute__ ((__unused__))) {
    /* 若目标机上已是最新代码，则无需布署 */
    if (0 != strncmp(zpMetaIf->p_ExtraData, zpGlobRepoIf[zpMetaIf->RepoId]->zLastDpSig, 40)) {
        zDpCcur__ *zpDpSelfIf = zalloc_cache(zpMetaIf->RepoId, sizeof(zDpCcur__));
        zpDpSelfIf->RepoId = zpMetaIf->RepoId;
        zpDpSelfIf->p_HostIpStrAddr = zpMetaIf->p_data;
        zpDpSelfIf->p_CcurLock = NULL;  // 标记无需发送通知给调用者的条件变量
    
        zgit_push_ccur(zpDpSelfIf);
    }

    return 0;
}

/*
 * 外壳函数
 * 12：布署／撤销
 */
_i
zbatch_deploy(zMeta__ *zpMetaIf, _i zSd) {
    /* 系统高负载时，不接受布署请求，保留 20% 的性能提供查询等’读‘操作 */
    if (80 < zGlobMemLoad) {
        zpMetaIf->p_data = "====当前系统负载超过 80%，请稍后重试====";
        return -16;
    }

    if (0 != pthread_rwlock_trywrlock( &(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock) )) {
        if (0 == zpGlobRepoIf[zpMetaIf->RepoId]->zWhoGetWrLock) {
            sprintf(zpMetaIf->p_data, "系统正在刷新缓存，请 2 秒后重试");
        } else {
            sprintf(zpMetaIf->p_data, "正在布署，请 %.2f 分钟后查看布署列表中最新一条记录",
                    (0 == zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit) ? 2.0 : zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit / 60.0);
        }
        return -11;
    }

    _i zErrNo, zCommonBufLen;
    char *zppCommonBuf[2];
    zRegRes__ *zpHostStrAddrRegResIf;

    /* 预算本函数用到的最大 BufSiz，此处是一次性分配两个Buf*/
    zCommonBufLen = 2048 + 10 * zpGlobRepoIf[zpMetaIf->RepoId]->RepoPathLen + zpMetaIf->DataLen;
    zppCommonBuf[0] = zalloc_cache(zpMetaIf->RepoId, 2 * zCommonBufLen);
    zppCommonBuf[1] = zppCommonBuf[0] + zCommonBufLen;

    pthread_mutex_lock(&zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncLock);
    zpGlobRepoIf[zpMetaIf->RepoId]->zWhoGetWrLock = 1;  // 置为 1
    pthread_mutex_unlock(&zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncLock);
    pthread_cond_signal(&zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncCond);  // 通知旧的版本重试动作中止

    pthread_mutex_lock( &(zpGlobRepoIf[zpMetaIf->RepoId]->DpRetryLock) );

    /* 确认全部成功或确认布署失败这两种情况，直接返回，否则进入不间断重试模式，直到新的布署请求到来 */
    if (-10000 != (zErrNo = zdeploy(zpMetaIf, zSd, zppCommonBuf, &zpHostStrAddrRegResIf))) {
        zpGlobRepoIf[zpMetaIf->RepoId]->zWhoGetWrLock = 0;
        pthread_rwlock_unlock( &(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock) );
        pthread_mutex_unlock( &(zpGlobRepoIf[zpMetaIf->RepoId]->DpRetryLock) );
        return zErrNo;
    } else {
        zpGlobRepoIf[zpMetaIf->RepoId]->zWhoGetWrLock = 0;
        pthread_rwlock_unlock( &(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock) );
        pthread_mutex_unlock( &(zpGlobRepoIf[zpMetaIf->RepoId]->DpRetryLock) );

        /* 在没有新的布署动作之前，持续尝试布署失败的目标机 */
        while(1) {
            /* 等待剩余的所有主机状态都得到确认，不必在锁内执行 */
            for (_l zTimeCnter = 0; zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit > zTimeCnter; zTimeCnter++) {
                if ((0 != zpGlobRepoIf[zpMetaIf->RepoId]->zWhoGetWrLock)  /* 检测是否有新的布署请求 */
                        || ((zpGlobRepoIf[zpMetaIf->RepoId]->TotalHost == zpGlobRepoIf[zpMetaIf->RepoId]->DpReplyCnt) && (-1 != zpGlobRepoIf[zpMetaIf->RepoId]->ResType[1]))) {
                    return 0;
                }
                zsleep(0.1);
            }

            pthread_mutex_lock( &(zpGlobRepoIf[zpMetaIf->RepoId]->DpRetryLock) );
            if (0 !=  strncmp(zpGlobRepoIf[zpMetaIf->RepoId]->zDpingSig, zpMetaIf->p_ExtraData, 40)) {
                pthread_mutex_unlock( &(zpGlobRepoIf[zpMetaIf->RepoId]->DpRetryLock) );
                return 0;
            }

            /* 重置时间戳，并生成 SSH 指令 */
            zpGlobRepoIf[zpMetaIf->RepoId]->DpBaseTimeStamp = time(NULL);
            zConfig_Dp_Host_Ssh_Cmd(zppCommonBuf[0]);

            /* 预置值，对失败的目标机重新初始化 */
            zpGlobRepoIf[zpMetaIf->RepoId]->DpTotalTask = zpGlobRepoIf[zpMetaIf->RepoId]->TotalHost;
            zpGlobRepoIf[zpMetaIf->RepoId]->DpTaskFinCnt = 0;

            for (_ui zCnter = 0; zCnter < zpGlobRepoIf[zpMetaIf->RepoId]->TotalHost; zCnter++) {
                /* 检测是否有新的布署请求 */
                if (0 != zpGlobRepoIf[zpMetaIf->RepoId]->zWhoGetWrLock) {
                    pthread_mutex_unlock( &(zpGlobRepoIf[zpMetaIf->RepoId]->DpRetryLock) );
                    return 0;
                }

                if (1 != zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].DpState) {
                    zpGlobRepoIf[zpMetaIf->RepoId]->p_DpCcurIf[zCnter].zpThreadSourceIf = NULL;
                    zpGlobRepoIf[zpMetaIf->RepoId]->p_DpCcurIf[zCnter].RepoId = zpMetaIf->RepoId;
                    zpGlobRepoIf[zpMetaIf->RepoId]->p_DpCcurIf[zCnter].p_HostIpStrAddr = zpHostStrAddrRegResIf->p_rets[zCnter];
                    zpGlobRepoIf[zpMetaIf->RepoId]->p_DpCcurIf[zCnter].p_Cmd = zppCommonBuf[0];
                    zpGlobRepoIf[zpMetaIf->RepoId]->p_DpCcurIf[zCnter].p_CcurLock = &zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncLock;
                    zpGlobRepoIf[zpMetaIf->RepoId]->p_DpCcurIf[zCnter].p_CcurCond = &zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncCond;
                    zpGlobRepoIf[zpMetaIf->RepoId]->p_DpCcurIf[zCnter].p_TaskCnt = &zpGlobRepoIf[zpMetaIf->RepoId]->DpTaskFinCnt;
                    zAdd_To_Thread_Pool(zssh_ccur_simple_init_host, &(zpGlobRepoIf[zpMetaIf->RepoId]->p_DpCcurIf[zCnter]));

                    /* 调整目标机初始化状态数据（布署状态数据不调整！）*/
                    zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResListIf[zCnter].InitState = 0;
                } else {
                    zpGlobRepoIf[zpMetaIf->RepoId]->DpTotalTask -= 1;
                    zpHostStrAddrRegResIf->p_rets[zCnter] = NULL;  // 去掉已成功的 IP 地址，只保留失败的部分
                }
            }

            /* 等待所有 SSH 任务完成，此处不再检查执行结果 */
            pthread_mutex_lock(&zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncLock);
            while ((0 == zpGlobRepoIf[zpMetaIf->RepoId]->zWhoGetWrLock) && (zpGlobRepoIf[zpMetaIf->RepoId]->DpTaskFinCnt < zpGlobRepoIf[zpMetaIf->RepoId]->DpTotalTask)) {
                pthread_cond_wait(&zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncCond, &zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncLock);
            }
            pthread_mutex_unlock(&zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncLock);

            /* 检测是否有新的布署请求 */
            if (0 != zpGlobRepoIf[zpMetaIf->RepoId]->zWhoGetWrLock) {
                for (_ui zCnter = 0; zCnter < zpGlobRepoIf[zpMetaIf->RepoId]->TotalHost; zCnter++) {
                    /* 清理旧的未完工的线程，无需持锁 */
                    if (NULL != zpGlobRepoIf[zpMetaIf->RepoId]->p_DpCcurIf[zCnter].zpThreadSourceIf) {
                        pthread_cancel(zpGlobRepoIf[zpMetaIf->RepoId]->p_DpCcurIf[zCnter].zpThreadSourceIf->SelfTid);
                    }
                }

                pthread_mutex_unlock( &(zpGlobRepoIf[zpMetaIf->RepoId]->DpRetryLock) );
                return 0;
            }

            /* 预置值，对失败的目标机重新布署，任务总量与初始化目标机一致，此处无须再计算 */
            zpGlobRepoIf[zpMetaIf->RepoId]->DpTaskFinCnt = 0;
            zpGlobRepoIf[zpMetaIf->RepoId]->DpBaseTimeStamp = time(NULL);

            /* 在执行动作之前再检查一次布署结果，防止重新初始化的时间里已全部返回成功状态，从而造成无用的布署重试 */
            if (zpGlobRepoIf[zpMetaIf->RepoId]->TotalHost > zpGlobRepoIf[zpMetaIf->RepoId]->DpReplyCnt) {
                /* 对失败的目标主机重试布署 */
                for (_ui zCnter = 0; zCnter < zpHostStrAddrRegResIf->cnt; zCnter++) {
                    /* 检测是否有新的布署请求 */
                    if (0 != zpGlobRepoIf[zpMetaIf->RepoId]->zWhoGetWrLock) {
                        pthread_mutex_unlock( &(zpGlobRepoIf[zpMetaIf->RepoId]->DpRetryLock) );
                        return 0;
                    }

                    /* 结构体各成员参数与目标机初始化时一致，无需修改，直接复用即可 */
                    if (NULL != zpHostStrAddrRegResIf->p_rets[zCnter]) {
                        /* when memory load >= 80%，waiting ... */
                        pthread_mutex_lock(&zGlobCommonLock);
                        while (80 <= zGlobMemLoad) {
                            pthread_cond_wait(&zSysLoadCond, &zGlobCommonLock);
                        }
                        pthread_mutex_unlock(&zGlobCommonLock);
        
                        zAdd_To_Thread_Pool(zgit_push_ccur, &(zpGlobRepoIf[zpMetaIf->RepoId]->p_DpCcurIf[zCnter]));
                    }
                }

                /* 等待所有 git push 任务完成；重试时不必设置超时 */
                pthread_mutex_lock(&zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncLock);
                while ((0 == zpGlobRepoIf[zpMetaIf->RepoId]->zWhoGetWrLock) && (zpGlobRepoIf[zpMetaIf->RepoId]->DpTaskFinCnt < zpGlobRepoIf[zpMetaIf->RepoId]->DpTotalTask)) {
                    pthread_cond_wait(&zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncCond, &zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncLock);
                }
                pthread_mutex_unlock(&zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncLock);

                /* 检测是否有新的布署请求 */
                if (0 != zpGlobRepoIf[zpMetaIf->RepoId]->zWhoGetWrLock) {
                    for (_ui zCnter = 0; zCnter < zpGlobRepoIf[zpMetaIf->RepoId]->TotalHost; zCnter++) {
                        /* 清理旧的未完工的线程，无需持锁 */
                        if (NULL != zpGlobRepoIf[zpMetaIf->RepoId]->p_DpCcurIf[zCnter].zpThreadSourceIf) {
                            pthread_cancel(zpGlobRepoIf[zpMetaIf->RepoId]->p_DpCcurIf[zCnter].zpThreadSourceIf->SelfTid);
                        }
                    }

                    pthread_mutex_unlock( &(zpGlobRepoIf[zpMetaIf->RepoId]->DpRetryLock) );
                    return 0;
                }
            } else {
                pthread_mutex_unlock( &(zpGlobRepoIf[zpMetaIf->RepoId]->DpRetryLock) );
                return 0;
            }

            /* 超时上限延长为 2 倍 */
            zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeWaitLimit *= 2;

            pthread_mutex_unlock( &(zpGlobRepoIf[zpMetaIf->RepoId]->DpRetryLock) );
        }
    }
}

/*
 * 8：布署成功人工确认
 * 9：布署成功主机自动确认
 */
_i
zstate_confirm(zMeta__ *zpMetaIf, _i zSd __attribute__ ((__unused__))) {
    zDpRes__ *zpTmpIf = zpGlobRepoIf[zpMetaIf->RepoId]->p_DpResHashIf[zpMetaIf->HostId % zDpHashSiz];

    for (; zpTmpIf != NULL; zpTmpIf = zpTmpIf->p_next) {  // 遍历
        if (zpTmpIf->ClientAddr == zpMetaIf->HostId) {
            pthread_mutex_lock(&(zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncLock));

            char *zpLogStrId;
            /* 'B' 标识布署状态回复，'C' 目标机的 keep alive 消息 */
            if ('B' == zpMetaIf->p_ExtraData[0]) {
                if (0 != zpTmpIf->DpState) {
                    pthread_mutex_unlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncLock));
                    return 0;
                }

                if (0 != strncmp(zpGlobRepoIf[zpMetaIf->RepoId]->zDpingSig, zpMetaIf->p_data, zBytes(40))) {
                    pthread_mutex_unlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncLock));
                    return -101;  // 返回负数，用于打印日志
                }

                if ('+' == zpMetaIf->p_ExtraData[1]) {  // 负号 '-' 表示是异常返回，正号 '+' 表示是成功返回
                    zpGlobRepoIf[zpMetaIf->RepoId]->DpReplyCnt++;
                    zpTmpIf->DpState = 1;

                    zpLogStrId = zpGlobRepoIf[zpMetaIf->RepoId]->zDpingSig;
                } else if ('-' == zpMetaIf->p_ExtraData[1]) {
                    zpGlobRepoIf[zpMetaIf->RepoId]->DpReplyCnt = zpGlobRepoIf[zpMetaIf->RepoId]->DpTotalTask;  // 发生错误，计数打满，用于通知结束布署等待状态
                    zpTmpIf->DpState = -1;
                    zpGlobRepoIf[zpMetaIf->RepoId]->ResType[1] = -1;

                    snprintf(zpTmpIf->ErrMsg, zErrMsgBufSiz, "%s", zpMetaIf->p_data + 40);  // 所有的状态回复前40个字节均是 git SHA1sig
                    pthread_mutex_unlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncLock));
                    pthread_cond_signal(zpGlobRepoIf[zpMetaIf->RepoId]->p_DpCcurIf->p_CcurCond);
                    return -102;  // 返回负数，用于打印日志
                } else {
                    pthread_mutex_unlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncLock));
                    return -103;  // 未知的返回内容
                }
            } else if ('C' == zpMetaIf->p_ExtraData[0]) {
                zpGlobRepoIf[zpMetaIf->RepoId]->DpKeepAliveStamp = time(NULL);
                pthread_mutex_unlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncLock));
                return 0;
            } else {
                pthread_mutex_unlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncLock));
                return -103;  // 未知的返回内容
            }

            /* 调试功能：布署耗时统计，必须在锁内执行 */
            char zIpStrAddr[INET_ADDRSTRLEN], zTimeCntBuf[128];
            zconvert_ip_bin_to_str(zpMetaIf->HostId, zIpStrAddr);
            _i zWrLen = sprintf(zTimeCntBuf, "[%s] [%s]\t\t[TimeSpent(s): %ld]\n",
                    zpLogStrId,
                    zIpStrAddr,
                    time(NULL) - zpGlobRepoIf[zpMetaIf->RepoId]->DpBaseTimeStamp);
            write(zpGlobRepoIf[zpMetaIf->RepoId]->DpTimeSpentLogFd, zTimeCntBuf, zWrLen);

            pthread_mutex_unlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->DpSyncLock));
            if (zpGlobRepoIf[zpMetaIf->RepoId]->DpReplyCnt == zpGlobRepoIf[zpMetaIf->RepoId]->DpTotalTask) {
                pthread_cond_signal(zpGlobRepoIf[zpMetaIf->RepoId]->p_DpCcurIf->p_CcurCond);
            }
            return 0;
        }
    }

    return 0;
}

/*
 * 2；拒绝(锁定)某个项目的 布署／撤销／更新ip数据库 功能，仅提供查询服务
 * 3：允许布署／撤销／更新ip数据库
 */
_i
zlock_repo(zMeta__ *zpMetaIf, _i zSd) {
    pthread_rwlock_wrlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock));

    if (2 == zpMetaIf->OpsId) {
        zpGlobRepoIf[zpMetaIf->RepoId]->DpLock = zDpLocked;
    } else {
        zpGlobRepoIf[zpMetaIf->RepoId]->DpLock = zDpUnLock;
    }

    pthread_rwlock_unlock(&(zpGlobRepoIf[zpMetaIf->RepoId]->RwLock));

    zsendto(zSd, "[{\"OpsId\":0}]", sizeof("[{\"OpsId\":0}]") - 1, 0, NULL);

    return 0;
}

/* 14: 向目标机传输指定的文件 */
_i
zreq_file(zMeta__ *zpMetaIf, _i zSd) {
    char zSendBuf[4096];
    _i zFd, zDataLen;

    zCheck_Negative_Return(zFd = open(zpMetaIf->p_data, O_RDONLY), -80);
    while (0 < (zDataLen = read(zFd, zSendBuf, 4096))) {
        zsendto(zSd, zSendBuf, zDataLen, 0, NULL);
    }

    close(zFd);
    return 0;
}

/*
 * 网络服务路由函数
 */
void *
zops_route(void *zpParam) {
    _i zSd = ((zSocketAcceptParam__ *) zpParam)->ConnSd;
    _i zErrNo;
    char zJsonBuf[zGlobBufSiz] = {'\0'};
    char *zpJsonBuf = zJsonBuf;

    /* 必须清零，以防脏栈数据导致问题 */
    zMeta__ zMetaIf;
    memset(&zMetaIf, 0, sizeof(zMeta__));

    /* 若收到大体量数据，直接一次性扩展为1024倍的缓冲区，以简化逻辑 */
    if (zGlobBufSiz == (zMetaIf.DataLen = recv(zSd, zpJsonBuf, zGlobBufSiz, 0))) {
        zMem_C_Alloc(zpJsonBuf, char, zGlobBufSiz * 1024);  // 用清零的空间，保障正则匹配不出现混乱
        strcpy(zpJsonBuf, zJsonBuf);
        zMetaIf.DataLen += recv(zSd, zpJsonBuf + zMetaIf.DataLen, zGlobBufSiz * 1024 - zMetaIf.DataLen, 0);
    }

    if (zBytes(6) > zMetaIf.DataLen) {
        close(zSd);
        zPrint_Err(errno, "zBytes(6) > recv(...)", NULL);
        return NULL;
    }

    /* .p_data 与 .p_ExtraData 成员空间 */
    zMetaIf.DataLen += (zMetaIf.DataLen > zGlobBufSiz) ? zMetaIf.DataLen : zGlobBufSiz;
    zMetaIf.ExtraDataLen = zGlobBufSiz;
    char zDataBuf[zMetaIf.DataLen], zExtraDataBuf[zMetaIf.ExtraDataLen];
    memset(zDataBuf, 0, zMetaIf.DataLen);
    memset(zExtraDataBuf, 0, zMetaIf.ExtraDataLen);
    zMetaIf.p_data = zDataBuf;
    zMetaIf.p_ExtraData = zExtraDataBuf;

    if (0 > (zErrNo = zconvert_json_str_to_struct(zpJsonBuf, &zMetaIf))) {
        zMetaIf.OpsId = zErrNo;
        goto zMarkCommonAction;
    }

    if (0 > zMetaIf.OpsId || zServHashSiz <= zMetaIf.OpsId || NULL == zNetServ[zMetaIf.OpsId]) {
        zMetaIf.OpsId = -1;  // 此时代表错误码
        goto zMarkCommonAction;
    }

    if ((1 != zMetaIf.OpsId) && (5 != zMetaIf.OpsId)
            && ((zGlobMaxRepoId < zMetaIf.RepoId) || (0 >= zMetaIf.RepoId) || (NULL == zpGlobRepoIf[zMetaIf.RepoId]))) {
        zMetaIf.OpsId = -2;  // 此时代表错误码
        goto zMarkCommonAction;
    }

    if (0 > (zErrNo = zNetServ[zMetaIf.OpsId](&zMetaIf, zSd))) {
        zMetaIf.OpsId = zErrNo;  // 此时代表错误码
zMarkCommonAction:
        zconvert_struct_to_json_str(zpJsonBuf, &zMetaIf);
        zpJsonBuf[0] = '[';
        zsendto(zSd, zpJsonBuf, strlen(zpJsonBuf), 0, NULL);
        zsendto(zSd, "]", zBytes(1), 0, NULL);

        fprintf(stderr, "\n\033[31;01m[ DEBUG ] \033[00m%s", zpJsonBuf);  // 错误信息，打印出一份，防止客户端socket已关闭时，信息丢失
    }

    close(zSd);
    if (zpJsonBuf != &(zJsonBuf[0])) { free(zpJsonBuf); }
    return NULL;
}

/************
 * 网络服务 *
 ************/
/*  执行结果状态码对应表
 *  -1：操作指令不存在（未知／未定义）
 *  -2：项目ID不存在
 *  -3：代码版本ID不存在或与其相关联的内容为空（空提交记录）
 *  -4：差异文件ID不存在
 *  -5：指定的主机 IP 不存在
 *  -6：项目布署／撤销／更新ip数据库的权限被锁定
 *  -7：后端接收到的数据无法解析，要求前端重发
 *  -8：后端缓存版本已更新（场景：在前端查询与要求执行动作之间，有了新的布署记录）
 *  -9：服务端错误：接收缓冲区为空或容量不足，无法解析数据
 *  -10：前端请求的数据类型错误
 *  -11：正在布署／撤销过程中（请稍后重试？）
 *  -12：布署失败（超时？未全部返回成功状态）
 *  -13：上一次布署／撤销最终结果是失败，当前查询到的内容可能不准确
 *  -14：系统测算的布署耗时超过 90 秒，通知前端不必阻塞等待，可异步于布署列表中查询布署结果
 *  -15：布署前环境初始化失败（中控机）
 *  -16：系统负载太高(CPU 或 内存占用超过 80%)，不允许布署，提示稍后重试
 *
 *  -19：更新目标机IP列表时，存在重复IP
 *  -23：更新目标机IP列表时：部分或全部目标初始化失败
 *  -24：更新目标机IP列表时，没有在 ExtraData 字段指明IP总数量
 *  -26：目标机IP列表为空
 *  -28：前端指定的IP数量与实际解析出的数量不一致
 *  -29：一台或多台目标机环境初化失败(SSH 连接至目标机建立接收项目文件的元信息——git 仓库)
 *
 *  -32：请求创建的项目ID超出系统允许的最大或最小值（创建或载入项目代码库时出错）
 *  -33：无法创建请求的项目路径
 *  -34：请求创建的新项目信息格式错误（合法字段数量少于 5 个或大于 6 个，第6个字段用于标记是被动拉取代码还是主动推送代码）
 *  -35：请求创建的项目ID已存在（创建或载入项目代码库时出错）
 *  -36：请求创建的项目路径已存在，且项目ID不同
 *  -37：请求创建项目时指定的源版本控制系统错误(!git && !svn)
 *  -38：拉取远程代码库失败（git clone 失败）
 *  -39：项目元数据创建失败，如：无法打开或创建布署日志文件meta等原因
 *
 *  -70：服务器版本号列表缓存存在错误
 *  -71：服务器差异文件列表缓存存在错误
 *  -72：服务器单个文件的差异内容缓存存在错误
 *
 *  -80：目标机请求的文件路径不存在或无权访问
 *
 *  -101：目标机返回的版本号与正在布署的不一致
 *  -102：目标机返回的错误信息
 *  -103：目标机返回的状态信息Type无法识别
 *
 *  -10000: fake success
 */

/*
 * 0: 测试函数
 */
// _i
// ztest_func(zMeta__ *zpParam, _i zSd __attribute__ ((__unused__))) { return 0; }


void
zstart_server(void *zpParam) {
    zNetServ[0] = NULL;  // ztest_func;  // 留作功能测试接口
    zNetServ[1] = zadd_repo;  // 添加新代码库
    zNetServ[2] = zlock_repo;  // 锁定某个项目的布署／撤销功能，仅提供查询服务（即只读服务）
    zNetServ[3] = zlock_repo;  // 恢复布署／撤销功能
    zNetServ[4] = NULL;  // 已解决 CentOS-6 平台上 sendmsg 的问题，不再需要 zupdate_ip_db_proxy()
    zNetServ[5] = zshow_all_repo_meta;  // 显示所有有效项目的元信息
    zNetServ[6] = zshow_one_repo_meta;  // 显示单个有效项目的元信息
    zNetServ[7] = NULL;
    zNetServ[8] = zstate_confirm;  // 远程主机初始经状态、布署结果状态、错误信息
    zNetServ[9] = zprint_record;  // 显示CommitSig记录（提交记录或布署记录，在json中以DataType字段区分）
    zNetServ[10] = zprint_diff_files;  // 显示差异文件路径列表
    zNetServ[11] = zprint_diff_content;  // 显示差异文件内容
    zNetServ[12] = zbatch_deploy;  // 布署或撤销
    zNetServ[13] = zself_deploy;  // 用于新加入某个项目的主机每次启动时主动请求中控机向自己承载的所有项目同目最近一次已布署版本代码
    zNetServ[14] = zreq_file;  // 请求服务器传输指定的文件
    zNetServ[15] = NULL;

    /* 如下部分配置网络服务 */
    zNetServ__ *zpNetServIf = (zNetServ__ *)zpParam;
    _i zMajorSd;
    zMajorSd = zgenerate_serv_SD(zpNetServIf->p_IpAddr, zpNetServIf->p_port, zpNetServIf->zServType);  // 返回的 socket 已经做完 bind 和 listen

    /* 会传向新线程，使用静态变量；使用数组防止负载高时造成线程参数混乱 */
    static zSocketAcceptParam__ zSocketAcceptParamIf[64] = {{NULL, 0}};
    for (_ui zCnter = 0;; zCnter++) {
        if (-1 == (zSocketAcceptParamIf[zCnter % 64].ConnSd = accept(zMajorSd, NULL, 0))) {
            zPrint_Err(errno, "-1 == accept(...)", NULL);
        } else {
            zAdd_To_Thread_Pool(zops_route, &(zSocketAcceptParamIf[zCnter % 64]));
        }
    }
}





/*
 * 线程并发函数
 */
static void *
zssh_ccur(void  *zpIf) {
    zDpCcur__ *zpDpCcurIf = (zDpCcur__ *) zpIf;

    zssh_exec(zpDpCcurIf->p_HostIpStrAddr, zpDpCcurIf->p_HostServPort, zpDpCcurIf->p_Cmd,
            zpDpCcurIf->p_UserName, zpDpCcurIf->p_PubKeyPath, zpDpCcurIf->p_PrivateKeyPath, zpDpCcurIf->p_PassWd, zpDpCcurIf->zAuthType,
            zpDpCcurIf->p_RemoteOutPutBuf, zpDpCcurIf->RemoteOutPutBufSiz, zpDpCcurIf->p_CcurLock);

    pthread_mutex_lock(zpDpCcurIf->p_CcurLock);
    (* (zpDpCcurIf->p_TaskCnt))++;
    pthread_mutex_unlock(zpDpCcurIf->p_CcurLock);
    pthread_cond_signal(zpDpCcurIf->p_CcurCond);

    return NULL;
};

/* 简化参数版函数 */
static _i
zssh_exec_simple(char *zpHostIpAddr, char *zpCmd, pthread_mutex_t *zpCcurLock) {
    return zssh_exec(zpHostIpAddr, "22", zpCmd, "git", "/home/git/.ssh/id_rsa.pub", "/home/git/.ssh/id_rsa", NULL, 1, NULL, 0, zpCcurLock);
}

/* 简化参数版函数 */
static void *
zssh_ccur_simple(void  *zpIf) {
    zDpCcur__ *zpDpCcurIf = (zDpCcur__ *) zpIf;

    zssh_exec_simple(zpDpCcurIf->p_HostIpStrAddr, zpDpCcurIf->p_Cmd, zpDpCcurIf->p_CcurLock);

    pthread_mutex_lock(zpDpCcurIf->p_CcurLock);
    (* (zpDpCcurIf->p_TaskCnt))++;
    pthread_mutex_unlock(zpDpCcurIf->p_CcurLock);
    pthread_cond_signal(zpDpCcurIf->p_CcurCond);

    return NULL;
};

/* 远程主机初始化专用 */
static void *
zssh_ccur_simple_init_host(void  *zpIf) {
    zDpCcur__ *zpDpCcurIf = (zDpCcur__ *) zpIf;

    _ui zHostId = zconvert_ip_str_to_bin(zpDpCcurIf->p_HostIpStrAddr);
    zDpRes__ *zpTmpIf = zpGlobRepoIf[zpDpCcurIf->RepoId]->p_DpResHashIf[zHostId % zDpHashSiz];
    for (; NULL != zpTmpIf; zpTmpIf = zpTmpIf->p_next) {
        if (zHostId == zpTmpIf->ClientAddr) {
            if (0 == zssh_exec_simple(zpDpCcurIf->p_HostIpStrAddr, zpDpCcurIf->p_Cmd, zpDpCcurIf->p_CcurLock)) {
                zpTmpIf->InitState = 1;
            } else {
                zpTmpIf->InitState = -1;
                zpGlobRepoIf[zpDpCcurIf->RepoId]->ResType[0] = -1;
            }

            pthread_mutex_lock(zpDpCcurIf->p_CcurLock);
            (* (zpDpCcurIf->p_TaskCnt))++;
            pthread_mutex_unlock(zpDpCcurIf->p_CcurLock);
            pthread_cond_signal(zpDpCcurIf->p_CcurCond);

            break;
        }
    }

    return NULL;
};



/*
 *  接收数据时使用
 *  将json文本转换为zMetaInfo结构体
 *  返回：出错返回-1，正常返回0
 */
_i
zconvert_json_str_to_struct(char *zpJsonStr, struct zMetaInfo *zpMetaIf) {
    zRegInitInfo zRegInitIf[1];
    zRegResInfo zRegResIf[1] = {{.RepoId = -1}};  // 此时尚没取得 zpMetaIf->RepoIf 之值，不可使用项目内存池

    zreg_compile(zRegInitIf, "[^][}{\",:][^][}{\",]*");  // posix 的扩展正则语法中，中括号中匹配'[' 或 ']' 时需要将后一半括号放在第一个位置，而且不能转义
    zreg_match(zRegResIf, zRegInitIf, zpJsonStr);
    zReg_Free_Metasource(zRegInitIf);

    zRegResIf->cnt -= zRegResIf->cnt % 2;  // 若末端有换行、空白之类字符，忽略之

    void *zpBuf[128];
    zpBuf['O'] = &(zpMetaIf->OpsId);
    zpBuf['P'] = &(zpMetaIf->RepoId);
    zpBuf['R'] = &(zpMetaIf->CommitId);
    zpBuf['F'] = &(zpMetaIf->FileId);
    zpBuf['H'] = &(zpMetaIf->HostId);
    zpBuf['C'] = &(zpMetaIf->CacheId);
    zpBuf['D'] = &(zpMetaIf->DataType);
    zpBuf['d'] = zpMetaIf->p_data;
    zpBuf['E'] = zpMetaIf->p_ExtraData;

    for (_ui zCnter = 0; zCnter < zRegResIf->cnt; zCnter += 2) {
        if (NULL == zJsonParseOps[(_i)(zRegResIf->p_rets[zCnter][0])]) {
            strcpy(zpMetaIf->p_data, zpJsonStr);  // 必须复制，不能调整指针，zpJsonStr 缓存区会被上层调用者复用
            zReg_Free_Tmpsource(zRegResIf);
            return -7;
        }
        zJsonParseOps[(_i)(zRegResIf->p_rets[zCnter][0])](zRegResIf->p_rets[zCnter + 1], zpBuf[(_i)(zRegResIf->p_rets[zCnter][0])]);
    }

    zReg_Free_Tmpsource(zRegResIf);
    return 0;
}

/*
 * 生成缓存时使用
 * 将结构体数据转换成生成json文本
 */
void
zconvert_struct_to_json_str(char *zpJsonStrBuf, struct zMetaInfo *zpMetaIf) {
    sprintf(
            zpJsonStrBuf, ",{\"OpsId\":%d,\"CacheId\":%d,\"ProjId\":%d,\"RevId\":%d,\"FileId\":%d,\"DataType\":%d,\"data\":\"%s\",\"ExtraData\":\"%s\"}",
            zpMetaIf->OpsId,
            zpMetaIf->CacheId,
            zpMetaIf->RepoId,
            zpMetaIf->CommitId,
            zpMetaIf->FileId,
            zpMetaIf->DataType,
            (NULL == zpMetaIf->p_data) ? "_" : zpMetaIf->p_data,
            (NULL == zpMetaIf->p_ExtraData) ? "_" : zpMetaIf->p_ExtraData
            );
}





/*
 * json 解析回调：数字与字符串
 */
void
zparse_digit(void *zpIn, void *zpOut) {
    *((_i *)zpOut) = strtol(zpIn, NULL, 10);
}

void
zparse_str(void *zpIn, void *zpOut) {
    strcpy(zpOut, zpIn);  // 正则匹配出的结果，不会为 NULL，因此不必检查 zpIn
}