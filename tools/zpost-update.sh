#!/bin/sh
# 拉取server分支分代码到master分支；
# 通知中控机已收到代码；
export PATH="/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin"
export HOME="/home/git"
export zPathOnHost="__PROJ_PATH"

zProjName=`basename ${zPathOnHost}`
zProjOnLinePath=`dirname \`dirname ${zPathOnHost}\``

# 当前hook执行过程中要去掉执行权限，防止以下的git操作触发hook无限循环
chmod a-x ${zPathOnHost}/.git/hooks/post-update

# 清除可能存在的由于 git 崩溃残留的锁文件
rm -f ${zPathOnHost}/.git/index.lock
rm -f ${zPathOnHost}_SHADOW/.git/index.lock

cd ${zPathOnHost}_SHADOW
export GIT_DIR="${zPathOnHost}_SHADOW/.git"
\ls -a | grep -Ev '^(\.|\.\.|\.git)$' | xargs rm -rf
git stash
git stash clear
git pull --force ./.git server:master >/dev/null 2>&1
git reset -q --hard  #git reset -q --hard `git log -1 server --format=%H`
if [[ 0 -ne $? ]]; then exit 255; fi

cd $zPathOnHost
export GIT_DIR="${zPathOnHost}/.git"
\ls -a | grep -Ev '^(\.|\.\.|\.git)$' | xargs rm -rf
git stash
git stash clear
git pull --force ./.git server:master >/dev/null 2>&1
git reset -q --hard  #git reset -q --hard `git log -1 server --format=%H`

# 校验布署结果
zMasterSig=`git log master -1 --format=%H`
zServerSig=`git log server -1 --format=%H`
if [[ $zMasterSig != $zServerSig
    || 0 -ne `git status --short | wc -l`
    || `cat ${zPathOnHost}_SHADOW/.____dp-SHA1.res` != `find . -path './.git' -prune -o -type f -print | sort | xargs cat | sha1sum | grep -oP '^\S+'` ]]
then
    exit 255
fi

# 'B' 用于标识这是布署状态回复，'A' 用于标识远程主机初始化状态回复
cd ${zPathOnHost}_SHADOW  # 务必切换路径，回复脚本内用了相对路径
echo -e "\n\n[`date`]" >> /tmp/.____post-deploy.log 2>&1
sh -x ./tools/zclient_reply.sh "__MASTER_ADDR" "__MASTER_PORT" "B" "${zMasterSig}" >> /tmp/.____post-deploy.log 2>&1


######################################################################
# 采取换软链接的方式，避免推送大量代码过程中线上代码出现不一致的情况 #
######################################################################
rm -rf ${zProjOnLinePath}/${zProjName}  # 一次性使用，清理旧项目遗留的文件
rm -rf ${zProjOnLinePath}/${zProjName}_SHADOW  # 一次性使用，清理旧项目遗留的文件
# 临时切换至布署仓库工作区
ln -s ${zPathOnHost} ${zProjOnLinePath}/${zProjName}
rm -rf ${zPathOnHost}_OnLine
mkdir ${zPathOnHost}_OnLine
git clone $zPathOnHost/.git ${zPathOnHost}_OnLine
# 切换回线上仓库工作区
rm -rf ${zProjOnLinePath}/${zProjName}
ln -s ${zPathOnHost}_OnLine ${zProjOnLinePath}/${zProjName}

#######################################
# 弃用！git clone 比直接复制快2倍左右 #
#######################################
# cd $zPathOnHost
# # 首先复制新版本文件
# rm -rf ${zPathOnHost}_OnLineNew
# mkdir ${zPathOnHost}_OnLineNew
# find . -maxdepth 1 | grep -vE '(^|/)(\.|\.git)$' | xargs cp -R -t ${zPathOnHost}_OnLineNew/
# # 然后互换名称，同时后台新线程清除旧文件
# mv ${zPathOnHost}_OnLine ${zPathOnHost}_OnLineOld
# mv ${zPathOnHost}_OnLineNew ${zPathOnHost}_OnLine
# rm -rf ${zPathOnHost}_OnLineOld &
# # 最后重建软链接
# rm -rf ${zProjOnLinePath}/${zProjName}  # 一次性使用，清理旧项目遗留的文件
# rm -rf ${zProjOnLinePath}/${zProjName}_SHADOW  # 一次性使用，清理旧项目遗留的文件
# ln -sf ${zPathOnHost}_OnLine ${zProjOnLinePath}/${zProjName}

# 布署完成之后需要执行的动作：<项目名称.sh>
(cd $zPathOnHost && sh ${zPathOnHost}/____post-deploy.sh) &

# 如下部分用于保障相同 sig 可以连续布署，应对失败重试场景
cd $zPathOnHost
git commit --allow-empty -m "____Auto Commit By Deploy System____"
git push --force ./.git master:server >/dev/null 2>&1

# 自我更新
rm ${zPathOnHost}/.git/hooks/post-update
cp ${zPathOnHost}_SHADOW/tools/post-update ${zPathOnHost}/.git/hooks/post-update
chmod 0755 ${zPathOnHost}/.git/hooks/post-update

# 更新开机请求布署自身的脚本，设置为隐藏文件
mv ${zPathOnHost}_SHADOW/tools/____req-deploy.sh /home/git/.____req-deploy.sh
