#!/usr/bin/env bash

zInitEnv() {
    zProjName=$1
    zSvnServPath=/home/git/svn_$zProjName  #Subversion repo to receive code from remote developers
    zSyncPath=/home/git/sync_$zProjName  #Sync snv repo to git repo
    zDeployPath=/home/git/$zProjName #Used to deploy code! --CORE--
    zSshKeyPath=$zSyncPath/.git_shadow/authorized_keys  #store Control Host and major ECSs' SSH pubkeys

    cp -r ../demo/$zProjName /home/git/

    #Init Subversion Server
    rm -rf $zSvnServPath
    mkdir -p $zSvnServPath
    svnadmin create $zSvnServPath
    perl -p -i.bak -e 's/^#\s*anon-access\s*=.*$/anon-access = write/' $zSvnServPath/conf/svnserve.conf
    svnserve --listen-port=$2 -d -r $zSvnServPath

    #Init svn repo
    svn co svn://10.30.2.126:$2/ $zSyncPath
    svn propset svn:ignore '.git
    .gitignore' $zSyncPath

    #Init Sync Git Env
    mkdir -p $zSyncPath/.git_shadow
    touch $zSshKeyPath
    chmod 0600 $zSshKeyPath

    cd $zSyncPath
    git init .
    echo ".svn" > .gitignore
    git config --global user.email git_shadow@yixia.com
    git config --global user.name git_shadow

    touch README
    git add --all .
    git commit -m "INIT"
    git branch -M master sync_git  # 此git的作用是将svn库代码转换为git库代码

    #Init Deploy Git Env
    mkdir -p $zDeployPath
    cd $zDeployPath
    git init .
    touch README
    git add --all .
    git commit -m "INIT"
    git tag CURRENT
    git branch server  #Act as Git server
}

killall svnserve
rm -rf /home/git/*
zInitEnv miaopai 50000
#yes|ssh-keygen -t rsa -P '' -f /home/git/.ssh/id_rsa

mkdir -p /tmp/miaopai
cd /tmp/miaopai
rm -rf .svn
svn co svn://10.30.2.126:50000
cp /etc/* ./ 2>/dev/null
svn add *
svn commit -m "etc files"
svn up

cd $zSyncPath
svn update
git add --all .
git commit -m \"\$1:\$2\"
git push --force $zDeployPath/.git sync_git:server

cd $zDeployPath \n\
git pull --force ./.git server:master

#zInitEnv yizhibo 60000
