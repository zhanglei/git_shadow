#ifndef _Z
    #include "../../../inc/zutils.h"
#endif

#define PCRE2_CODE_UNIT_WIDTH 8  //must define this before pcre2.h
#include <pcre2.h>  //compile with '-lpcre2-8'

#define zMatchLimit 64
#define zErrBufLen 256

struct zPCRERetInfo{
    char *p_rets[zMatchLimit];  //matched results
    _i cnt;         //total num of matched substrings
//  char *p_NewSubject;      //store the new subject after substitution
};
typedef struct zPCRERetInfo zPCRERetInfo;

struct zPCREInitInfo {
    pcre2_code *p_pd;
    pcre2_match_data *p_MatchData;
};
typedef struct zPCREInitInfo zPCREInitInfo;

void
zpcre_get_err(const _i zErrNo) {
// TEST: PASS
    PCRE2_UCHAR zBuffer[zErrBufLen];
    pcre2_get_error_message(zErrNo, zBuffer, zSizeOf(zBuffer));
    zPrint_Err(errno, NULL, (char *)zBuffer);
    exit(1);
}

zPCREInitInfo *
zpcre_init(const char *zpPCREPattern) {
// TEST: PASS
    zPCREInitInfo *zpPCREInitIf;
    zMem_Alloc(zpPCREInitIf, zPCREInitInfo, 1);

    PCRE2_SPTR zPattern = (PCRE2_SPTR)zpPCREPattern;

    _i zErrNo;
    PCRE2_SIZE zErrOffset;

    zpPCREInitIf->p_pd = pcre2_compile(zPattern, PCRE2_ZERO_TERMINATED, 0, &zErrNo, &zErrOffset, NULL);
    if (NULL == zpPCREInitIf->p_pd) {
        zpcre_get_err(zErrNo);
    }

    zpPCREInitIf->p_MatchData = pcre2_match_data_create_from_pattern(zpPCREInitIf->p_pd, NULL);
    return zpPCREInitIf;
}

zPCRERetInfo *
zpcre_match(const zPCREInitInfo *zpPCREInitIf, const char *zpPCRESubject, const _i zMatchAllMark) {
// TEST: PASS
    _i zRetCnt, zMatchMax;

    zPCRERetInfo *zpRetIf;
    zMem_C_Alloc(zpRetIf, zPCRERetInfo, 1);

    size_t zSubjectLen = strlen(zpPCRESubject);
    PCRE2_SPTR zSubject = (PCRE2_SPTR)zpPCRESubject;

    PCRE2_SIZE *zpRetVector;

    zpRetIf->cnt = 0;

    zMatchMax = (zMatchAllMark == 1) ? zMatchLimit : 1;
    //zMatchAllMark == 1 means that it will act like 'm//g' in perl

    for (_i i = 0; i < zMatchMax; i++) {
        zRetCnt = pcre2_match(zpPCREInitIf->p_pd, zSubject, zSubjectLen, 0, 0, zpPCREInitIf->p_MatchData, NULL);
        if (zRetCnt < 0) {
        //zRetCnt == 0 means space is not enough, you need check it
        //when use pcre2_match_data_create instead of the one whih suffix '_pattern'
            if (zRetCnt == PCRE2_ERROR_NOMATCH) {
                break;
            } else {
                zpcre_get_err(zRetCnt);
            }
        }

        zpRetVector = pcre2_get_ovector_pointer(zpPCREInitIf->p_MatchData);
        if (zpRetVector[0] == zpRetVector[1] || strlen((char *)zSubject) < zpRetVector[1]) {
            break;
        }

        PCRE2_SPTR zSubStringStart = zSubject + zpRetVector[0];
        size_t zSubStringLen = zpRetVector[1] - zpRetVector[0];  // Maybe add 1?

        zMem_C_Alloc(zpRetIf->p_rets[i], char, zSubStringLen);  // Must use calloc !!!
        strncpy(zpRetIf->p_rets[i], (char *)zSubStringStart, zSubStringLen);
        zpRetIf->cnt += 1;
        zSubject += zpRetVector[1];
    }

//  zRetIf->p_NewSubject = NULL;
    return zpRetIf;
}

// zPCRERetInfo *
// zpcre_substitude(PCRE2_SPTR zPattern, PCRE2_SPTR zSubject, _i zMatchAllMark) {
//     ;  //TO DO
// }

void
zpcre_free_tmpsource(zPCRERetInfo *zpRet) {
// TEST: PASS
    if (NULL == zpRet) { return; }
    for (_i i = 0; i < zpRet->cnt; i++) {
        if (NULL == zpRet->p_rets[i]) { continue; }
        free(zpRet->p_rets[i]);
        zpRet->p_rets[i] = NULL;
    }
    free(zpRet);
}

void
zpcre_free_metasource(zPCREInitInfo *zpInitIf) {
// TEST: PASS
    pcre2_match_data_free(zpInitIf->p_MatchData);
    pcre2_code_free(zpInitIf->p_pd);
    free(zpInitIf);
}

#undef zErrBufLen
#undef zMatchLimit