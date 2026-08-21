@echo off
rem 以「本地更新服务器」模式启动旧版应用（1.0.0）
rem 设置 QUICKSHOT_UPDATE_URL 后，检查更新/下载全部指向本地 http://127.0.0.1:8000
rem 使用前提：已先运行 server\start-server.ps1
set QUICKSHOT_UPDATE_URL=http://127.0.0.1:8000/version.json
start "" "%~dp0app-v1.0.0\QuickShot.exe"
