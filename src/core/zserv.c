#ifndef _Z
    #include "../zmain.c"
#endif

/************
 * META OPS *
 ************/
#define zGet_SendIfPtr(zpUpperVecWrapIf, zSelfId) ((zpUpperVecWrapIf)->p_VecIf[zSelfId].iov_base)
#define zGet_SubVecWrapIfPtr(zpUpperVecWrapIf, zSelfId) ((zpUpperVecWrapIf)->p_RefDataIf[zSelfId].p_SubVecWrapIf)
#define zGet_NativeDataPtr(zpUpperVecWrapIf, zSelfId) ((zpUpperVecWrapIf)->p_RefDataIf[zSelfId].p_data)

#define zGet_OneCommitSendIfPtr(zpTopVecWrapIf, zCommitId) ((char *)zGet_SendIfPtr(zpTopVecWrapIf, zCommitId))
#define zGet_OneCommitSigPtr(zpTopVecWrapIf, zCommitId) zGet_NativeDataPtr(zpTopVecWrapIf, zCommitId)

#define zIsCommitCacheType 0
#define zIsDeployCacheType 1

#define zServHashSiz 14

/* 执行结果状态码对应表
 * -100：动作执行成功 (仅表示动作本身已执行，不代表最终结果，如：布署请求返回 -100 表示中控机已开始布署，但是否布署成功尚不能确认)
 * -1：操作指令不存在（未知／未定义）
 * -2：项目ID不存在
 * -3：代码版本ID不存在
 * -4：差异文件ID不存在
 * -5：指定的主机 IP 不存在
 * -6：项目布署／撤销／更新ip数据库的权限被锁定
 * -7：后端接收到的数据不完整，要求前端重发
 * -8：后端缓存版本已更新（场景：在前端查询与要求执行动作之间，有了新的布署记录）
 */

/**************
 * NATIVE OPS *
 **************/
/*
 * 功能：生成单个文件的差异内容缓存
 */
void
zget_diff_content(void *zpIf) {
#ifdef _zDEBUG
    zCheck_Null_Exit(zpIf);
#endif
    struct zCacheMetaInfo *zpCacheMetaIf;
    struct zVecWrapInfo *zpTopVecWrapIf, *zpUpperVecWrapIf, *zpCurVecWrapIf;

    FILE *zpShellRetHandler;
    char zShellBuf[128], *zpRes;

    struct zSendInfo *zpSendIf;  // 此项是 iovec 的 io_base 字段
    _i zVecId;
    _i zSendDataLen, zVecDataLen;
    _i zAllocSiz = 128;

    zpCacheMetaIf = (struct zCacheMetaInfo *)zpIf;

    if (0 ==zpCacheMetaIf->TopObjTypeMark) {
        zpTopVecWrapIf = &(zpGlobRepoIf[zpCacheMetaIf->RepoId].CommitVecWrapIf);
    } else {
        zpTopVecWrapIf = &(zpGlobRepoIf[zpCacheMetaIf->RepoId].DeployVecWrapIf);
    }

    zpUpperVecWrapIf = zGet_SubVecWrapIfPtr(zpTopVecWrapIf, zpCacheMetaIf->CommitId);

	zMem_Alloc(zpCurVecWrapIf, struct zVecWrapInfo, 1);
    zMem_Alloc(zpCurVecWrapIf->p_VecIf, struct iovec, zAllocSiz);

    /* 必须在shell命令中切换到正确的工作路径 */
    sprintf(zShellBuf, "cd %s && git diff %s CURRENT -- %s", zpGlobRepoIf[zpCacheMetaIf->RepoId].RepoPath,
            zGet_OneCommitSigPtr(zpTopVecWrapIf, zpCacheMetaIf->CommitId),
            (char *)(((struct zSendInfo *)zGet_SendIfPtr(zpUpperVecWrapIf, zpCacheMetaIf->FileId) )->data));

    zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );

    for (zVecId = 0;  NULL != (zpRes = zget_one_line_from_FILE(zpShellRetHandler)); zVecId++) {
        if (zVecId >= zAllocSiz) {
            zAllocSiz *= 2;
            zMem_Re_Alloc(zpCurVecWrapIf->p_VecIf, struct iovec, zAllocSiz, zpCurVecWrapIf->p_VecIf);
        }

        zSendDataLen = 1 + strlen(zpRes);
        zVecDataLen = zSendDataLen + sizeof(struct zSendInfo);
        zCheck_Null_Exit( zpSendIf = malloc(zVecDataLen) );

        zpSendIf->SelfId = zVecId;
        zpSendIf->DataLen = zSendDataLen;
        strcpy((char *)zpSendIf->data, zpRes);

        zpCurVecWrapIf->p_VecIf[zVecId].iov_base = zpSendIf;
        zpCurVecWrapIf->p_VecIf[zVecId].iov_len = zVecDataLen;
    }
    pclose(zpShellRetHandler);

    zpCurVecWrapIf->VecSiz = zVecId;
    if (0 == zpCurVecWrapIf->VecSiz) {
        free(zpCurVecWrapIf->p_VecIf);
        zpCurVecWrapIf->p_VecIf = NULL;
        return;
    } else {
        /* 将分配的空间缩减为最终的实际成员数量 */
        zMem_Re_Alloc(zpCurVecWrapIf->p_VecIf, struct iovec, zpCurVecWrapIf->VecSiz, zpCurVecWrapIf->p_VecIf);
    }

    /* 将自身关联到上一级数据结构 */
    zpUpperVecWrapIf->p_RefDataIf->p_SubVecWrapIf = zpCurVecWrapIf;

    zpCurVecWrapIf->p_RefDataIf = NULL;  /* 因为没有下一级数据，所以置为NULL */

    free(zpIf);  // 上一级传入的结构体空间是在堆上分配的
}

/* 
 * 功能：生成某个 Commit 版本(提交记录与布署记录通用)的文件差异列表与每个文件的差异内容
 */
