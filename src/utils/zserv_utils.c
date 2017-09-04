#ifndef _Z
    #include "../zmain.c"
#endif

/************
 * META OPS *
 ************/
/* 重置内存池状态，释放掉后来扩展的空间，恢复为初始大小 */
#define zReset_Mem_Pool_State(zRepoId) do {\
    pthread_mutex_lock(&(zppGlobRepoIf[zRepoId]->MemLock));\
    \
    void **zppPrev = zppGlobRepoIf[zRepoId]->p_MemPool;\
    while(NULL != zppPrev[0]) {\
        zppPrev = zppPrev[0];\
        munmap(zppGlobRepoIf[zRepoId]->p_MemPool, zMemPoolSiz);\
        zppGlobRepoIf[zRepoId]->p_MemPool = zppPrev;\
    }\
    zppGlobRepoIf[zRepoId]->MemPoolOffSet = sizeof(void *);\
    \
    pthread_mutex_unlock(&(zppGlobRepoIf[zRepoId]->MemLock));\
} while(0)

/**************
 * NATIVE OPS *
 **************/
/* 在任务分发之前执行：定义必要的计数器、锁、条件变量等 */
#define zCcur_Init(zRepoId, zTotalTask, zSuffix) \
    _i *zpFinMark##zSuffix = zalloc_cache(zRepoId, sizeof(_i));\
    _i *zpTotalTask##zSuffix = zalloc_cache(zRepoId, sizeof(_i));\
    _i *zpTaskCnter##zSuffix = zalloc_cache(zRepoId, sizeof(_i));\
    _i *zpThreadCnter##zSuffix = zalloc_cache(zRepoId, sizeof(_i));\
    *(zpFinMark##zSuffix) = 0;\
    *(zpTotalTask##zSuffix) = zTotalTask;\
    *(zpTaskCnter##zSuffix) = 0;\
    *(zpThreadCnter##zSuffix) = 0;\
\
    pthread_cond_t *(zpCondVar##zSuffix) = zalloc_cache(zRepoId, sizeof(pthread_cond_t));\
    pthread_cond_init(zpCondVar##zSuffix, NULL);\
\
    pthread_mutex_t *(zpMutexLock##zSuffix) = zalloc_cache(zRepoId, 3 * sizeof(pthread_mutex_t));\
    pthread_mutex_init(zpMutexLock##zSuffix, NULL);\
    pthread_mutex_init(zpMutexLock##zSuffix + 1, NULL);\
    pthread_mutex_init(zpMutexLock##zSuffix + 2, NULL);\
\
    pthread_mutex_lock(zpMutexLock##zSuffix);

/* 配置将要传递给工作线程的参数(结构体) */
#define zCcur_Sub_Config(zpSubIf, zSuffix) \
    zpSubIf->p_FinMark = zpFinMark##zSuffix;\
    zpSubIf->p_TotalTask = zpTotalTask##zSuffix;\
    zpSubIf->p_TaskCnter = zpTaskCnter##zSuffix;\
    zpSubIf->p_ThreadCnter = zpThreadCnter##zSuffix;\
    zpSubIf->p_CondVar = zpCondVar##zSuffix;\
    zpSubIf->p_MutexLock[0] = zpMutexLock##zSuffix;\
    zpSubIf->p_MutexLock[1] = zpMutexLock##zSuffix + 1;\
    zpSubIf->p_MutexLock[2] = zpMutexLock##zSuffix + 2;

/* 用于线程递归分发任务的场景，如处理树结构时 */
#define zCcur_Sub_Config_Thread(zpSubIf, zpIf) \
    zpSubIf->p_FinMark = zpIf->p_FinMark;\
    zpSubIf->p_TotalTask = zpIf->p_TotalTask;\
    zpSubIf->p_TaskCnter = zpIf->p_TaskCnter;\
    zpSubIf->p_ThreadCnter = zpIf->p_ThreadCnter;\
    zpSubIf->p_CondVar = zpIf->p_CondVar;\
    zpSubIf->p_MutexLock[0] = zpIf->p_MutexLock[0];\
    zpSubIf->p_MutexLock[1] = zpIf->p_MutexLock[1];\
    zpSubIf->p_MutexLock[2] = zpIf->p_MutexLock[2];

/* 放置于调用者每次分发任务之前(即调用工作线程之前)，其中zStopExpression指最后一次循环的判断条件，如：A > B && C < D */
#define zCcur_Fin_Mark(zStopExpression, zSuffix) do {\
        pthread_mutex_lock(zpMutexLock##zSuffix + 2);\
        (*(zpTaskCnter##zSuffix))++;\
        pthread_mutex_unlock(zpMutexLock##zSuffix + 2);\
        if (zStopExpression) {\
            *(zpFinMark##zSuffix) = 1;\
        }\
    } while(0)

/* 用于线程递归分发任务的场景，如处理树结构时 */
#define zCcur_Fin_Mark_Thread(zpIf) do {\
        pthread_mutex_lock(zpIf->p_MutexLock[2]);\
        (*(zpIf->p_TaskCnter))++;\
        pthread_mutex_unlock(zpIf->p_MutexLock[2]);\
        if (*(zpIf->p_TotalTask) == *(zpIf->p_TaskCnter)) {\
            *(zpIf->p_FinMark) = 1;\
        }\
    } while(0)

/*
 * 用于存在条件式跳转的循环场景
 * 每次跳过时，都必须让同步计数器递减一次
 */
#define zCcur_Cnter_Subtract(zSuffix) do {\
        (*(zpTaskCnter##zSuffix))--;\
} while(0)

/*
 * 当调用者任务分发完成之后执行，之后释放资源占用
 */
#define zCcur_Wait(zSuffix) do {\
        while ((1 != *(zpFinMark##zSuffix)) || *(zpTaskCnter##zSuffix) != *(zpThreadCnter##zSuffix)) {\
            pthread_cond_wait(zpCondVar##zSuffix, zpMutexLock##zSuffix);\
        }\
        pthread_mutex_unlock(zpMutexLock##zSuffix);\
        pthread_cond_destroy(zpCondVar##zSuffix);\
        pthread_mutex_destroy((zpMutexLock##zSuffix) + 2);\
        pthread_mutex_destroy((zpMutexLock##zSuffix) + 1);\
        pthread_mutex_destroy(zpMutexLock##zSuffix);\
    } while(0)

/* 放置于工作线程的回调函数末尾 */
#define zCcur_Fin_Signal(zpIf) do {\
        pthread_mutex_lock(zpIf->p_MutexLock[1]);\
        (*(zpIf->p_ThreadCnter))++;\
        pthread_mutex_unlock(zpIf->p_MutexLock[1]);\
        if ((1 == *(zpIf->p_FinMark)) && (*(zpIf->p_TaskCnter) == *(zpIf->p_ThreadCnter))) {\
            pthread_mutex_lock(zpIf->p_MutexLock[0]);\
            pthread_mutex_unlock(zpIf->p_MutexLock[0]);\
\
            /* 此处一定要再次检查条件是否成立，避免任务分发过程中，已提前完成任务的线程恰好条件成立的情况 */\
            if ((1 == *(zpIf->p_FinMark)) && (*(zpIf->p_TaskCnter) == *(zpIf->p_ThreadCnter))) {\
                pthread_cond_signal(zpIf->p_CondVar);\
            }\
        }\
    } while(0)

/* 用于提取深层对象 */
#define zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zCommitId) ((zpTopVecWrapIf)->p_RefDataIf[zCommitId].p_SubVecWrapIf)
#define zGet_OneFileVecWrapIf(zpTopVecWrapIf, zCommitId, zFileId) ((zpTopVecWrapIf)->p_RefDataIf[zCommitId].p_SubVecWrapIf->p_RefDataIf[zFileId].p_SubVecWrapIf)

#define zGet_OneCommitSig(zpTopVecWrapIf, zCommitId) ((zpTopVecWrapIf)->p_RefDataIf[zCommitId].p_data)
#define zGet_OneFilePath(zpTopVecWrapIf, zCommitId, zFileId) ((zpTopVecWrapIf)->p_RefDataIf[zCommitId].p_SubVecWrapIf->p_RefDataIf[zFileId].p_data)

/*
 * 功能：生成单个文件的差异内容缓存
 */
void *
zget_diff_content(void *zpIf) {
    zMetaInfo *zpMetaIf = (zMetaInfo *)zpIf;
    zVecWrapInfo *zpTopVecWrapIf;
    zBaseDataInfo *zpTmpBaseDataIf[3];
    _i zBaseDataLen, zCnter;

    FILE *zpShellRetHandler;
    char zShellBuf[zCommonBufSiz], zRes[zBytes(1448)];  // MTU 上限，每个分片最多可以发送1448 Bytes

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf);
    } else if (zIsDeployDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->DeployVecWrapIf);
    } else {
        zPrint_Err(0, NULL, "数据类型错误!");
        return NULL;
    }

    /* 必须在shell命令中切换到正确的工作路径 */
    sprintf(zShellBuf, "cd \"%s\" && git diff \"%s\" \"%s\" -- \"%s\"",
            zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath,
            zppGlobRepoIf[zpMetaIf->RepoId]->zLastDeploySig,
            zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId),
            zGet_OneFilePath(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId));

    zCheck_Null_Exit(zpShellRetHandler = popen(zShellBuf, "r"));

    /* 此处读取行内容，因为没有下一级数据，故采用大片读取，不再分行 */
    zCnter = 0;
    if (0 < (zBaseDataLen = zget_str_content(zRes, zBytes(1448), zpShellRetHandler))) {
        zpTmpBaseDataIf[0] = zalloc_cache(zpMetaIf->RepoId, sizeof(zBaseDataInfo) + zBaseDataLen);
        zpTmpBaseDataIf[0]->DataLen = zBaseDataLen;
        memcpy(zpTmpBaseDataIf[0]->p_data, zRes, zBaseDataLen);

        zpTmpBaseDataIf[2] = zpTmpBaseDataIf[1] = zpTmpBaseDataIf[0];
        zpTmpBaseDataIf[1]->p_next = NULL;

        zCnter++;
        for (; 0 < (zBaseDataLen = zget_str_content(zRes, zBytes(1448), zpShellRetHandler)); zCnter++) {
            zpTmpBaseDataIf[0] = zalloc_cache(zpMetaIf->RepoId, sizeof(zBaseDataInfo) + zBaseDataLen);
            zpTmpBaseDataIf[0]->DataLen = zBaseDataLen;
            memcpy(zpTmpBaseDataIf[0]->p_data, zRes, zBaseDataLen);

            zpTmpBaseDataIf[1]->p_next = zpTmpBaseDataIf[0];
            zpTmpBaseDataIf[1] = zpTmpBaseDataIf[0];
        }

        pclose(zpShellRetHandler);
    } else {
        pclose(zpShellRetHandler);
        return (void *) -1;
    }

    if (0 == zCnter) {
        zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId) = NULL;
    } else {
        zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId) = zalloc_cache(zpMetaIf->RepoId, sizeof(zVecWrapInfo));
        zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->VecSiz = 0;  // 先赋为 0
        zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->p_RefDataIf = NULL;
        zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->p_VecIf = zalloc_cache(zpMetaIf->RepoId, zCnter * sizeof(struct iovec));
        for (_i i = 0; i < zCnter; i++, zpTmpBaseDataIf[2] = zpTmpBaseDataIf[2]->p_next) {
            zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->p_VecIf[i].iov_base = zpTmpBaseDataIf[2]->p_data;
            zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->p_VecIf[i].iov_len = zpTmpBaseDataIf[2]->DataLen;
        }

        /* 最后为 VecSiz 赋值，通知同类请求缓存已生成 */
        zGet_OneFileVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId, zpMetaIf->FileId)->VecSiz = zCnter;
    }

    return NULL;
}

/*
 * 功能：生成某个 Commit 版本(提交记录与布署记录通用)的文件差异列表
 */
void *
zgenerate_graph(void *zpIf) {
    zMetaInfo *zpNodeIf, *zpTmpNodeIf;
    _i zOffSet;

    zpNodeIf = (zMetaInfo *)zpIf;
    zpNodeIf->pp_ResHash[zpNodeIf->LineNum] = zpIf;
    zOffSet = 6 * zpNodeIf->OffSet + 10;

    zpNodeIf->p_data[--zOffSet] = ' ';
    zpNodeIf->p_data[--zOffSet] = '\200';
    zpNodeIf->p_data[--zOffSet] = '\224';
    zpNodeIf->p_data[--zOffSet] = '\342';
    zpNodeIf->p_data[--zOffSet] = '\200';
    zpNodeIf->p_data[--zOffSet] = '\224';
    zpNodeIf->p_data[--zOffSet] = '\342';
    zpNodeIf->p_data[--zOffSet] = (NULL == zpNodeIf->p_left) ? '\224' : '\234';
    zpNodeIf->p_data[--zOffSet] = '\224';
    zpNodeIf->p_data[--zOffSet] = '\342';

    zpTmpNodeIf = zpNodeIf;
    for (_i i = 0; i < zpNodeIf->OffSet; i++) {
        zpNodeIf->p_data[--zOffSet] = ' ';
        zpNodeIf->p_data[--zOffSet] = ' ';
        zpNodeIf->p_data[--zOffSet] = ' ';

        zpTmpNodeIf = zpTmpNodeIf->p_father;
        if (NULL == zpTmpNodeIf->p_left) {
            zpNodeIf->p_data[--zOffSet] = ' ';
        } else {
            zpNodeIf->p_data[--zOffSet] = '\202';
            zpNodeIf->p_data[--zOffSet] = '\224';
            zpNodeIf->p_data[--zOffSet] = '\342';
        }
    }

    zpNodeIf->p_data = zpNodeIf->p_data + zOffSet;

    zCcur_Fin_Mark_Thread(zpNodeIf);
    zCcur_Fin_Signal(zpNodeIf);

    return NULL;
}

void *
zdistribute_task(void *zpIf) {
    zMetaInfo *zpNodeIf, *zpTmpNodeIf;
    zpNodeIf = (zMetaInfo *)zpIf;

    zpTmpNodeIf = zpNodeIf->p_left;
    if (NULL != zpTmpNodeIf) {  // 不能用循环，会导致重复发放
        zpTmpNodeIf->pp_ResHash = zpNodeIf->pp_ResHash;

        zCcur_Sub_Config_Thread(zpTmpNodeIf, zpNodeIf);
        zAdd_To_Thread_Pool(zdistribute_task, zpTmpNodeIf);
    }

    zpTmpNodeIf = zpNodeIf->p_FirstChild;
    if (NULL != zpTmpNodeIf) {  // 不能用循环，会导致重复发放
        zpTmpNodeIf->pp_ResHash = zpNodeIf->pp_ResHash;

        zCcur_Sub_Config_Thread(zpTmpNodeIf, zpNodeIf);
        zAdd_To_Thread_Pool(zdistribute_task, zpTmpNodeIf);
    }

    zAdd_To_Thread_Pool(zgenerate_graph, zpIf);

    return NULL;
}

#define zGenerate_Tree_Node() do {\
    zpTmpNodeIf[0] = zalloc_cache(zpMetaIf->RepoId, sizeof(zMetaInfo));\
    zpTmpNodeIf[0]->LineNum = zLineCnter;  /* 横向偏移 */\
    zLineCnter++;  /* 每个节点会占用一行显示输出 */\
    zpTmpNodeIf[0]->OffSet = zNodeCnter;  /* 纵向偏移 */\
\
    zpTmpNodeIf[0]->p_FirstChild = NULL;\
    zpTmpNodeIf[0]->p_left = NULL;\
    zpTmpNodeIf[0]->p_data = zalloc_cache(zpMetaIf->RepoId, 6 * zpTmpNodeIf[0]->OffSet + 10 + 1 + strlen(zpPcreRetIf->p_rets[zNodeCnter]));\
    strcpy(zpTmpNodeIf[0]->p_data + 6 * zpTmpNodeIf[0]->OffSet + 10, zpPcreRetIf->p_rets[zNodeCnter]);\
\
    zpTmpNodeIf[0]->OpsId = 0;\
    zpTmpNodeIf[0]->RepoId = zpMetaIf->RepoId;\
    zpTmpNodeIf[0]->CommitId = zpMetaIf->CommitId;\
    zpTmpNodeIf[0]->CacheId = zppGlobRepoIf[zpMetaIf->RepoId]->CacheId;\
    zpTmpNodeIf[0]->DataType = zpMetaIf->DataType;\
\
    if (zNodeCnter == (zpPcreRetIf->cnt - 1)) {\
        zpTmpNodeIf[0]->FileId = zpTmpNodeIf[0]->LineNum;\
        zpTmpNodeIf[0]->p_ExtraData = zalloc_cache(zpMetaIf->RepoId, zBaseDataLen);\
        memcpy(zpTmpNodeIf[0]->p_ExtraData, zShellBuf, zBaseDataLen);\
    } else {\
        zpTmpNodeIf[0]->FileId = -1;\
        zpTmpNodeIf[0]->p_ExtraData = NULL;\
    }\
\
    if (0 == zNodeCnter) {\
        zpTmpNodeIf[0]->p_father = NULL;\
        if (NULL == zpRootNodeIf) {\
            zpRootNodeIf = zpTmpNodeIf[0];\
        } else {\
            for (zpTmpNodeIf[2] = zpRootNodeIf; NULL != zpTmpNodeIf[2]->p_left; zpTmpNodeIf[2] = zpTmpNodeIf[2]->p_left) {}\
            zpTmpNodeIf[2]->p_left = zpTmpNodeIf[0];\
        }\
    } else {\
        zpTmpNodeIf[0]->p_father = zpTmpNodeIf[1];\
        if (NULL == zpTmpNodeIf[2]) {\
            zpTmpNodeIf[1]->p_FirstChild = zpTmpNodeIf[0];\
        } else {\
            zpTmpNodeIf[2]->p_left = zpTmpNodeIf[0];\
        }\
    }\
\
    zNodeCnter++;\
    for (; zNodeCnter < zpPcreRetIf->cnt; zNodeCnter++) {\
        zpTmpNodeIf[0]->p_FirstChild = zalloc_cache(zpMetaIf->RepoId, sizeof(zMetaInfo));\
        zpTmpNodeIf[1] = zpTmpNodeIf[0];\
        zpTmpNodeIf[0] = zpTmpNodeIf[0]->p_FirstChild;\
        zpTmpNodeIf[0]->p_father = zpTmpNodeIf[1];\
        zpTmpNodeIf[0]->p_FirstChild = NULL;\
        zpTmpNodeIf[0]->p_left = NULL;\
\
        zpTmpNodeIf[0]->LineNum = zLineCnter;  /* 横向偏移 */\
        zLineCnter++;  /* 每个节点会占用一行显示输出 */\
        zpTmpNodeIf[0]->OffSet = zNodeCnter;  /* 纵向偏移 */\
\
        zpTmpNodeIf[0]->p_data = zalloc_cache(zpMetaIf->RepoId, 6 * zpTmpNodeIf[0]->OffSet + 10 + 1 + strlen(zpPcreRetIf->p_rets[zNodeCnter]));\
        strcpy(zpTmpNodeIf[0]->p_data + 6 * zpTmpNodeIf[0]->OffSet + 10, zpPcreRetIf->p_rets[zNodeCnter]);\
\
        zpTmpNodeIf[0]->OpsId = 0;\
        zpTmpNodeIf[0]->RepoId = zpMetaIf->RepoId;\
        zpTmpNodeIf[0]->CommitId = zpMetaIf->CommitId;\
        zpTmpNodeIf[0]->CacheId = zppGlobRepoIf[zpMetaIf->RepoId]->CacheId;\
        zpTmpNodeIf[0]->DataType = zpMetaIf->DataType;\
\
        zpTmpNodeIf[0]->FileId = -1;  /* 中间的点节仅用作显示，不关联元数据 */\
        zpTmpNodeIf[0]->p_ExtraData = NULL;\
    }\
    zpTmpNodeIf[0]->FileId = zpTmpNodeIf[0]->LineNum;  /* 最后一个节点关联元数据 */\
    zpTmpNodeIf[0]->p_ExtraData = zalloc_cache(zpMetaIf->RepoId, zBaseDataLen);\
    memcpy(zpTmpNodeIf[0]->p_ExtraData, zShellBuf, zBaseDataLen);\
} while(0)

/* 差异文件数量 >128 时，调用此函数，以防生成树图损耗太多性能；此时无需检查无差的性况 */
void
zget_file_list_large(zMetaInfo *zpMetaIf, zVecWrapInfo *zpTopVecWrapIf, FILE *zpShellRetHandler, char *zpShellBuf, char *zpJsonBuf) {
    zMetaInfo zSubMetaIf;
    zBaseDataInfo *zpTmpBaseDataIf[3];
    _i zVecDataLen, zBaseDataLen, zCnter;

    for (zCnter = 0; NULL != zget_one_line(zpShellBuf, zCommonBufSiz, zpShellRetHandler); zCnter++) {
        zBaseDataLen = strlen(zpShellBuf);
        zpTmpBaseDataIf[0] = zalloc_cache(zpMetaIf->RepoId, sizeof(zBaseDataInfo) + zBaseDataLen);
        if (0 == zCnter) { zpTmpBaseDataIf[2] = zpTmpBaseDataIf[1] = zpTmpBaseDataIf[0]; }
        zpTmpBaseDataIf[0]->DataLen = zBaseDataLen;
        memcpy(zpTmpBaseDataIf[0]->p_data, zpShellBuf, zBaseDataLen);
        zpTmpBaseDataIf[0]->p_data[zBaseDataLen - 1] = '\0';

        zpTmpBaseDataIf[1]->p_next = zpTmpBaseDataIf[0];
        zpTmpBaseDataIf[1] = zpTmpBaseDataIf[0];
        zpTmpBaseDataIf[0] = zpTmpBaseDataIf[0]->p_next;
    }
    pclose(zpShellRetHandler);

    zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId) = zalloc_cache(zpMetaIf->RepoId, sizeof(zVecWrapInfo));
    zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->VecSiz = zCnter;
    zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_RefDataIf = zalloc_cache(zpMetaIf->RepoId, zCnter * sizeof(zRefDataInfo));
    zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf = zalloc_cache(zpMetaIf->RepoId, zCnter * sizeof(struct iovec));

    for (_i i = 0; i < zCnter; i++, zpTmpBaseDataIf[2] = zpTmpBaseDataIf[2]->p_next) {
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_RefDataIf[i].p_data = zpTmpBaseDataIf[2]->p_data;

        /* 用于转换成JsonStr */
        zSubMetaIf.OpsId = 0;
        zSubMetaIf.RepoId = zpMetaIf->RepoId;
        zSubMetaIf.CommitId = zpMetaIf->CommitId;
        zSubMetaIf.FileId = i;
        zSubMetaIf.CacheId = zppGlobRepoIf[zpMetaIf->RepoId]->CacheId;
        zSubMetaIf.DataType = zpMetaIf->DataType;
        zSubMetaIf.p_data = zpTmpBaseDataIf[2]->p_data;
        zSubMetaIf.p_ExtraData = NULL;

        /* 将zMetaInfo转换为JSON文本 */
        zconvert_struct_to_json_str(zpJsonBuf, &zSubMetaIf);

        zVecDataLen = strlen(zpJsonBuf);
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[i].iov_len = zVecDataLen;
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[i].iov_base = zalloc_cache(zpMetaIf->RepoId, zVecDataLen);
        memcpy(zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[i].iov_base, zpJsonBuf, zVecDataLen);

        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_RefDataIf[i].p_SubVecWrapIf = NULL;
    }

    /* 修饰第一项，形成二维json；最后一个 ']' 会在网络服务中通过单独一个 send 发过去 */
    ((char *)(zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[0].iov_base))[0] = '[';
}

void *
zget_file_list(void *zpIf) {
    zMetaInfo *zpMetaIf = (zMetaInfo *)zpIf;
    zVecWrapInfo *zpTopVecWrapIf;
    FILE *zpShellRetHandler;
    char zShellBuf[zCommonBufSiz], zJsonBuf[zCommonBufSiz];

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf);
    } else if (zIsDeployDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->DeployVecWrapIf);
    } else {
        zPrint_Err(0, NULL, "请求的数据类型错误!");
        return (void *) -1;
    }

    /* 必须在shell命令中切换到正确的工作路径 */
    sprintf(zShellBuf, "cd \"%s\" && git diff --shortstat \"%s\" \"%s\" | grep -oP '\\d+(?=\\s*file)' && git diff --name-only \"%s\" \"%s\"",
            zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath,
            zppGlobRepoIf[zpMetaIf->RepoId]->zLastDeploySig,
            zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId),
            zppGlobRepoIf[zpMetaIf->RepoId]->zLastDeploySig,
            zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId));

    zCheck_Null_Exit(zpShellRetHandler = popen(zShellBuf, "r"));

    /* 差异文件数量 >128 时使用 git 原生视图 */
    if (NULL == zget_one_line(zShellBuf, zCommonBufSiz, zpShellRetHandler)) {
        pclose(zpShellRetHandler);
        return (void *) -1;
    } else {
        if (128 < strtol(zShellBuf, NULL, 10)) {
            zget_file_list_large(zpMetaIf, zpTopVecWrapIf, zpShellRetHandler, zShellBuf, zJsonBuf);
            goto zMarkLarge;
        }
    }

    /* 差异文件数量 <=128 生成Tree图 */
    zMetaInfo zSubMetaIf;
    _i zVecDataLen, zBaseDataLen, zNodeCnter, zLineCnter;
    zMetaInfo *zpRootNodeIf, *zpTmpNodeIf[3];  // [0]：本体    [1]：记录父节点    [2]：记录兄长节点
    zPCREInitInfo *zpPcreInitIf;
    zPCRERetInfo *zpPcreRetIf;

    /* 在生成树节点之前分配空间，以使其不为 NULL，防止多个查询文件列的的请求导致重复生成同一缓存 */
    zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId) = zalloc_cache(zpMetaIf->RepoId, sizeof(zVecWrapInfo));
    zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->VecSiz = 0;  // 先赋为 0，知会同类请求缓存正在生成过程中

    zpRootNodeIf = NULL;
    zLineCnter = 0;
    zpPcreInitIf = zpcre_init("[^/]+");
    if (NULL != zget_one_line(zShellBuf, zCommonBufSiz, zpShellRetHandler)) {
        zBaseDataLen = strlen(zShellBuf);

        zShellBuf[zBaseDataLen - 1] = '/';  // 由于 '非' 逻辑匹配无法取得最后一个字符，此处为适为 pcre 临时添加末尾标识
        zpPcreRetIf = zpcre_match(zpPcreInitIf, zShellBuf, 1);
        zShellBuf[zBaseDataLen - 1] = '\0';  // 去除临时的多余字符

        zNodeCnter = 0;
        zpTmpNodeIf[2] = zpTmpNodeIf[1] = zpTmpNodeIf[0] = NULL;
        zGenerate_Tree_Node(); /* 添加树节点 */
        zpcre_free_tmpsource(zpPcreRetIf);

        while (NULL != zget_one_line(zShellBuf, zCommonBufSiz, zpShellRetHandler)) {
            zBaseDataLen = strlen(zShellBuf);

            zShellBuf[zBaseDataLen - 1] = '/';  // 由于 '非' 逻辑匹配无法取得最后一个字符，此处为适为 pcre 临时添加末尾标识
            zpPcreRetIf = zpcre_match(zpPcreInitIf, zShellBuf, 1);
            zShellBuf[zBaseDataLen - 1] = '\0';  // 去除临时的多余字符

            zpTmpNodeIf[0] = zpRootNodeIf;
            zpTmpNodeIf[2] = zpTmpNodeIf[1] = NULL;
            for (zNodeCnter = 0; zNodeCnter < zpPcreRetIf->cnt;) {
                do {
                    if (0 == strcmp(zpTmpNodeIf[0]->p_data + 6 * zpTmpNodeIf[0]->OffSet + 10, zpPcreRetIf->p_rets[zNodeCnter])) {
                        zpTmpNodeIf[1] = zpTmpNodeIf[0];
                        zpTmpNodeIf[0] = zpTmpNodeIf[0]->p_FirstChild;
                        zpTmpNodeIf[2] = NULL;
                        zNodeCnter++;
                        if (NULL == zpTmpNodeIf[0]) {
                            goto zMarkOuter;
                        } else {
                            goto zMarkInner;
                        }
                    }
                    zpTmpNodeIf[2] = zpTmpNodeIf[0];
                    zpTmpNodeIf[0] = zpTmpNodeIf[0]->p_left;
                } while (NULL != zpTmpNodeIf[0]);
                break;
zMarkInner:;
            }
zMarkOuter:;
            zGenerate_Tree_Node(); /* 添加树节点 */
            zpcre_free_tmpsource(zpPcreRetIf);
        }
    }
    zpcre_free_metasource(zpPcreInitIf);
    pclose(zpShellRetHandler);

    if (NULL == zpRootNodeIf) {
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_RefDataIf = NULL;
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf = zalloc_cache(zpMetaIf->RepoId, sizeof(struct iovec));

        zSubMetaIf.OpsId = 0;
        zSubMetaIf.RepoId = zpMetaIf->RepoId;
        zSubMetaIf.CommitId = zpMetaIf->CommitId;
        zSubMetaIf.FileId = -1;  // 置为 -1，不允许再查询下一级内容
        zSubMetaIf.CacheId = zppGlobRepoIf[zpMetaIf->RepoId]->CacheId;
        zSubMetaIf.DataType = zpMetaIf->DataType;
        zSubMetaIf.p_data = (0 == strcmp(zppGlobRepoIf[zpMetaIf->RepoId]->zLastDeploySig, zGet_OneCommitSig(zpTopVecWrapIf, zpMetaIf->CommitId))) ? "===> 最新的已布署版本 <===" : "=> 无差异 <=";
        zSubMetaIf.p_ExtraData = NULL;

        /* 将zMetaInfo转换为JSON文本 */
        zconvert_struct_to_json_str(zJsonBuf, &zSubMetaIf);
        zJsonBuf[0] = '[';  // 逗号替换为 '['

        zVecDataLen = strlen(zJsonBuf);
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[0].iov_len = zVecDataLen;
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[0].iov_base = zalloc_cache(zpMetaIf->RepoId, zVecDataLen);
        memcpy(zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[0].iov_base, zJsonBuf, zVecDataLen);

        /* 最后为 VecSiz 赋值，通知同类请求缓存已生成 */
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->VecSiz = 1;
    } else {
        /* 用于存储最终的每一行已格式化的文本 */
        zpRootNodeIf->pp_ResHash = zalloc_cache(zpMetaIf->RepoId, zLineCnter * sizeof(zMetaInfo *));

        /* Tree 图生成过程的并发控制 */
        zCcur_Init(zpMetaIf->RepoId, zLineCnter, A);
        zCcur_Sub_Config(zpRootNodeIf, A);
        zAdd_To_Thread_Pool(zdistribute_task, zpRootNodeIf);
        zCcur_Wait(A);

        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_RefDataIf 
            = zalloc_cache(zpMetaIf->RepoId, zLineCnter * sizeof(zRefDataInfo));
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf 
            = zalloc_cache(zpMetaIf->RepoId, zLineCnter * sizeof(struct iovec));

        for (_i i = 0; i < zLineCnter; i++) {
            zconvert_struct_to_json_str(zJsonBuf, zpRootNodeIf->pp_ResHash[i]); /* 将 zMetaInfo 转换为 json 文本 */

            zVecDataLen = strlen(zJsonBuf);
            zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[i].iov_len = zVecDataLen;
            zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[i].iov_base = zalloc_cache(zpMetaIf->RepoId, zVecDataLen);
            memcpy(zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[i].iov_base, zJsonBuf, zVecDataLen);

            zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_RefDataIf[i].p_data = zpRootNodeIf->pp_ResHash[i]->p_ExtraData;
            zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_RefDataIf[i].p_SubVecWrapIf = NULL;
        }

        /* 修饰第一项，形成二维json；最后一个 ']' 会在网络服务中通过单独一个 send 发过去 */
        ((char *)(zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->p_VecIf[0].iov_base))[0] = '[';

        /* 最后为 VecSiz 赋值，通知同类请求缓存已生成 */
        zGet_OneCommitVecWrapIf(zpTopVecWrapIf, zpMetaIf->CommitId)->VecSiz = zLineCnter;
    }

zMarkLarge:
    return NULL;
}

/*
 * 功能：逐层生成单个代码库的 commit/deploy 列表、文件列表及差异内容缓存
 * 当有新的布署或撤销动作完成时，所有的缓存都会失效，因此每次都需要重新执行此函数以刷新预载缓存
 */
void *
zgenerate_cache(void *zpIf) {
    zMetaInfo *zpMetaIf, zSubMetaIf;
    zVecWrapInfo *zpTopVecWrapIf, *zpSortedTopVecWrapIf;
    zBaseDataInfo *zpTmpBaseDataIf[3];
    _i zVecDataLen, zBaseDataLen, zCnter;

    FILE *zpShellRetHandler;
    char zRes[zCommonBufSiz], zShellBuf[zCommonBufSiz], zJsonBuf[zBytes(256)];

    zpMetaIf = (zMetaInfo *)zpIf;

    if (zIsCommitDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->CommitVecWrapIf);
        zpSortedTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->SortedCommitVecWrapIf);
        sprintf(zShellBuf, "cd \"%s\" && git log server --format=\"%%H_%%ct\"", zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath); // 取 server 分支的提交记录
        zCheck_Null_Exit(zpShellRetHandler = popen(zShellBuf, "r"));
    } else if (zIsDeployDataType == zpMetaIf->DataType) {
        zpTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->DeployVecWrapIf);
        zpSortedTopVecWrapIf = &(zppGlobRepoIf[zpMetaIf->RepoId]->SortedDeployVecWrapIf);
        // 调用外部命令 cat，而不是用 fopen 打开，如此可用统一的 pclose 关闭
        sprintf(zShellBuf, "cat \"%s\"\"%s\"", zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath, zLogPath);
        zCheck_Null_Exit(zpShellRetHandler = popen(zShellBuf, "r"));
    } else {
        zPrint_Err(0, NULL, "数据类型错误!");
        exit(1);
    }

    /* 第一行单独处理，避免后续每次判断是否是第一行 */
    zCnter = 0;
    if (NULL != zget_one_line(zRes, zCommonBufSiz, zpShellRetHandler)) {
        /* 只提取比最近一次布署版本更新的提交记录 */
        if ((zIsCommitDataType == zpMetaIf->DataType)
                && (0 == (strncmp(zppGlobRepoIf[zpMetaIf->RepoId]->zLastDeploySig, zRes, zBytes(40))))) { goto zMarkSkip; }
        zBaseDataLen = strlen(zRes);
        zpTmpBaseDataIf[0] = zalloc_cache(zpMetaIf->RepoId, sizeof(zBaseDataInfo) + zBaseDataLen);
        zpTmpBaseDataIf[0]->DataLen = zBaseDataLen;
        memcpy(zpTmpBaseDataIf[0]->p_data, zRes, zBaseDataLen);
        zpTmpBaseDataIf[0]->p_data[zBaseDataLen - 1] = '\0';

        zpTmpBaseDataIf[2] = zpTmpBaseDataIf[1] = zpTmpBaseDataIf[0];
        zpTmpBaseDataIf[1]->p_next = NULL;

        zCnter++;
        for (; (zCnter < zCacheSiz) && (NULL != zget_one_line(zRes, zCommonBufSiz, zpShellRetHandler)); zCnter++) {
            /* 只提取比最近一次布署版本更新的提交记录 */
            if ((zIsCommitDataType == zpMetaIf->DataType)
                    && (0 == (strncmp(zppGlobRepoIf[zpMetaIf->RepoId]->zLastDeploySig, zRes, zBytes(40))))) { goto zMarkSkip; }
            zBaseDataLen = strlen(zRes);
            zpTmpBaseDataIf[0] = zalloc_cache(zpMetaIf->RepoId, sizeof(zBaseDataInfo) + zBaseDataLen);
            zpTmpBaseDataIf[0]->DataLen = zBaseDataLen;
            memcpy(zpTmpBaseDataIf[0]->p_data, zRes, zBaseDataLen);
            zpTmpBaseDataIf[0]->p_data[zBaseDataLen - 1] = '\0';

            zpTmpBaseDataIf[1]->p_next = zpTmpBaseDataIf[0];
            zpTmpBaseDataIf[1] = zpTmpBaseDataIf[0];
        }
    }
zMarkSkip:
    pclose(zpShellRetHandler);

    /* 存储的是实际的对象数量 */
    zpSortedTopVecWrapIf->VecSiz = zpTopVecWrapIf->VecSiz = zCnter;

    if (0 != zCnter) {
        for (_i i = 0; i < zCnter; i++, zpTmpBaseDataIf[2] = zpTmpBaseDataIf[2]->p_next) {
            zpTmpBaseDataIf[2]->p_data[40] = '\0';

            /* 用于转换成JsonStr */
            zSubMetaIf.OpsId = 0;
            zSubMetaIf.RepoId = zpMetaIf->RepoId;
            zSubMetaIf.CommitId = i;
            zSubMetaIf.FileId = -1;
            zSubMetaIf.CacheId =  zppGlobRepoIf[zpMetaIf->RepoId]->CacheId;
            zSubMetaIf.DataType = zpMetaIf->DataType;
            zSubMetaIf.p_data = zpTmpBaseDataIf[2]->p_data;
            zSubMetaIf.p_ExtraData = &(zpTmpBaseDataIf[2]->p_data[41]);

            /* 将zMetaInfo转换为JSON文本 */
            zconvert_struct_to_json_str(zJsonBuf, &zSubMetaIf);

            zVecDataLen = strlen(zJsonBuf);
            zpTopVecWrapIf->p_VecIf[i].iov_len = zVecDataLen;
            zpTopVecWrapIf->p_VecIf[i].iov_base = zalloc_cache(zpMetaIf->RepoId, zVecDataLen);
            memcpy(zpTopVecWrapIf->p_VecIf[i].iov_base, zJsonBuf, zVecDataLen);

            zpTopVecWrapIf->p_RefDataIf[i].p_data = zpTmpBaseDataIf[2]->p_data;
            zpTopVecWrapIf->p_RefDataIf[i].p_SubVecWrapIf = NULL;
        }

        if (zIsDeployDataType == zpMetaIf->DataType) {
            // 存储最近一次布署的 SHA1 sig，执行布署是首先对比布署目标与最近一次布署，若相同，则直接返回成功
            strcpy(zppGlobRepoIf[zpMetaIf->RepoId]->zLastDeploySig, zpTopVecWrapIf->p_RefDataIf[zCnter - 1].p_data);
            /* 将布署记录按逆向时间排序（新记录显示在前面） */
            for (_i i = 0; i < zpTopVecWrapIf->VecSiz; i++) {
                zCnter--;
                zpSortedTopVecWrapIf->p_VecIf[zCnter].iov_base = zpTopVecWrapIf->p_VecIf[i].iov_base;
                zpSortedTopVecWrapIf->p_VecIf[zCnter].iov_len = zpTopVecWrapIf->p_VecIf[i].iov_len;
            }
        } else {
            /* 提交记录缓存本来就是有序的，不需要额外排序 */
            zpSortedTopVecWrapIf->p_VecIf = zpTopVecWrapIf->p_VecIf;
        }

        /* 修饰第一项，形成二维json；最后一个 ']' 会在网络服务中通过单独一个 send 发过去 */
        ((char *)(zpSortedTopVecWrapIf->p_VecIf[0].iov_base))[0] = '[';
    }

//    /* !!!!队列结构已经弃用!!!! 此后增量更新时，逆向写入，因此队列的下一个可写位置标记为最末一个位置 */
//    zppGlobRepoIf[zpMetaIf->RepoId]->CommitCacheQueueHeadId = zCacheSiz - 1;

    /* 防止意外访问导致的程序崩溃 */
    memset(zpTopVecWrapIf->p_RefDataIf + zpTopVecWrapIf->VecSiz, 0, sizeof(zRefDataInfo) * (zCacheSiz - zpTopVecWrapIf->VecSiz));

    /* >>>>任务完成，尝试通知上层调用者；若不是在新线程中执行，则不需要通知 */
    if (NULL != zpMetaIf->p_CondVar) { zCcur_Fin_Signal(zpMetaIf); }

    return NULL;
}

// 记录布署或撤销的日志
void
zwrite_log(_i zRepoId) {
// TEST:PASS
    char zShellBuf[zCommonBufSiz], zRes[zCommonBufSiz];
    FILE *zpFile;
    _i zLen;

    /* write last deploy SHA1_sig and it's timestamp to: <_SHADOW/log/meta> */
    sprintf(zShellBuf, "cd \"%s\" && git log \"%s\" -1 --format=\"%%H_%%ct\"",
            zppGlobRepoIf[zRepoId]->p_RepoPath,
            zppGlobRepoIf[zRepoId]->zLastDeploySig);
    zCheck_Null_Exit(zpFile = popen(zShellBuf, "r"));
    zget_one_line(zRes, zCommonBufSiz, zpFile);
    zLen = strlen(zRes);  // 写入文件时，不能写入最后的 '\0'

    if (zLen != write(zppGlobRepoIf[zRepoId]->LogFd, zRes, zLen)) {
        zPrint_Err(0, NULL, "日志写入失败： <_SHADOW/log/deploy/meta> !");
        exit(1);
    }
}

/************
 * INIT OPS *
 ************/
/*
 * 参数：
 *   新建项目基本信息五个字段
 *   初次启动标记(zInitMark: 1 表示为初始化时调用，0 表示动态更新时调用)
 * 返回值:
 *         -33：无法创建请求的项目路径
 *         -34：请求创建的新项目信息格式错误（合法字段数量不是五个）
 *         -35：请求创建的项目ID已存在或不合法（创建项目代码库时出错）
 *         -36：请求创建的项目路径已存在，且项目ID不同
 *         -37：请求创建项目时指定的源版本控制系统错误(!git && !svn)
 *         -38：拉取远程代码库失败（git clone 失败）
 *         -39：项目元数据创建失败，如：项目ID无法写入repo_id、无法打开或创建布署日志文件meta等原因
 */
#define zFree_Source() do {\
    free(zppGlobRepoIf[zRepoId]->p_RepoPath);\
    free(zppGlobRepoIf[zRepoId]);\
    zppGlobRepoIf[zRepoId] = NULL;\
    zMem_Re_Alloc(zppGlobRepoIf, zRepoInfo *, zGlobMaxRepoId + 1, zppGlobRepoIf);\
    zpcre_free_tmpsource(zpRetIf);\
    zpcre_free_metasource(zpInitIf);\
} while(0)

_i
zinit_one_repo_env(char *zpRepoMetaData) {
    zPCREInitInfo *zpInitIf;
    zPCRERetInfo *zpRetIf;

    zMetaInfo *zpMetaIf[2];
    char zShellBuf[zCommonBufSiz];

    _i zRepoId, zFd, zErrNo;

    /* 正则匹配项目基本信息（5个字段） */
    zpInitIf = zpcre_init("(\\w|[[:punct:]])+");
    zpRetIf = zpcre_match(zpInitIf, zpRepoMetaData, 1);
    if (5 != zpRetIf->cnt) {
        zPrint_Time();
        return -34;
    }

    /* 提取项目ID */
    zRepoId = strtol(zpRetIf->p_rets[0], NULL, 10);
    if (zRepoId > zGlobMaxRepoId) {
        zMem_Re_Alloc(zppGlobRepoIf, zRepoInfo *, zRepoId + 1, zppGlobRepoIf);
        for (_i i = zGlobMaxRepoId + 1; i < zRepoId; i++) {
            zppGlobRepoIf[i] = NULL;
        }
    } else {
        if (NULL != zppGlobRepoIf[zRepoId]) {
            zpcre_free_tmpsource(zpRetIf);
            zpcre_free_metasource(zpInitIf);
            return -35;
        }
    }

    /* 分配项目信息的存储空间，务必使用 calloc */
    zMem_C_Alloc(zppGlobRepoIf[zRepoId], zRepoInfo, 1);
    zppGlobRepoIf[zRepoId]->RepoId = zRepoId;

    /* 提取项目绝对路径 */
    zMem_Alloc(zppGlobRepoIf[zRepoId]->p_RepoPath, char, 1 + strlen("/home/git/") + strlen(zpRetIf->p_rets[1]));
    sprintf(zppGlobRepoIf[zRepoId]->p_RepoPath, "%s%s", "/home/git/", zpRetIf->p_rets[1]);

    /* 调用SHELL执行检查和创建，此处SHELL参数不能加引号 */
    sprintf(zShellBuf, "sh -x /home/git/zgit_shadow/scripts/zmaster_init_repo.sh %s", zpRepoMetaData);

    /* system 返回的是与 waitpid 中的 status 一样的值，需要用宏 WEXITSTATUS 提取真正的错误码 */
    zErrNo = WEXITSTATUS(system(zShellBuf));
    if (255 == zErrNo) {
        zFree_Source();
        return -36;
    } else if (254 == zErrNo) {
        zFree_Source();
        return -33;
    } else if (253 == zErrNo) {
        zFree_Source();
        return -38;
    }

    /* 打开日志文件 */
    char zPathBuf[zCommonBufSiz];
    sprintf(zPathBuf, "%s%s", zppGlobRepoIf[zRepoId]->p_RepoPath, zLogPath);
    zppGlobRepoIf[zRepoId]->LogFd = open(zPathBuf, O_WRONLY | O_CREAT | O_APPEND, 0755);

    sprintf(zPathBuf, "%s%s", zppGlobRepoIf[zRepoId]->p_RepoPath, zRepoIdPath);
    zFd = open(zPathBuf, O_WRONLY | O_TRUNC | O_CREAT, 0644);

    if (-1 == zFd || -1 == zppGlobRepoIf[zRepoId]->LogFd) {
        close(zFd);
        close(zppGlobRepoIf[zRepoId]->LogFd);
        zFree_Source();
        return -39;
    }

    /* 在每个代码库的<_SHADOW/info/repo_id>文件中写入所属代码库的ID */
    char zRepoIdBuf[12];  // 足以容纳整数最大值即可
    _i zRepoIdStrLen = sprintf(zRepoIdBuf, "%d", zRepoId);
    if (zRepoIdStrLen != write(zFd, zRepoIdBuf, zRepoIdStrLen)) {
        close(zFd);
        close(zppGlobRepoIf[zRepoId]->LogFd);
        zFree_Source();
        return -39;
    }
    close(zFd);

    /* 检测并生成项目代码定期更新命令 */
    char zPullCmdBuf[zCommonBufSiz];
    if (0 == strcmp("git", zpRetIf->p_rets[4])) {
        sprintf(zPullCmdBuf, "cd /home/git/\"%s\" && \\ls -a | grep -Ev '^(\\.|\\.\\.|\\.git)$' | xargs rm -rf; git stash; git pull --force \"%s\" \"%s\":server; rm -f .git/index.lock",
                zpRetIf->p_rets[1],
                zpRetIf->p_rets[2],
                zpRetIf->p_rets[3]);
    } else if (0 == strcmp("svn", zpRetIf->p_rets[4])) {
        sprintf(zPullCmdBuf, "cd /home/git/\"%s\"/.sync_svn_to_git && svn up && git add --all . && git commit -m \"_\" && git push --force ../.git master:server",
                zpRetIf->p_rets[1]);
    } else {
        close(zppGlobRepoIf[zRepoId]->LogFd);
        zFree_Source();
        return -37;
    }

    zMem_Alloc(zppGlobRepoIf[zRepoId]->p_PullCmd, char, 1 + strlen(zPullCmdBuf));
    strcpy(zppGlobRepoIf[zRepoId]->p_PullCmd, zPullCmdBuf);

    /* 清理资源占用 */
    zpcre_free_tmpsource(zpRetIf);
    zpcre_free_metasource(zpInitIf);

    /* 内存池初始化，开头留一个指针位置，用于当内存池容量不足时，指向下一块新开辟的内存区 */
    if (MAP_FAILED ==
            (zppGlobRepoIf[zRepoId]->p_MemPool = mmap(NULL, zMemPoolSiz, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0))) {
        zPrint_Time();
        fprintf(stderr, "mmap failed! RepoId: %d", zRepoId);
        exit(1);
    }
    void **zppPrev = zppGlobRepoIf[zRepoId]->p_MemPool;
    zppPrev[0] = NULL;
    zppGlobRepoIf[zRepoId]->MemPoolOffSet = sizeof(void *);
    zCheck_Pthread_Func_Exit(pthread_mutex_init(&(zppGlobRepoIf[zRepoId]->MemLock), NULL));

    /* 为每个代码库生成一把读写锁，锁属性设置写者优先 */
    zCheck_Pthread_Func_Exit(pthread_rwlockattr_init(&(zppGlobRepoIf[zRepoId]->zRWLockAttr)));
    zCheck_Pthread_Func_Exit(pthread_rwlockattr_setkind_np(&(zppGlobRepoIf[zRepoId]->zRWLockAttr), PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP));
    zCheck_Pthread_Func_Exit(pthread_rwlock_init(&(zppGlobRepoIf[zRepoId]->RwLock), &(zppGlobRepoIf[zRepoId]->zRWLockAttr)));
    zCheck_Pthread_Func_Exit(pthread_rwlockattr_destroy(&(zppGlobRepoIf[zRepoId]->zRWLockAttr)));

    /* 读写锁生成之后，立刻拿写锁 */
    pthread_rwlock_wrlock(&(zppGlobRepoIf[zRepoId]->RwLock));

    /* 用于统计布署状态的互斥锁 */
    zCheck_Pthread_Func_Exit(pthread_mutex_init(&zppGlobRepoIf[zRepoId]->ReplyCntLock, NULL));
    /* 用于保证 "git pull" 原子性拉取的互斥锁 */
    zCheck_Pthread_Func_Exit(pthread_mutex_init(&zppGlobRepoIf[zRepoId]->PullLock, NULL));

    /* 缓存版本初始化 */
    zppGlobRepoIf[zRepoId]->CacheId = 1000000000;
    /* 上一次布署结果状态初始化 */
    zppGlobRepoIf[zRepoId]->RepoState = zRepoGood;

    /* 提取最近一次布署的SHA1 sig，日志文件不会为空，初创时即会以空库的提交记录作为第一条布署记录 */
    sprintf(zShellBuf, "cat \"%s\"\"%s\" | tail -1", zppGlobRepoIf[zRepoId]->p_RepoPath, zLogPath);
    FILE *zpShellRetHandler = popen(zShellBuf, "r");
    if (zBytes(40) == zget_str_content(zppGlobRepoIf[zRepoId]->zLastDeploySig, zBytes(40), zpShellRetHandler)) {
        zppGlobRepoIf[zRepoId]->zLastDeploySig[40] = '\0';
    } else {
        zppGlobRepoIf[zRepoId]->RepoState = zRepoDamaged;
    }
    pclose(zpShellRetHandler);

    /* 指针指向自身的静态数据项 */
    zppGlobRepoIf[zRepoId]->CommitVecWrapIf.p_VecIf = zppGlobRepoIf[zRepoId]->CommitVecIf;
    zppGlobRepoIf[zRepoId]->CommitVecWrapIf.p_RefDataIf = zppGlobRepoIf[zRepoId]->CommitRefDataIf;
    zppGlobRepoIf[zRepoId]->SortedCommitVecWrapIf.p_VecIf = zppGlobRepoIf[zRepoId]->CommitVecIf;  // 提交记录总是有序的，不需要再分配静态空间

    zppGlobRepoIf[zRepoId]->DeployVecWrapIf.p_VecIf = zppGlobRepoIf[zRepoId]->DeployVecIf;
    zppGlobRepoIf[zRepoId]->DeployVecWrapIf.p_RefDataIf = zppGlobRepoIf[zRepoId]->DeployRefDataIf;
    zppGlobRepoIf[zRepoId]->SortedDeployVecWrapIf.p_VecIf = zppGlobRepoIf[zRepoId]->SortedDeployVecIf;

    /* 初始化任务分发环境 */
    zCcur_Init(zRepoId, 1, A);  //___
    zCcur_Fin_Mark(1 == 1, A);  //___
    zCcur_Init(zRepoId, 1, B);  //___
    zCcur_Fin_Mark(1 == 1, B);  //___

    /* 生成提交记录缓存 */
    zpMetaIf[0] = zalloc_cache(zRepoId, sizeof(zMetaInfo));
    zCcur_Sub_Config(zpMetaIf[0], A);  //___
    zpMetaIf[0]->RepoId = zRepoId;
    zpMetaIf[0]->DataType = zIsCommitDataType;
    zAdd_To_Thread_Pool(zgenerate_cache, zpMetaIf[0]);

    /* 生成布署记录缓存 */
    zpMetaIf[1] = zalloc_cache(zRepoId, sizeof(zMetaInfo));
    zCcur_Sub_Config(zpMetaIf[1], B);  //___
    zpMetaIf[1]->RepoId = zRepoId;
    zpMetaIf[1]->DataType = zIsDeployDataType;
    zAdd_To_Thread_Pool(zgenerate_cache, zpMetaIf[1]);

    /* 等待两批任务完成，之后释放相关资源占用 */
    zCcur_Wait(A);  //___
    zCcur_Wait(B);  //___

    zGlobMaxRepoId = zRepoId > zGlobMaxRepoId ? zRepoId : zGlobMaxRepoId;
    zppGlobRepoIf[zRepoId]->zInitRepoFinMark = 1;

    /* 放锁 */
    pthread_rwlock_unlock(&(zppGlobRepoIf[zRepoId]->RwLock));
    return 0;
}
#undef zFree_Source

/* 读取项目信息，初始化配套环境 */
void *
zinit_env(const char *zpConfPath) {
    FILE *zpFile;
    char zRes[zCommonBufSiz];
    _i zErrNo;

    /* json 解析时的回调函数索引 */
    zJsonParseOps['O']  // OpsId
        = zJsonParseOps['P']  // ProjId
        = zJsonParseOps['R']  // RevId
        = zJsonParseOps['F']  // FileId
        = zJsonParseOps['H']  // HostId
        = zJsonParseOps['C']  // CacheId
        = zJsonParseOps['D']  // DataType
        = zparse_digit;
    zJsonParseOps['d']  // data
        = zJsonParseOps['E']  // ExtraData
        = zparse_str;

    zCheck_Null_Exit(zpFile = fopen(zpConfPath, "r"));
    while (NULL != zget_one_line(zRes, zCommonBufSiz, zpFile)) {
        if (0 > (zErrNo = zinit_one_repo_env(zRes))) {
            fprintf(stderr, "ERROR[zinit_one_repo_env]: %d\n", zErrNo);
        }
    }

    if (0 > zGlobMaxRepoId) { zPrint_Err(0, NULL, "未读取到有效代码库信息!"); }

    fclose(zpFile);
    return NULL;
}

/* 通过 SSH 远程初始化一个目标主机，完成任务后通知上层调用者 */
void *
zinit_one_remote_host(void *zpIf) {
    zMetaInfo *zpMetaIf = (zMetaInfo *)zpIf;
    char zShellBuf[zCommonBufSiz];
    char zHostStrAddrBuf[16];

    zconvert_ipv4_bin_to_str(zpMetaIf->HostId, zHostStrAddrBuf);

    sprintf(zShellBuf, "sh -x /home/git/zgit_shadow/scripts/zhost_init_repo.sh \"%s\" \"%s\" \"%d\" \"%s\"",
            zppGlobRepoIf[zpMetaIf->RepoId]->ProxyHostStrAddr,
            zHostStrAddrBuf,
            zpMetaIf->RepoId,
            zppGlobRepoIf[zpMetaIf->RepoId]->p_RepoPath + 9);  // 去掉最前面的 "/home/git" 共计 9 个字符

    system(zShellBuf);

    zCcur_Fin_Signal(zpMetaIf);

    return NULL;
}
