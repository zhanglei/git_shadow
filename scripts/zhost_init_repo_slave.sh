#!/bin/sh
zSlaveAddr=$1
zPathOnHost=$2

ssh $zSlaveAddr "
    if [[ 0 -ne \`ls -d $zPathOnHost 2>/dev/null | wc -l\` ]];then exit; fi &&
    mkdir -p ${zPathOnHost}_SHADOW &&
\
    cd ${zPathOnHost}_SHADOW &&
    git init . &&
    git config user.name "git_shadow" &&
    git config user.email "git_shadow@$x" &&
    git commit --allow-empty -m "__init__" &&
    git branch -f server &&
\
    cd $zPathOnHost &&
    git init . &&
    git config user.name "`basename $zPathOnHost`" &&
    git config user.email "`basename ${zPathOnHost}`@$x" &&
    git commit --allow-empty -m "__init__" &&
    git branch -f server
    " &&

scp /tmp/zhost_post-update.sh git@${zSlaveAddr}:${zPathOnHost}/.git/hooks/post-update &&

ssh $zSlaveAddr "
    eval sed -i 's%_PROJ_PATH%${zPathOnHost}%g' ${zPathOnHost}/.git/hooks/post-update &&
    eval sed -i 's%_MASTER_ADDR%__MASTER_ADDR%g' ${zPathOnHost}/.git/hooks/post-update &&
    eval sed -i 's%_MASTER_PORT%__MASTER_PORT%g' ${zPathOnHost}/.git/hooks/post-update &&
    chmod 0755 ${zPathOnHost}/.git/hooks/post-update
    "