void
zget_file_list_and_diff_content(void *zpIf) {
#ifdef _zDEBUG
    zCheck_Null_Exit(zpIf);
#endif
    struct zCacheMetaInfo *zpCacheMetaIf, *zpSubCacheMetaIf;
    struct zVecWrapInfo *zpTopVecWrapIf, *zpUpperVecWrapIf, *zpCurVecWrapIf;

    FILE *zpShellRetHandler;
    char zShellBuf[128], *zpRes;

    struct zSendInfo *zpSendIf;  // 此项是 iovec 的 io_base 字段
    _i zVecId;
    _i zSendDataLen, zVecDataLen;
    _i zAllocSiz = 128;

    zpCacheMetaIf = (struct zCacheMetaInfo *)zpIf;

    if (0 ==zpCacheMetaIf->TopObjTypeMark) {
        zpTopVecWrapIf = &(zpGlobRepoIf[zpCacheMetaIf->RepoId].CommitVecWrapIf);
    } else {
        zpTopVecWrapIf = &(zpGlobRepoIf[zpCacheMetaIf->RepoId].DeployVecWrapIf);
    }

    zpUpperVecWrapIf = zpTopVecWrapIf;

	zMem_Alloc(zpCurVecWrapIf, struct zVecWrapInfo, 1);
    zMem_Alloc(zpCurVecWrapIf->p_VecIf, struct iovec, zAllocSiz);

    /* 必须在shell命令中切换到正确的工作路径 */
    sprintf(zShellBuf, "cd %s && git diff --name-only %s CURRENT", zpGlobRepoIf[zpCacheMetaIf->RepoId].RepoPath, zGet_OneCommitSigPtr(zpTopVecWrapIf, zpCacheMetaIf->CommitId));

    zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );

    for (zVecId = 0;  NULL != (zpRes = zget_one_line_from_FILE(zpShellRetHandler)); zVecId++) {
        if (zVecId >= zAllocSiz) {
            zAllocSiz *= 2;
            zMem_Re_Alloc(zpCurVecWrapIf->p_VecIf, struct iovec, zAllocSiz, zpCurVecWrapIf->p_VecIf);
        }

        zSendDataLen = 1 + strlen(zpRes);
        zVecDataLen = zSendDataLen + sizeof(struct zSendInfo);
        zCheck_Null_Exit( zpSendIf = malloc(zVecDataLen) );

        zpSendIf->SelfId = zVecId;
        zpSendIf->DataLen = zSendDataLen;
        strcpy((char *)zpSendIf->data, zpRes);

        zpCurVecWrapIf->p_VecIf[zVecId].iov_base = zpSendIf;
        zpCurVecWrapIf->p_VecIf[zVecId].iov_len = zVecDataLen;
    }
    pclose(zpShellRetHandler);

    zpCurVecWrapIf->VecSiz = zVecId;
    if (0 == zpCurVecWrapIf->VecSiz) {
        /* 用于差异文件数量为0的情况，如：将 CURRENT 与其自身对比，结果将为空 */
        free(zpCurVecWrapIf->p_VecIf);
        zpCurVecWrapIf->p_VecIf = NULL;
        return;
    } else {
        /* 将分配的空间缩减为最终的实际成员数量 */
        zMem_Re_Alloc(zpCurVecWrapIf->p_VecIf, struct iovec, zpCurVecWrapIf->VecSiz, zpCurVecWrapIf->p_VecIf);
    }

    /* 将自身关联到上一级数据结构 */
    zpUpperVecWrapIf->p_RefDataIf->p_SubVecWrapIf = zpCurVecWrapIf;

    /* 如果差异文件数量不为0，则需要分配RefData空间，指向文件对比差异内容VecInfo */
    zMem_Alloc(zpCurVecWrapIf->p_RefDataIf, struct zRefDataInfo, zpCurVecWrapIf->VecSiz);

    /* 进入下一层获取对应的差异内容 */
    for (_i zId = 0; zId < zpCurVecWrapIf->VecSiz; zId++) {
        zMem_Alloc(zpSubCacheMetaIf, struct zCacheMetaInfo, 1);
        zpSubCacheMetaIf->TopObjTypeMark = zpCacheMetaIf->TopObjTypeMark;
        zpSubCacheMetaIf->RepoId = zpCacheMetaIf->RepoId;
        zpSubCacheMetaIf->CommitId = zpCacheMetaIf->CommitId;
        zpSubCacheMetaIf->FileId = zId;

		zget_diff_content(zpSubCacheMetaIf);
    //    zAdd_To_Thread_Pool(zget_diff_content, zpSubCacheMetaIf);
    }

    free(zpIf);  // 上一级传入的结构体空间是在堆上分配的
}

/*
 *  传入的是一个包含单次 commit 信息的额外malloc出来的 zVerWrapInfo 结构体指针，需要释放其下的文件列表结构及其内部的文件内容结构
 */
void
zfree_one_commit_cache(void *zpIf) {
#ifdef _zDEBUG
    zCheck_Null_Exit(zpIf);
#endif
    struct zVecWrapInfo *zpVecWrapIf = (struct zVecWrapInfo *) zpIf;
    for (_i zFileId = 0; zFileId < zpVecWrapIf->VecSiz; zFileId++) {
        for (_i zLineId = 0; zLineId < zGet_SubVecWrapIfPtr(zpVecWrapIf, zFileId)->VecSiz; zLineId++) {
            free(zGet_SubVecWrapIfPtr(zpVecWrapIf, zFileId)->p_VecIf[zLineId].iov_base);
        }
        free(zGet_SubVecWrapIfPtr(zpVecWrapIf, zFileId)->p_VecIf);

        free(zGet_SubVecWrapIfPtr(zpVecWrapIf, zFileId)->p_RefDataIf->p_SubVecWrapIf);
        free(zGet_SubVecWrapIfPtr(zpVecWrapIf, zFileId)->p_RefDataIf);

        free(zGet_SubVecWrapIfPtr(zpVecWrapIf, zFileId));

        free(zpVecWrapIf->p_VecIf[zFileId].iov_base);
    }
    free(zpVecWrapIf->p_VecIf);

    free(zpVecWrapIf->p_RefDataIf->p_data);
    free(zpVecWrapIf->p_RefDataIf->p_SubVecWrapIf);
    free(zpVecWrapIf->p_RefDataIf);

    free(zpVecWrapIf);  // 传入的对象是为线程任务新开辟的内存空间，需要释放掉
}

/*
 * 功能：逐层生成单个代码库的 commit/deploy 列表、文件列表及差异内容缓存
 * 当有新的布署或撤销动作完成时，所有的缓存都会失效，因此每次都需要重新执行此函数以刷新预载缓存
 */
