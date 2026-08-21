# QuickShot 多远程仓库同步指南

## 1. 概述

QuickShot 代码同时托管在 **Gitee** 和 **GitHub** 两个远程仓库，本地 `master` 分支分别推送为两个平台的分支名：

| 远程名 | 地址 | 协议 | 推送目标分支 |
|--------|------|------|--------------|
| `gitee` | `https://gitee.com/chiangyangNPU/quick-shot.git` | HTTPS | `master` |
| `github` | `git@github.com:ChiangyangNPU/QuickShot.git` | SSH | `main` |

> 本地分支统一为 `master`，不需要改名。推送时通过 refspec 完成映射：
> Gitee 推 `master`，GitHub 推 `master:main`（本地 `master` → 远程 `main`）。

## 2. 远程配置

```bash
# 添加远程（已配置，重装/克隆后可参照）
git remote add gitee  https://gitee.com/chiangyangNPU/quick-shot.git
git remote add github git@github.com:ChiangyangNPU/QuickShot.git

# 查看当前配置
git remote -v
```

### 认证方式

| 远程 | 认证方式 | 说明 |
|------|----------|------|
| `gitee` | HTTPS + Windows 凭据管理器 | Gitee 凭据已存于系统凭据库，无需重复输入 |
| `github` | SSH（`id_ed25519`） | 公钥已注册在 GitHub 账户；SSH 可避免 HTTPS 凭据弹窗问题 |

## 3. 日常用法

| 命令 | 作用 |
|------|------|
| `git push gitee master` | 只推 Gitee（`master`） |
| `git push github` | 只推 GitHub（`master` 已跟踪 `github/main`） |
| `git pushall` | **一键同时推两个仓库** |

### pushall 别名

```bash
# 已配置：git config alias.pushall '!git push gitee master && git push github master:main'
git pushall
```

- `git push gitee master` 成功后才继续推 GitHub（`&&` 串联）。
- 两处均为 "Everything up-to-date" 即表示两边已同步。

## 4. 首次推送（已完成，参考）

```bash
# 1. 初始提交
git add -A
git commit -m "Initial commit: QuickShot"

# 2. 推 Gitee（master）
git push -u gitee master

# 3. 推 GitHub（本地 master → 远程 main）
git push -u github master:main
```

## 5. 常见问题排查

### 5.1 GitHub 认证失败

症状：`fatal: Authentication failed for 'https://github.com/...'`

原因：Git Credential Manager（GCM）的交互式认证流程可能被输入法 DLL 的调试日志污染
（日志形如 `warning: invalid credential line: ... tsf_oime.cpp ...`）。

解决：改用 **SSH** 认证（本机 `id_ed25519` 已注册 GitHub）：

```bash
git remote set-url github git@github.com:ChiangyangNPU/QuickShot.git
ssh -T git@github.com   # 输出 "Hi ChiangyangNPU!" 即认证成功
```

### 5.2 大文件超出 GitHub 100MB 上限

症状：`remote: error: File ... is 375.57 MB; this exceeds GitHub's file size limit of 100.00 MB`

原因：`third_party/onnxruntime/lib/onnxruntime.pdb`（375MB，onnxruntime 调试符号）超 GitHub 硬上限。
Gitee 仅警告（>50MB）但不拦截，GitHub 则硬性拒绝。

处理（已执行）：从仓库移除 PDB 并重写提交：

```bash
# 1. 修改 .gitignore，删除 "!third_party/onnxruntime/lib/*.pdb" 放行规则
# 2. 移出版本控制（磁盘文件保留，构建时重新生成）
git rm --cached third_party/onnxruntime/lib/*.pdb
git add .gitignore
git commit --amend        # 重写初始提交（无 PDB）
# 3. 重推两个仓库
git push -f gitee master
git push -u github master:main
```

> 注意：GitHub 会扫描**全部历史**，仅删除文件再提交无法通过，必须重写包含该文件的历史。
> PDB 为构建产物，移除后不影响编译、运行与自研代码调试。

### 5.3 国内网络无法访问 GitHub

症状：`Failed to connect to github.com port 443`

解决：开启代理/VPN 后，为 git 配置代理（以本地 Clash 端口为例）：

```bash
git config --global http.proxy http://127.0.0.1:7890
# 无需代理时移除：
# git config --global --unset http.proxy
```

SSH 通道（`git@github.com`）同样受网络影响，必要时可走 SSH-over-443：
在 `~/.ssh/config` 中添加：

```
Host github.com
  HostName ssh.github.com
  Port 443
```
