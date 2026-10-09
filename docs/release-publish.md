# QuickShot 发布流程（GitHub / Gitee 双端）

> 更新：2026-10-09（v0.1.0 首发实测）
> 相关文档：[git-multi-remote.md](git-multi-remote.md)（双远程同步）

## 1. 总览

| 环节 | 在哪做 | 说明 |
|------|--------|------|
| 编译与打包 | GitHub Actions（`release.yml`） | push `v*` tag 触发，双平台并行：Windows MinGW（zip）+ macOS arm64（DMG） |
| GitHub Release | GitHub Actions | 构建完自动发布（非草稿），产物由 CI 直接挂上 |
| Gitee Release | **本地快速通道**（`deploy/gitee/publish_gitee_release.sh`） | 用本地网络把**同一份 CI 产物**传到 Gitee（分钟级） |

**为什么 Gitee 不走 CI**：QuickShot 的 `release.yml` 没有 Gitee 上传步骤；即使补上，也需要在
GitHub 仓库配置 `GITEE_TOKEN` 密钥，且 GitHub runner 在海外，直传 Gitee 实测需数小时
（同作者的 TMD 项目实测约 3.7 小时 / 170MB）。本地通道分钟级完成，产物与 GitHub 完全一致。

## 2. 发版步骤

```bash
# 0. 版本号：CMakeLists.txt 的 project(QuickShot VERSION x.y.z)，
#    必须与 tag 一致（release.yml 内有校验，不一致直接失败）

# 1. 提交改动
git add -A && git commit -m "chore: bump to vX.Y.Z"

# 2. 打【附注】标签，发布说明写在 -m 里（双端 Release 描述都取自它）
git tag -a vX.Y.Z -F notes.md --cleanup=verbatim   # 或 -m "…"
#    注意：必须 -a（附注标签）；轻量标签取不到说明文字，会退化成提交信息

# 3. 推代码与标签到两个远程
git push gitee master && git push github master:main
git push github vX.Y.Z && git push gitee vX.Y.Z     # 推 GitHub 才触发 CI

# 4. 等 CI：GitHub 仓库 → Actions → Release（双平台约 10-15 分钟）
#    成功后 GitHub Release 应含：
#      QuickShot-Release-vX.Y.Z-Windows-x64.zip
#      QuickShot-Release-vX.Y.Z.dmg

# 5. 发 Gitee（本地快速通道）
GITEE_TOKEN=<私人令牌> bash deploy/gitee/publish_gitee_release.sh
#    默认取最新 tag；产物自动从 GitHub Release 下载并校验字节数
#    可重复执行：已存在且大小一致的附件会跳过
```

## 3. 发布说明的组织方式

- 文案来源：**tag 注释**（`git tag -a` 的 `-m`/`-F` 内容）——随 tag 走，无需维护额外文件
- `release.yml` 的 `Prepare release notes` 步骤会显式按 ref 拉取 tag 对象（actions/checkout
  对 tag 事件按 sha 浅拉取，本地是轻量 tag，不显式 fetch 会取到提交信息），末尾自动附
  `**Full Changelog**` 链接
- 应用内的更新弹窗读取的正是 Release 的 `body` 字段，所以文案质量直接影响用户体验
- Markdown 标题行（`#` 开头）在 `git tag -F` 下会被默认清理规则吃掉，需加 `--cleanup=verbatim`

## 4. Gitee 令牌（`GITEE_TOKEN`）怎么来

路径：Gitee → 设置 → 私人令牌 → 生成新令牌，权限勾选 `projects`（发布 Release 够用），
其余可不勾。该令牌用于：

- 本地通道脚本（`--token-file` 或环境变量 `GITEE_TOKEN`）
- 将来若要把 Gitee 上传搬进 CI：在 **GitHub 仓库** Settings → Secrets and variables →
  Actions 添加同名密钥 `GITEE_TOKEN`，再给 `release.yml` 加一个上传 job（参考 TMD 项目）

> 令牌明文只在生成时展示一次，请自行妥善保存；不用了可在 Gitee 设置里删除。

## 5. 常见问题

| 现象 | 原因与处理 |
|------|------------|
| CI 报 “Tag (vX.Y.Z) 与 CMakeLists.txt 版本不一致” | 版本号没改或 tag 打错，改 `CMakeLists.txt` 后重打 tag |
| macOS 打包步骤 1 秒即失败 | `deploy/mac/deploy_mac.sh` 在 git 索引里丢了可执行位；工作流已改为 `bash 脚本` 调用，另请确认 `git ls-files -s` 显示 100755 |
| CI 的 DMG 背景没有版本号 | macOS job 的 Pillow 安装失败（脚本已逐级回退 + 告警），DMG 会回退静态背景图，不影响安装包 |
| Gitee 附件上传失败 | 单附件上限 100MB；当前 DMG 约 93MB、zip 约 86MB，均在限内。跨国/网络抖动可重跑脚本 |
| macOS 首次打开被 Gatekeeper 拦 | 未做 Apple 公证，右键 →「打开」放行；录屏/录音需在系统设置里授权 |