void
zgenerate_cache(void *zpIf) {
#ifdef _zDEBUG
    zCheck_Null_Exit(zpIf);
#endif
    struct zCacheMetaInfo *zpCacheMetaIf, *zpSubCacheMetaIf;
    struct zVecWrapInfo *zpTopVecWrapIf, *zpOldVecWrapIf;

    zpCacheMetaIf = (struct zCacheMetaInfo *)zpIf;

    struct zSendInfo *zpCommitSendIf;  // iov_base
    _i zSendDataLen, zVecDatalen, zCnter, *zpIntPtr;

    FILE *zpShellRetHandler;
    char *zpRes, zShellBuf[128], zLogPathBuf[128];

    if (zIsCommitCacheType ==zpCacheMetaIf->TopObjTypeMark) {
        zpTopVecWrapIf = &(zpGlobRepoIf[zpCacheMetaIf->RepoId].CommitVecWrapIf);

        sprintf(zShellBuf, "cd %s && git log --format=%%H%%ct", zpGlobRepoIf[zpCacheMetaIf->RepoId].RepoPath); // 必须在shell命令中切换到正确的工作路径
    } else {
        zpTopVecWrapIf = &(zpGlobRepoIf[zpCacheMetaIf->RepoId].DeployVecWrapIf);

        strcpy(zLogPathBuf, zpGlobRepoIf[zpCacheMetaIf->RepoId].RepoPath);
        strcat(zLogPathBuf, "/");
        strcat(zLogPathBuf, zSigLogPath);
        sprintf(zShellBuf, "cat %s", zLogPathBuf);
    }
    zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );
    
    for (zCnter = 0;  (zCnter < zCacheSiz) && (NULL != (zpRes = zget_one_line_from_FILE(zpShellRetHandler))); zCnter++) {
        zSendDataLen = 2 * sizeof(zpGlobRepoIf[zpCacheMetaIf->RepoId].CacheId);  // [最近一次布署的时间戳，即：CacheId]+[本次commit的时间戳]
        zVecDatalen = zSendDataLen + sizeof(struct zSendInfo);

        zCheck_Null_Exit( zpCommitSendIf = malloc(zVecDatalen) );

        zpCommitSendIf->SelfId = zCnter;
        zpCommitSendIf->DataLen = zSendDataLen;
		zpIntPtr = zpCommitSendIf->data;
		zpIntPtr[0] = zpGlobRepoIf[zpCacheMetaIf->RepoId].CacheId;  // 前一个整数位存放所属代码库的CacheId
        zpIntPtr[1] = strtol(zpRes + zBytes(40), NULL, 10);  // 后一个整数位存放本次 commit 的时间戳

        if (NULL != zGet_SubVecWrapIfPtr(zpTopVecWrapIf, zCnter)) {
            zMem_Alloc(zpOldVecWrapIf, struct zVecWrapInfo, 1);

            zpOldVecWrapIf->p_RefDataIf = zGet_SubVecWrapIfPtr(zpTopVecWrapIf, zCnter)->p_RefDataIf;
            zpOldVecWrapIf->p_VecIf = zGet_SubVecWrapIfPtr(zpTopVecWrapIf, zCnter)->p_VecIf;
            zpOldVecWrapIf->VecSiz = zGet_SubVecWrapIfPtr(zpTopVecWrapIf, zCnter)->VecSiz;

            zAdd_To_Thread_Pool(zfree_one_commit_cache, zpOldVecWrapIf);  // +
            zGet_SubVecWrapIfPtr(zpTopVecWrapIf, zCnter) = NULL;
        }

        zpTopVecWrapIf->p_VecIf[zCnter].iov_base = zpCommitSendIf;
        zpTopVecWrapIf->p_VecIf[zCnter].iov_len = zVecDatalen;

        zpRes[zBytes(40)] = '\0';
        zMem_Alloc(zpTopVecWrapIf->p_RefDataIf[zCnter].p_data, char, zBytes(41));  // 40位SHA1 sig ＋ 末尾'\0'
        strcpy(zpTopVecWrapIf->p_RefDataIf[zCnter].p_data, zpRes);

        zpGlobRepoIf[zpCacheMetaIf->RepoId].SortedCommitVecWrapIf.p_VecIf[zCnter].iov_base
            = zpGlobRepoIf[zpCacheMetaIf->RepoId].CommitVecWrapIf.p_VecIf[zCnter].iov_base;
        zpGlobRepoIf[zpCacheMetaIf->RepoId].SortedCommitVecWrapIf.p_VecIf[zCnter].iov_len
            = zpGlobRepoIf[zpCacheMetaIf->RepoId].CommitVecWrapIf.p_VecIf[zCnter].iov_len;
    }
    pclose(zpShellRetHandler);

    zpGlobRepoIf[zpCacheMetaIf->RepoId].SortedCommitVecWrapIf.VecSiz 
        = zpGlobRepoIf[zpCacheMetaIf->RepoId].CommitVecWrapIf.VecSiz 
        = zCnter;  // 存储的是实际的对象数量

    zpGlobRepoIf[zpCacheMetaIf->RepoId].CommitCacheQueueHeadId = zCacheSiz;  // 此后增量更新时，逆向写入，因此队列的下一个可写位置标记为最末一个位置

    // 生成下一级缓存
    _i zCacheUpdateLimit = (zPreLoadCacheSiz < zCnter) ? zPreLoadCacheSiz : zCnter;
    for (zCnter = 0; zCnter < zCacheUpdateLimit; zCnter++) {
        zMem_Alloc(zpSubCacheMetaIf, struct zCacheMetaInfo, 1);

        zpSubCacheMetaIf->TopObjTypeMark = zpCacheMetaIf->TopObjTypeMark;
        zpSubCacheMetaIf->RepoId = zpCacheMetaIf->RepoId;
        zpSubCacheMetaIf->CommitId = zCnter;
        zpSubCacheMetaIf->FileId = -1;

        zAdd_To_Thread_Pool(zget_file_list_and_diff_content, zpSubCacheMetaIf); // +
    }

    free(zpIf);  // 上一级传入的结构体空间是在堆上分配的
}

/*
 * 当监测到有新的代码提交时，为新版本代码生成缓存
 * 此函数在 inotify 中使用，传入的参数是 struct zObjInfo 数型
 */
void
zupdate_one_commit_cache(void *zpIf) {
#ifdef _zDEBUG
    zCheck_Null_Exit(zpIf);
#endif
	struct zObjInfo *zpObjIf;
    struct zCacheMetaInfo *zpSubCacheMetaIf;
    struct zVecWrapInfo *zpTopVecWrapIf, *zpSortedTopVecWrapIf, *zpOldVecWrapIf;

    struct zSendInfo *zpCommitSendIf;  // iov_base
    _i zSendDataLen, zVecDatalen, *zpHeadId, *zpIntPtr;

    FILE *zpShellRetHandler;
    char *zpRes, zShellBuf[128];

    zpObjIf = (struct zObjInfo*)zpIf;

    zpTopVecWrapIf = &(zpGlobRepoIf[zpObjIf->RepoId].CommitVecWrapIf);
    zpSortedTopVecWrapIf = &(zpGlobRepoIf[zpObjIf->RepoId].SortedCommitVecWrapIf);

    zpHeadId = &(zpGlobRepoIf[zpObjIf->RepoId].CommitCacheQueueHeadId);

    // 必须在shell命令中切换到正确的工作路径
    sprintf(zShellBuf, "cd %s && git log -1 --format=%%H%%ct", zpGlobRepoIf[zpObjIf->RepoId].RepoPath);
    zCheck_Null_Exit( zpShellRetHandler = popen(zShellBuf, "r") );
    zpRes = zget_one_line_from_FILE(zpShellRetHandler);
    pclose(zpShellRetHandler);

    zSendDataLen = 2 * sizeof(zpGlobRepoIf[zpObjIf->RepoId].CacheId);  // [最近一次布署的时间戳，即：CacheId]+[本次commit的时间戳]
    zVecDatalen = zSendDataLen + sizeof(struct zSendInfo);

    zCheck_Null_Exit( zpCommitSendIf = malloc(zVecDatalen) );

    zpCommitSendIf->SelfId = *zpHeadId;
    zpCommitSendIf->DataLen = zSendDataLen;
	zpIntPtr = zpCommitSendIf->data;
	zpIntPtr[0] = zpGlobRepoIf[zpObjIf->RepoId].CacheId;  // 前一个整数位存放所属代码库的CacheId
    zpIntPtr[1] = strtol(zpRes + zBytes(40), NULL, 10);  // 后一个整数位存放本次 commit 的时间戳

    if (NULL != zGet_SubVecWrapIfPtr(zpTopVecWrapIf, *zpHeadId)) {
        zMem_Alloc(zpOldVecWrapIf, struct zVecWrapInfo, 1);

        zpOldVecWrapIf->p_RefDataIf = zGet_SubVecWrapIfPtr(zpTopVecWrapIf, *zpHeadId)->p_RefDataIf;
        zpOldVecWrapIf->p_VecIf = zGet_SubVecWrapIfPtr(zpTopVecWrapIf, *zpHeadId)->p_VecIf;
        zpOldVecWrapIf->VecSiz = zGet_SubVecWrapIfPtr(zpTopVecWrapIf, *zpHeadId)->VecSiz;

        zAdd_To_Thread_Pool(zfree_one_commit_cache, zpOldVecWrapIf);  // +
        zGet_SubVecWrapIfPtr(zpTopVecWrapIf, *zpHeadId) = NULL;
    }

    zpTopVecWrapIf->p_VecIf[*zpHeadId].iov_base = zpCommitSendIf;
    zpTopVecWrapIf->p_VecIf[*zpHeadId].iov_len = zVecDatalen;

    zpRes[zBytes(40)] = '\0';
    zMem_Alloc(zpTopVecWrapIf->p_RefDataIf[*zpHeadId].p_data, char, zBytes(41));  // 40位SHA1 sig ＋ 末尾'\0'
    strcpy(zpTopVecWrapIf->p_RefDataIf[*zpHeadId].p_data, zpRes);

    if (zCacheSiz > zpTopVecWrapIf->VecSiz) {
        zpTopVecWrapIf->VecSiz++;
        zpSortedTopVecWrapIf->VecSiz++;
    }

    // 对缓存队列的结果进行排序（按时间戳降序排列），这是将要向前端发送的最终结果
    for (_i i = 0, j = *zpHeadId; i < zpTopVecWrapIf->VecSiz; i++) {
        zpSortedTopVecWrapIf->p_VecIf[i].iov_base = zpTopVecWrapIf->p_VecIf[j].iov_base;
        zpSortedTopVecWrapIf->p_VecIf[i].iov_len = zpTopVecWrapIf->p_VecIf[j].iov_len;

        if ((zpTopVecWrapIf->VecSiz - 1) == j) {
            j = 0;
        } else {
            j++;
        }
    }

    /* 生成下一级缓存 */
    zMem_Alloc(zpSubCacheMetaIf, struct zCacheMetaInfo, 1);

    zpSubCacheMetaIf->TopObjTypeMark = zIsCommitCacheType;
    zpSubCacheMetaIf->RepoId = zpObjIf->RepoId;
    zpSubCacheMetaIf->CommitId = *zpHeadId;
    zpSubCacheMetaIf->FileId = -1;

    zget_file_list_and_diff_content(zpSubCacheMetaIf);

    /* 更新队列下一次将写入的位置的索引 */
    if (0 == *zpHeadId) {
        *zpHeadId = zCacheSiz -1;
    } else {
        (*zpHeadId)--;
    }
}

