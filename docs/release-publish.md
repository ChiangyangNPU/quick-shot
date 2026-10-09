# QuickShot 发布流程（GitHub / Gitee 双端）

> 更新：2026-10-09（v0.1.0 首发实测）
> 相关文档：[git-multi-remote.md](git-multi-remote.md)（双远程同步）

## 1. 总览

| 环节 | 在哪做 | 说明 |
|------|--------|------|
| 编译与打包 | GitHub Actions（`release.yml` 的 `release` job） | push `v*` tag 触发，双平台并行：Windows MinGW（zip）+ macOS arm64（DMG） |
| GitHub Release | GitHub Actions | 构建完自动发布（非草稿），产物由 CI 直接挂上 |
| Gitee Release | GitHub Actions（`release.yml` 的 `gitee` job） | 构建完成后自动把**同一份产物**传到 Gitee；**前置条件：仓库配置 `GITEE_TOKEN` 密钥** |
| 补发通道 | 本地（`deploy/gitee/publish_gitee_release.sh`） | CI 失败/超时或想立刻发完时使用，分钟级 |

CI 与本地**共用同一个脚本**（`deploy/gitee/publish_gitee_release.sh`），两条通路行为一致：
复用/创建 Gitee Release（说明取 tag 注释）→ 传附件（失败重试）→ 按实际下载字节数逐项校验，
可重复执行（同名同大小自动跳过）。

> 为什么 CI 里失败要红灯：`GITEE_TOKEN` 缺失或上传失败会直接 `exit 1`，避免出现
> 「CI 全绿但 Gitee 没产物」。失败时 GitHub Release 仍已发布，用本地脚本补发即可。
>
> 参考：同作者的 TMD 项目同样走「build → publish(GitHub) → Upload to Gitee」，其 v0.1.2
> 实测 Gitee 上传步骤约 50 秒完成；网络差时曾观察到小时级耗时，故 job 超时留了余量。

## 2. 发版步骤

```bash
# 0. 版本号：CMakeLists.txt 的 project(QuickShot VERSION x.y.z)，
#    必须与 tag 一致（release.yml 内有校验，不一致直接失败）
#    前置：GitHub 仓库已配置 GITEE_TOKEN 密钥（见 §4），否则 gitee job 会红灯

# 1. 提交改动
git add -A && git commit -m "chore: bump to vX.Y.Z"

# 2. 打【附注】标签，发布说明写在 -m 里（双端 Release 描述都取自它）
git tag -a vX.Y.Z -F notes.md --cleanup=verbatim   # 或 -m "…"
#    注意：必须 -a（附注标签）；轻量标签取不到说明文字，会退化成提交信息

# 3. 推代码与标签到两个远程
git push gitee master && git push github master:main
git push github vX.Y.Z && git push gitee vX.Y.Z     # 推 GitHub 才触发 CI

# 4. 等 CI：GitHub 仓库 → Actions → Release（双平台约 10-15 分钟）
#    两个 job：release（构建 + 发 GitHub Release）→ gitee（同步到 Gitee）
#    成功后两端各含：
#      QuickShot-Release-vX.Y.Z-Windows-x64.zip
#      QuickShot-Release-vX.Y.Z.dmg

# 5.（仅在 gitee job 失败/超时/想立刻发完时）本地补发
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
其余可不勾。令牌明文只在生成时展示一次，请自行妥善保存；不用了可在 Gitee 设置里删除。

**配置到 GitHub 仓库（CI 自动发布的前置条件，只需做一次）**：

> GitHub 仓库 → `Settings` → `Secrets and variables` → `Actions` → `New repository secret`
>
> | Name | Secret |
> |------|--------|
> | `GITEE_TOKEN` | Gitee 私人令牌 |

配置后，每次 push `v*` tag，`gitee` job 会自动完成 Gitee 发布。**未配置时该 job 直接红灯**
（错误信息里带配置路径与本地补发命令），GitHub Release 不受影响。

同一令牌也可用于本地补发：

```bash
GITEE_TOKEN=<私人令牌> bash deploy/gitee/publish_gitee_release.sh
# 或把令牌存文件后：bash deploy/gitee/publish_gitee_release.sh --token-file ~/.gitee_token
```

## 5. 常见问题

| 现象 | 原因与处理 |
|------|------------|
| CI 报 “Tag (vX.Y.Z) 与 CMakeLists.txt 版本不一致” | 版本号没改或 tag 打错，改 `CMakeLists.txt` 后重打 tag |
| macOS 打包步骤 1 秒即失败 | `deploy/mac/deploy_mac.sh` 在 git 索引里丢了可执行位；工作流已改为 `bash 脚本` 调用，另请确认 `git ls-files -s` 显示 100755 |
| CI 的 DMG 背景没有版本号 | macOS job 的 Pillow 安装失败（脚本已逐级回退 + 告警），DMG 会回退静态背景图，不影响安装包 |
| Gitee 附件上传失败 | 单附件上限 100MB；当前 DMG 约 93MB、zip 约 86MB，均在限内。CI 失败/超时用本地脚本补发 |
| macOS 首次打开被 Gatekeeper 拦 | 未做 Apple 公证，右键 →「打开」放行；录屏/录音需在系统设置里授权 |
