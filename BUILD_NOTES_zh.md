# 改版 Moonlight（Ferry 远控控制端）：打包须知与注意事项

> 整理日期：2026-11-09（Asia/Tokyo）。本仓库是**公开仓库**，本文不含任何密码、token、内网地址。

## 简介与状态

这是 Ferry 项目里的远控「控制端」串流程序，基于上游 moonlight-stream/moonlight-qt，加了 Ferry 工具栏（细把手、画质、黑屏、静音、四边停靠）、与 Ferry 桌面端的 IPC、配对恢复和主机发现刷新等。被控端用官方 Sunshine。

- 分支：`ferry-remote`（默认分支）。最新代码提交 `e2f1277c`「Refresh Ferry host discovery and acknowledge pairing recovery before exit」。
- Ferry 0.4.7 安装包（Ferry 提交 3cbd078）使用的就是这个提交编出的 Moonlight.exe。
- 真机两台电脑串流、工具栏交互和动画流畅度仍待实测。
- 项目整体介绍和状态见 Ferry 主仓库（私有）的 `PROJECT_INTRO_zh.md`，所有程序总览见 `FERRY_ALL_PROJECTS_zh.md`。

## 环境

- Windows x64，Visual Studio（MSVC，本机为 VS Community 2026）
- Qt 6.12.0 `msvc2022_64`
- 子模块必须递归拉取：`git clone --recursive -b ferry-remote https://github.com/m558dfm7tz-cell/moonlight-qt.git`
  （SDL_GameControllerDB、moonlight-common-c 及其 enet/nanors、qmdnsengine）

## 构建

CI（`.github/workflows/build-ferry.yml`，推送 `ferry-remote` 自动跑，产物 `Moonlight-Windows-x64-ferry`）：

```bat
powershell ./setup-deps.ps1
set "PATH=<Qt>\6.12.0\msvc2022_64\bin;%PATH%"
scripts\build-arch.bat Release x64
:: 输出 build\deploy-x64-release\*
```

本机：同样用 Qt 6.12.0 msvc2022_64 做**完整干净重编**（qmake/nmake）。

## 放进 Ferry 安装包的注意事项

1. **必须完整干净重编**。曾经增量编译漏掉 `CliStream.qml` 等 QML 资源，导致远程一点就退出（Ferry d630dd5 坏包）。
2. 编好后把 `deploy-x64-release` 整个目录作为 Ferry 运行库 payload 的 `Moonlight\` 子目录。
3. 改了 Moonlight 后，要同步更新 Ferry 仓库 `installer/dependencies/moonlight-build.json` 里的 `commit`、`exe_sha256`、`exe_bytes`，否则 Ferry 出包脚本会报「Moonlight binary pin mismatch」。
4. Ferry 出包前会跑 `scripts/Test-MoonlightResources.ps1`，检查 5 个 QML 入口资源。
5. 配对相关约定：配对要等证书写进 Ferry 用的那份 Moonlight 配置才算成功；配对中不要重启 Sunshine；不要提前杀 Moonlight。
6. 推送只改文档的提交时，提交信息加 `[skip ci]`，避免白跑 Windows 构建。

## 已知问题

- 真实远控点击、热键、动画帧时序、跨显示器混合 DPI 未验收（ML7 报告）。
- 编译有 Qt 弃用提示和 Qt6EntryPoint 的 LNK4291 警告，不影响链接。