/*
 * 将文本格式的ipv4地址转换成二进制无符号整型(按网络字节序，即大端字节序)，以及反向转换
 */
_ui
zconvert_ipv4_str_to_bin(const char *zpStrAddr) {
    struct in_addr zIpv4Addr;
    zCheck_Negative_Exit( inet_pton(AF_INET, zpStrAddr, &zIpv4Addr) );
    return zIpv4Addr.s_addr;
}

void
zconvert_ipv4_bin_to_str(_ui zIpv4BinAddr, char *zpBufOUT) {
    struct in_addr zIpv4Addr;
	zIpv4Addr.s_addr = zIpv4BinAddr;
    inet_ntop(AF_INET, &zIpv4Addr, zpBufOUT, INET_ADDRSTRLEN);
}

/*
 * 更新ipv4 地址缓存
 */
void
zupdate_ipv4_db_hash(_i zRepoId) {
// TEST:PASS
    struct stat zStatIf;
    struct zDeployResInfo *zpTmpIf;

    _i zFd[2] = {-9};
    zCheck_Negative_Exit(zFd[0] = open(zpGlobRepoIf[zRepoId].RepoPath, O_RDONLY));
    zCheck_Negative_Exit(zFd[1] = openat(zFd[0], zAllIpPath, O_RDONLY));  // 打开客户端ip地址数据库文件
    zCheck_Negative_Exit(fstat(zFd[1], &zStatIf));
    close(zFd[0]);

    zpGlobRepoIf[zRepoId].TotalHost = zStatIf.st_size / zSizeOf(_ui);  // 主机总数
    zMem_Alloc(zpGlobRepoIf[zRepoId].p_DpResList, struct zDeployResInfo, zpGlobRepoIf[zRepoId].TotalHost);  // 分配数组空间，用于顺序读取

    for (_i j = 0; j < zpGlobRepoIf[zRepoId].TotalHost; j++) {
        zpGlobRepoIf[zRepoId].p_DpResList[j].RepoId = zRepoId;  // 写入代码库索引值
        zpGlobRepoIf[zRepoId].p_DpResList[j].DeployState = 0;  // 初始化布署状态为0（即：未接收到确认时的状态）
        zpGlobRepoIf[zRepoId].p_DpResList[j].p_next = NULL;

        errno = 0;
        if (zSizeOf(_ui) != read(zFd[1], &(zpGlobRepoIf[zRepoId].p_DpResList->ClientAddr), zSizeOf(_ui))) { // 读入二进制格式的ipv4地址
            zPrint_Err(errno, NULL, "read client info failed!");
            exit(1);
        }

        zpTmpIf = zpGlobRepoIf[zRepoId].p_DpResHash[(zpGlobRepoIf[zRepoId].p_DpResList[j].ClientAddr) % zDeployHashSiz];  // HASH 定位
        if (NULL == zpTmpIf) {
            zpGlobRepoIf[zRepoId].p_DpResHash[(zpGlobRepoIf[zRepoId].p_DpResList[j].ClientAddr) % zDeployHashSiz] = &(zpGlobRepoIf[zRepoId].p_DpResList[j]);  // 若顶层为空，直接指向数组中对应的位置
        } else {
            while (NULL != zpTmpIf->p_next) {  // 将线性数组影射成 HASH 结构
                zpTmpIf = zpTmpIf->p_next;
            }

            zpTmpIf->p_next = &(zpGlobRepoIf[zRepoId].p_DpResList[j]);
        }
    }

    close(zFd[1]);
}

/*
 * 监控到ip数据文本文件变动，触发此函数执行二进制ip数据库更新，更新ip数据库
 */
void
zupdate_ipv4_db(void *zpIf) {
    _i zRepoId = *((_i *)zpIf);
    FILE *zpFileHandler = NULL;
    char *zpBuf = NULL;
    _ui zIpv4Addr = 0;
    _i zFd[3] = {0};

    pthread_rwlock_wrlock(&(zpGlobRepoIf[zRepoId].RwLock));

    zCheck_Negative_Exit(zFd[0] = open(zpGlobRepoIf[zRepoId].RepoPath, O_RDONLY));

    zCheck_Negative_Exit(zFd[1] = openat(zFd[0], zAllIpTxtPath, O_RDONLY));
    zCheck_Negative_Exit(zFd[2] = openat(zFd[0], zAllIpPath, O_WRONLY | O_TRUNC | O_CREAT, 0600));

    zCheck_Null_Exit(zpFileHandler = fdopen(zFd[1], "r"));
    zPCREInitInfo *zpPCREInitIf = zpcre_init("^(\\d{1,3}\\.){3}\\d{1,3}$");
    zPCRERetInfo *zpPCREResIf;
    for (_i i = 1; NULL != (zpBuf = zget_one_line_from_FILE(zpFileHandler)); i++) {
        zpPCREResIf = zpcre_match(zpPCREInitIf, zpBuf, 0);
        if (0 == zpPCREResIf->cnt) {
            zpcre_free_tmpsource(zpPCREResIf);
            zPrint_Time();
            fprintf(stderr, "\033[31;01m[%s]-[Line %d]: Invalid entry!\033[00m\n", zAllIpPath, i);
            exit(1);
        }
        zpcre_free_tmpsource(zpPCREResIf);

        zIpv4Addr = zconvert_ipv4_str_to_bin(zpBuf);
        if (sizeof(_ui) != write(zFd[2], &zIpv4Addr, sizeof(_ui))) {
            zPrint_Err(0, NULL, "Write to $zAllIpPath failed!");
            exit(1);
        }
    }
    zpcre_free_metasource(zpPCREInitIf);
    fclose(zpFileHandler);
    close(zFd[2]);
    close(zFd[1]);
    close(zFd[0]);

    // ipv4 数据文件更新后，立即更新对应的缓存中的列表与HASH
    zupdate_ipv4_db_hash(zRepoId);

    pthread_rwlock_unlock(&(zpGlobRepoIf[zRepoId].RwLock));
}

/*
 * 通用函数，调用外部程序或脚本文件执行相应的动作
 * 传入参数：
 * $1：代码库ID
 * $2：代码库绝对路径
 * $3：受监控路径名称
 */
void
zinotify_common_callback(void *zpIf) {
    struct zObjInfo *zpObjIf = (struct zObjInfo *) zpIf;
    char zShellBuf[zCommonBufSiz];

    sprintf(zShellBuf, "%s/.git_shadow/scripts/zpost-inotify %d %s %s",
        zpGlobRepoIf[zpObjIf->RepoId].RepoPath,
        zpObjIf->RepoId,
        zpGlobRepoIf[zpObjIf->RepoId].RepoPath,
        zpObjHash[zpObjIf->UpperWid]->path);

    if (0 != system(zShellBuf)) {
        zPrint_Err(0, NULL, "[system]: shell command failed!");
    }
}

/***********
 * NET OPS *
 ***********/
/* 通用的初始化动作 */
#define zRecv_Data_And_Check_RepoId_And_Init_TopWrap() do {\
    if (zSizeOf(struct zRecvInfo) > zrecv_nohang(zSd, &zRecvIf, sizeof(struct zRecvInfo), 0, NULL)) {\
        zPrint_Err(0, NULL, "接收到的数据不完整!");\
		zErrNo = -7;\
		zsendto(zSd, &zErrNo, sizeof(zErrNo), 0, NULL);\
        shutdown(zSd, SHUT_RDWR);\
        return;\
    }\
\
    if (0 > zRecvIf.RepoId || zGlobRepoNum <= zRecvIf.RepoId) {\
        zPrint_Err(0, NULL, "项目ID不存在!");\
		zErrNo = -2;\
		zsendto(zSd, &zErrNo, sizeof(zErrNo), 0, NULL);\
        shutdown(zSd, SHUT_RDWR);\
        return;\
    }\
\
	if (1 == zRecvIf.OpsId) {\
		zpTopVecWrapIf= &(zpGlobRepoIf[zRecvIf.RepoId].CommitVecWrapIf);\
    	zpSortedTopVecWrapIf = &(zpGlobRepoIf[zRecvIf.RepoId].SortedCommitVecWrapIf);\
	} else {\
    	zpTopVecWrapIf = zpSortedTopVecWrapIf = &(zpGlobRepoIf[zRecvIf.RepoId].DeployVecWrapIf);\
	}\
} while(0)

/* 适用于查询类动作：检查 CommitId 是否合法以及对应的缓存ID是否有效（与全局缓存ID是否一致），若缓存失效，则更新缓存 */
#define zCheck_CommitId() do {\
	if (0 > zRecvIf.CommitId || zCacheSiz <= zRecvIf.CommitId) {\
	    zPrint_Err(0, NULL, "版本号ID不存在!");\
		zErrNo = -3;\
		zsendto(zSd, &zErrNo, sizeof(zErrNo), 0, NULL);\
	    shutdown(zSd, SHUT_RDWR);\
	    return;\
	}\
} while(0)

/* 适用于查询类动作：检查 FileId 的时候，不检查缓存版本 */
#define zCheck_FileId() do {\
	if (0 > zRecvIf.FileId || zpTopVecWrapIf->p_RefDataIf[zRecvIf.CommitId].p_SubVecWrapIf->VecSiz <= zRecvIf.FileId) {\
	    zPrint_Err(0, NULL, "差异文件ID不存在!");\
		zErrNo = -4;\
		zsendto(zSd, &zErrNo, sizeof(zErrNo), 0, NULL);\
	    shutdown(zSd, SHUT_RDWR);\
	    return;\
	}\
} while(0)

/* zprint_diff_files 函数使用，检查缓存中的CacheId与全局CacheId是否一致，若不一致，则首先更新缓存*/
#define zCheck_Local_CacheId_And_Update_Cache() do {\
	if (NULL == zpTopVecWrapIf->p_RefDataIf[zRecvIf.CommitId].p_SubVecWrapIf\
			|| zpGlobRepoIf[zRecvIf.RepoId].CacheId != (_i)(zpTopVecWrapIf->p_RefDataIf[zRecvIf.CommitId].p_data)) {\
		zMem_Alloc(zpMetaIf, struct zCacheMetaInfo, 1);\
		zpMetaIf->TopObjTypeMark = (1 == zRecvIf.OpsId) ? zIsCommitCacheType : zIsDeployCacheType;\
		zpMetaIf->RepoId = zRecvIf.RepoId;\
		zpMetaIf->CommitId = zRecvIf.CommitId;\
		zget_file_list_and_diff_content(zpMetaIf);\
	}\
} while(0)

/* zdeploy 函数使用，检查前端所使用的缓存ID是否与本地一致*/
#define zCheck_Remote_CacheId_And_FreeRwLock_If_Match() do {\
	if (zRecvIf.CacheId != zpGlobRepoIf[zRecvIf.RepoId].CacheId) {\
    	pthread_rwlock_unlock( &(zpGlobRepoIf[zRecvIf.RepoId].RwLock) );\
	    zPrint_Err(0, NULL, "前端发送的缓存ID已失效!");\
		zErrNo = -8;\
		zsendto(zSd, &zErrNo, sizeof(zErrNo), 0, NULL);\
	    shutdown(zSd, SHUT_RDWR);\
	    return;\
	}\
} while(0)

/*
 * 0：添加新项目（代码库）
 */
void
zadd_repo(void *zpIf) {
	struct zRecvInfo zRecvIf;
	struct zVecWrapInfo *zpTopVecWrapIf, *zpSortedTopVecWrapIf;
	_i zSd, zErrNo;
    zSd = *((_i *)zpIf);

	zRecv_Data_And_Check_RepoId_And_Init_TopWrap();

	zErrNo = -100;
	zsendto(zSd, &zErrNo, sizeof(zErrNo), 0, NULL);
	// TO DO
}

/*
 * 1：开发人员已提交的版本号列表
 * 2：历史布署版本号列表
 */
void
zprint_record(void *zpIf) {
	struct zRecvInfo zRecvIf;
	struct zVecWrapInfo *zpTopVecWrapIf, *zpSortedTopVecWrapIf;
	_i zSd, zErrNo;
    zSd = *((_i *)zpIf);

	zRecv_Data_And_Check_RepoId_And_Init_TopWrap();

	zErrNo = -100;
	zsendto(zSd, &zErrNo, sizeof(zErrNo), 0, NULL);

    pthread_rwlock_rdlock( &(zpGlobRepoIf[zRecvIf.RepoId].RwLock) );

    zsendmsg(zSd, zpSortedTopVecWrapIf, 0, NULL);

    pthread_rwlock_unlock( &(zpGlobRepoIf[zRecvIf.RepoId].RwLock) );
    shutdown(zSd, SHUT_RDWR);
}

/*
 * 3：显示差异文件路径列表
 */
void
zprint_diff_files(void *zpIf) {
	struct zRecvInfo zRecvIf;
	struct zVecWrapInfo *zpTopVecWrapIf, *zpSortedTopVecWrapIf;
	_i zSd, zErrNo;
    zSd = *((_i *)zpIf);
	zRecv_Data_And_Check_RepoId_And_Init_TopWrap();

	struct zCacheMetaInfo *zpMetaIf;

    pthread_rwlock_rdlock( &(zpGlobRepoIf[zRecvIf.RepoId].RwLock) );
	zCheck_CommitId();

	zErrNo = -100;
	zsendto(zSd, &zErrNo, sizeof(zErrNo), 0, NULL);

	zCheck_Local_CacheId_And_Update_Cache();
    zsendmsg(zSd, zpSortedTopVecWrapIf->p_RefDataIf[zRecvIf.CommitId].p_SubVecWrapIf, 0, NULL);

    pthread_rwlock_unlock( &(zpGlobRepoIf[zRecvIf.RepoId].RwLock) );
    shutdown(zSd, SHUT_RDWR);
}

/*
 * 4：显示差异文件路径列表
 */
void
zprint_diff_content(void *zpIf) {
	struct zRecvInfo zRecvIf;
	struct zVecWrapInfo *zpTopVecWrapIf, *zpSortedTopVecWrapIf;
	_i zSd, zErrNo;
    zSd = *((_i *)zpIf);
	zRecv_Data_And_Check_RepoId_And_Init_TopWrap();

    pthread_rwlock_rdlock( &(zpGlobRepoIf[zRecvIf.RepoId].RwLock) );

	zCheck_CommitId();
	zCheck_FileId();

	zErrNo = -100;
	zsendto(zSd, &zErrNo, sizeof(zErrNo), 0, NULL);

    zsendmsg(zSd, zpSortedTopVecWrapIf->p_RefDataIf[zRecvIf.CommitId].p_SubVecWrapIf, 0, NULL);

    pthread_rwlock_unlock( &(zpGlobRepoIf[zRecvIf.RepoId].RwLock) );
    shutdown(zSd, SHUT_RDWR);
}

// 记录布署或撤销的日志
void
zwrite_log(_i zRepoId) {
    struct stat zStatIf;
    char zShellBuf[128], *zpRes;
    FILE *zpFile;
    _i zFd[2], zLen;

    zCheck_Negative_Exit(zFd[0] = open(zpGlobRepoIf[zRepoId].RepoPath, O_RDONLY));
    zCheck_Negative_Exit(zFd[1] = openat(zFd[0], zSigLogPath, O_RDONLY));
    zCheck_Negative_Exit(fstat(zFd[1], &zStatIf));  // 获取当前sig日志文件属性
    close(zFd[0]);
    close(zFd[1]);

    // 将 CURRENT 标签的40位sig字符串及10位时间戳追加写入.git_shadow/log/sig，连同行末的 '\0' 合计 51位
    sprintf(zShellBuf, "cd %s && git log -1 CURRENT --format=%%H%%ct", zpGlobRepoIf[zRepoId].RepoPath);
    zCheck_Null_Exit(zpFile = popen(zShellBuf, "r"));
    zpRes = zget_one_line_from_FILE(zpFile);

	if (zBytes(51) != (zLen = 1 + strlen(zpRes))) {
        zPrint_Err(0, NULL, "外部命令返回的信息错误：CURRENTsig + TimeStamp 的长度不等于 51 !");
	}

    if (zLen != write(zpGlobRepoIf[zRepoId].LogFd, zpRes, zLen)) {
        zCheck_Negative_Exit(ftruncate(zpGlobRepoIf[zRepoId].LogFd, zStatIf.st_size));
        zPrint_Err(0, NULL, "日志写入失败： <.git_shadow/log/deploy/sig> !");
        exit(1);
    }
}

/*
 * 5：布署
 * 6：撤销
 */
void
zdeploy(void *zpIf) {
	struct zRecvInfo zRecvIf;
	struct zVecWrapInfo *zpTopVecWrapIf, *zpSortedTopVecWrapIf;
	_i zSd, zErrNo;
    zSd = *((_i *)zpIf);
	zRecv_Data_And_Check_RepoId_And_Init_TopWrap();

	/* 如果当前代码库处于写操作锁定状态，则返回错误代码并退出 */
	if (1 == zpGlobRepoIf[zRecvIf.RepoId].DpLock) {
		zErrNo = -6;
		zsendto(zSd, &zErrNo, sizeof(zErrNo), 0, NULL);
	    shutdown(zSd, SHUT_RDWR);
		return;
	}

    char zShellBuf[zCommonBufSiz];  // 存放SHELL命令字符串
	char *zpFilePath;
	struct zCacheMetaInfo *zpMetaIf;

	char zIpv4AddrStr[INET_ADDRSTRLEN];
	struct zDeployResInfo *zpTmp = zpGlobRepoIf[zRecvIf.RepoId].p_DpResHash[zRecvIf.HostIp % zDeployHashSiz];

    pthread_rwlock_wrlock( &(zpGlobRepoIf[zRecvIf.RepoId].RwLock) );  // 加写锁

	zCheck_CommitId();
	zCheck_Remote_CacheId_And_FreeRwLock_If_Match();  // 若缓存ID不一致，会释放写锁并退出函数

	if (0 > zRecvIf.FileId) {
		zpFilePath = "";
	} else if (zGet_SubVecWrapIfPtr(zpTopVecWrapIf, zRecvIf.CommitId)->VecSiz <= zRecvIf.FileId) {
    	pthread_rwlock_unlock( &(zpGlobRepoIf[zRecvIf.RepoId].RwLock) );  // 释放锁
	    zPrint_Err(0, NULL, "差异文件ID不存在!");
		zErrNo = -4;
		zsendto(zSd, &zErrNo, sizeof(zErrNo), 0, NULL);
	    shutdown(zSd, SHUT_RDWR);
	    return;
	} else {
		zpFilePath = (char *)(((struct zSendInfo *)zGet_SendIfPtr(zGet_SubVecWrapIfPtr(zpTopVecWrapIf, zRecvIf.CommitId), zRecvIf.FileId))->data);
	}

	zErrNo = -100;
	zsendto(zSd, &zErrNo, sizeof(zErrNo), 0, NULL);

	zIpv4AddrStr[0] = '\0';
	while (NULL != zpTmp) {
		if (zRecvIf.HostIp == zpTmp->ClientAddr) {
			zconvert_ipv4_bin_to_str(zRecvIf.HostIp, zIpv4AddrStr);
			break;
		}
		zpTmp = zpTmp->p_next;
	}

	/* 重置布署状态 */
    zpGlobRepoIf[zRecvIf.RepoId].ReplyCnt = -1; 
    for (_i i = 0; i < zpGlobRepoIf[zRecvIf.RepoId].TotalHost; i++) {
        zpGlobRepoIf[zRecvIf.RepoId].p_DpResHash[i]->DeployState = 0;
    }

	/* 执行外部脚本使用git进行布署 */
    sprintf(zShellBuf, "cd %s && ./.git_shadow/scripts/zdeploy.sh -D -f %s -H %s", zpGlobRepoIf[zRecvIf.RepoId].RepoPath, zpFilePath, zIpv4AddrStr);
    if (0 != system(zShellBuf)) {
		zPrint_Err(0, NULL, "shell 布署命令出错!");
	}

	//等待所有主机的状态都得到确认，每隔 0.2 秒向前端发送已成功部署的数量统计
	while (zpGlobRepoIf[zRecvIf.RepoId].TotalHost != zpGlobRepoIf[zRecvIf.RepoId].ReplyCnt) {
		zsleep(0.2);
		zsendto(zSd, &(zpGlobRepoIf[zRecvIf.RepoId].ReplyCnt), sizeof(zpGlobRepoIf[zRecvIf.RepoId].ReplyCnt), 0, NULL);
	}

	/* 将本次布署信息写入日志 */
    zwrite_log(zRecvIf.RepoId);

	/* 更新全局缓存 */
	zMem_Alloc(zpMetaIf, struct zCacheMetaInfo, 1);
	zpMetaIf->TopObjTypeMark = zIsCommitCacheType;
	zpMetaIf->RepoId = zRecvIf.RepoId;
	zgenerate_cache(zpMetaIf);  // 此处第一层动作不使用线程池，保证数据一致性

	zMem_Alloc(zpMetaIf, struct zCacheMetaInfo, 1);
	zpMetaIf->TopObjTypeMark = zIsDeployCacheType;
	zpMetaIf->RepoId = zRecvIf.RepoId;
	zgenerate_cache(zpMetaIf);  // 此处第一层动作不使用线程池，保证数据一致性

    pthread_rwlock_unlock( &(zpGlobRepoIf[zRecvIf.RepoId].RwLock) );
    shutdown(zSd, SHUT_RDWR);
}

/*
 * 9：回复尚未确认成功的主机列表
 */
void
zprint_failing_list(void *zpIf) {
	struct zRecvInfo zRecvIf;
	struct zVecWrapInfo *zpTopVecWrapIf, *zpSortedTopVecWrapIf;
	_i zSd, zErrNo;
    zSd = *((_i *)zpIf);
	zRecv_Data_And_Check_RepoId_And_Init_TopWrap();

	_ui *zpFailingList = zpGlobRepoIf[zRecvIf.RepoId].p_FailingList;
	/* 第一个元素写入实时时间戳 */
	zpFailingList[0] = time(NULL);

	if (zpGlobRepoIf[zRecvIf.RepoId].ReplyCnt == zpGlobRepoIf[zRecvIf.RepoId].TotalHost) {
	    zsendto(zSd, zpFailingList, sizeof(zpFailingList[0]), 0, NULL);
	} else {
    	_i zUnReplyCnt = 1;

		/* 顺序遍历线性列表，获取尚未确认状态的客户端ip列表 */
    	for (_i i = 0; i < zpGlobRepoIf[zRecvIf.RepoId].TotalHost; i++) {
    	    if (0 == zpGlobRepoIf[zRecvIf.RepoId].p_DpResList[i].DeployState) {
    	        zpFailingList[zUnReplyCnt] = zpGlobRepoIf[zRecvIf.RepoId].p_DpResList[i].ClientAddr;
    	        zUnReplyCnt++;
    	    }
    	}

	    zsendto(zSd, zpFailingList, zUnReplyCnt * sizeof(zpFailingList[0]), 0, NULL);
	}
}

/*
 * 7：布署成功人工确认
 * 8：布署成功主机自动确认
 */
void
zstate_confirm(void *zpIf) {
	struct zRecvInfo zRecvIf;
	struct zVecWrapInfo *zpTopVecWrapIf, *zpSortedTopVecWrapIf;
	_i zSd, zErrNo;
    zSd = *((_i *)zpIf);
	zRecv_Data_And_Check_RepoId_And_Init_TopWrap();

	/* 若是人工确认的状态，回复本次动作的执行结果 */
	if (7 == zRecvIf.OpsId) {
		zErrNo = -100;
		zsendto(zSd, &zErrNo, sizeof(zErrNo), 0, NULL);
	}

	struct zDeployResInfo *zpTmp = zpGlobRepoIf[zRecvIf.RepoId].p_DpResHash[zRecvIf.HostIp % zDeployHashSiz];

    for (; zpTmp != NULL; zpTmp = zpTmp->p_next) {  // 遍历
        if (zpTmp->ClientAddr == zRecvIf.HostIp) {
            zpTmp->DeployState = 1;

    		pthread_mutex_lock( &(zpGlobRepoIf[zRecvIf.RepoId].MutexLock) );

            zpGlobRepoIf[zRecvIf.RepoId].ReplyCnt++;

    		pthread_mutex_unlock( &(zpGlobRepoIf[zRecvIf.RepoId].MutexLock) );

            shutdown(zSd, SHUT_RDWR);
            return;
        }
    }

    zPrint_Err(0, NULL, "不明来源的确认信息!");
    shutdown(zSd, SHUT_RDWR);
}

/*
 * 10：仅更新集群中负责与中控机直接通信的主机的 ip 列表
 * 11：更新集群中所有主机的 ip 列表
 */
void
zupdate_ipv4_db_txt(void *zpIf) {
	struct zRecvInfo zRecvIf;
	struct zVecWrapInfo *zpTopVecWrapIf, *zpSortedTopVecWrapIf;
	_i zSd, zErrNo;
    zSd = *((_i *)zpIf);
	zRecv_Data_And_Check_RepoId_And_Init_TopWrap();

	/* 如果当前代码库处于写操作锁定状态，则返回错误代码并退出 */
	if (1 == zpGlobRepoIf[zRecvIf.RepoId].DpLock) {
		zErrNo = -6;
		zsendto(zSd, &zErrNo, sizeof(zErrNo), 0, NULL);
	    shutdown(zSd, SHUT_RDWR);
		return;
	}

    char zRecvBuf[zCommonBufSiz], zPathBuf[128], *zpWritePath;
    _i zFd, zRecvSiz;

    zpWritePath = (10 == zRecvIf.OpsId) ? zMajorIpTxtPath : zAllIpTxtPath;

    strcpy(zPathBuf, zpGlobRepoIf[zRecvIf.RepoId].RepoPath);
    strcat(zPathBuf, "/");
    strcat(zPathBuf, zpWritePath);

    zCheck_Negative_Exit( zFd = open(zPathBuf, O_WRONLY | O_TRUNC | O_CREAT, 0600) );

    /* 接收网络数据并同步写入文件 */
    while (0 < (zRecvSiz = recv(zSd, zRecvBuf, zCommonBufSiz, 0))) {
        if (zRecvSiz != write(zFd, zRecvBuf, zRecvSiz)) {
            zPrint_Err(errno, NULL, "Write ipv4 list to txt file failed!");
            exit(1);
        }
    }
    zCheck_Negative_Exit(zRecvSiz);

    close(zFd);

    /* 将新接收的文件的 MD5 checksum 发送给前端校验 */
    zsendto(zSd, zgenerate_file_sig_md5(zPathBuf), zBytes(36), 0, NULL);

    shutdown(zSd, SHUT_RDWR);
}

/*
 * 12；拒绝(锁定)某个项目的 布署／撤销／更新ip数据库 功能，仅提供查询服务
 * 13：允许布署／撤销／更新ip数据库
 */
void
zlock_repo(void *zpIf) {
	struct zRecvInfo zRecvIf;
	struct zVecWrapInfo *zpTopVecWrapIf, *zpSortedTopVecWrapIf;
	_i zSd, zErrNo;
    zSd = *((_i *)zpIf);
	zRecv_Data_And_Check_RepoId_And_Init_TopWrap();

	zErrNo = -100;
	zsendto(zSd, &zErrNo, sizeof(zErrNo), 0, NULL);

	pthread_mutex_lock(&(zpGlobRepoIf[zRecvIf.RepoId].MutexLock));

	if (12 == zRecvIf.OpsId) {
		zpGlobRepoIf[zRecvIf.RepoId].DpLock = 1;
	} else {
		zpGlobRepoIf[zRecvIf.RepoId].DpLock = 0;
	}

	pthread_mutex_unlock(&(zpGlobRepoIf[zRecvIf.RepoId].MutexLock));
}

/*
 * 核心路由函数
 */
void
zstart_server(void *zpIf) {
#define zMaxEvents 64
    /* 如下部分定义服务接口 */
    zThreadPoolOps zNetServ[zServHashSiz];
    zNetServ[0] = zadd_repo;  // 添加新代码库
    zNetServ[1] = zprint_record;  // 显示提交记录
    zNetServ[2] = zprint_record;  // 显示布署记录
    zNetServ[3] = zprint_diff_files;  // 显示差异文件路径列表
    zNetServ[4] = zprint_diff_content;  // 显示差异文件内容
    zNetServ[5] = zdeploy;  // 布署(如果 zRecvInfo 中 IP 地址数据段不为-1，则表示仅布署到指定的单台主机，适用于前端要求重试的场景)
    zNetServ[6] = zdeploy;  // 撤销(如果 zRecvInfo 中 IP 地址数据段不为-1，则表示仅布署到指定的单台主机，适用于前端要求重试的场景)
    zNetServ[7] = zstate_confirm;  // 布署成功状态人工确认
    zNetServ[8] = zstate_confirm;  // 布署成功状态自动确认
    zNetServ[9] = zprint_failing_list;  // 显示尚未布署成功的主机 ip 列表
    zNetServ[10] = zupdate_ipv4_db_txt;  // 仅更新集群中负责与中控机直接通信的主机的 ip 列表
    zNetServ[11] = zupdate_ipv4_db_txt;  // 更新集群中所有主机的 ip 列表
    zNetServ[12] = zlock_repo;  // 锁定某个项目的布署／撤销功能，仅提供查询服务（即只读服务）
    zNetServ[13] = zlock_repo;  // 恢复布署／撤销功能

    /* 如下部分配置 epoll 环境 */
    struct zNetServInfo *zpNetServIf = (struct zNetServInfo *)zpIf;
    struct epoll_event zEv, zEvents[zMaxEvents];
    _i zMajorSd, zConnSd, zEvNum, zEpollSd, zErrNo;

    zMajorSd = zgenerate_serv_SD(zpNetServIf->p_host, zpNetServIf->p_port, zpNetServIf->zServType);  // 返回的 socket 已经做完 bind 和 listen

    zEpollSd = epoll_create1(0);
    zCheck_Negative_Return(zEpollSd,);

    zEv.events = EPOLLIN;
    zEv.data.fd = zMajorSd;
    zCheck_Negative_Exit( epoll_ctl(zEpollSd, EPOLL_CTL_ADD, zMajorSd, &zEv) );

    // 如下部分启动 epoll 监听服务
    for (_i zCmd = 0;;) {  // zCmd: 用于存放前端发送的操作指令
        zEvNum = epoll_wait(zEpollSd, zEvents, zMaxEvents, -1);  // 阻塞等待事件发生
        zCheck_Negative_Return(zEvNum,);

        for (_i i = 0; i < zEvNum; i++, zCmd = 0) {
           if (zEvents[i].data.fd == zMajorSd) {  // 主socket上收到事件，执行accept
               zCheck_Negative_Exit( zConnSd = accept(zMajorSd, (struct sockaddr *) NULL, 0) );

               zEv.events = EPOLLIN | EPOLLET;  // 新创建的socket以边缘触发模式监控
               zEv.data.fd = zConnSd;
               zCheck_Negative_Exit( epoll_ctl(zEpollSd, EPOLL_CTL_ADD, zConnSd, &zEv) );
            } else {
				if (sizeof(_i) != zrecv_nohang(zEvents[i].data.fd, &zCmd, sizeof(_i), MSG_PEEK, NULL)) {
    			    zPrint_Err(0, NULL, "接收到的数据不完整!");
					zErrNo = -7;
					zsendto(zEvents[i].data.fd, &zErrNo, sizeof(zErrNo), 0, NULL);
					continue;
				}
    			if (0 > zCmd || zServHashSiz <= zCmd) {
    			    zPrint_Err(0, NULL, "操作指令ID不存在!");
					zErrNo = -1;
					zsendto(zEvents[i].data.fd, &zErrNo, sizeof(zErrNo), 0, NULL);
					continue;
    			}
                zAdd_To_Thread_Pool(zNetServ[zCmd], &(zEvents[i].data.fd));
            }
        }
    }
#undef zMaxEvents
}

/*
 * 主机更新自身ipv4数据库文件
 */
void
zupdate_ipv4_db_self(_i zBaseFd) {
    FILE *zpFileHandler;
    char *zpBuf;
    _ui zIpv4Addr;
    _i zFd;

    zCheck_Negative_Exit(zFd = openat(zBaseFd, zSelfIpPath, O_WRONLY | O_TRUNC | O_CREAT, 0600));

    zCheck_Null_Exit( zpFileHandler = popen("ip addr | grep -oP '(\\d{1,3}\\.){3}\\d{1,3}' | grep -v 127", "r") );
    while (NULL != (zpBuf = zget_one_line_from_FILE(zpFileHandler))) {
        zIpv4Addr = zconvert_ipv4_str_to_bin(zpBuf);
        if (zSizeOf(_ui) != write(zFd, &zIpv4Addr, zSizeOf(_ui))) {
            zPrint_Err(0, NULL, "自身IP地址更新失败!");
            exit(1);
        }
    }

    fclose(zpFileHandler);
    close(zFd);
}

/*
 * 用于集群中的主机向中控机发送状态确认信息
 */
void
zstate_reply(char *zpHost, char *zpPort) {
	struct zRecvInfo zDpResIf;
    _i zFd, zSd, zResLen;
    _ui zIpv4Bin;

    zCheck_Negative_Exit( zFd = open(zRepoIdPath, O_RDONLY) );
	/* 读取版本库ID */
    zCheck_Negative_Exit( read(zFd, &(zDpResIf.RepoId), sizeof(_i)) );
	/* 更新自身 ip 地址 */
	zupdate_ipv4_db_self(zFd);
    close(zFd);
	/* 填充动作代号 */
	zDpResIf.OpsId = 10;
	/* 以点分格式的ipv4地址连接服务端 */
    if (-1== (zSd = ztcp_connect(zpHost, zpPort, AI_NUMERICHOST | AI_NUMERICSERV))) {
        zPrint_Err(0, NULL, "无法与中控机建立连接！");
        exit(1);
    }
	/* 读取本机的所有非回环ip地址，依次发送状态确认信息至服务端 */
    zCheck_Negative_Exit( zFd = open(zSelfIpPath, O_RDONLY) );

    while (0 < (zResLen = read(zFd, &zIpv4Bin, sizeof(_ui)))) {
        zDpResIf.HostIp = zIpv4Bin;
        if (sizeof(zDpResIf) != zsendto(zSd, &zDpResIf, sizeof(zDpResIf), 0, NULL)) {
            zPrint_Err(0, NULL, "布署状态信息回复失败！");
        }
    }

    shutdown(zSd, SHUT_RDWR);
    close(zFd);
}
