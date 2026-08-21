# start-server.ps1 - 本地 HTTP 静态文件服务器（模拟更新服务器，供检查更新/下载测试用）
# 用法: powershell -ExecutionPolicy Bypass -File start-server.ps1 [-Port 8000]
# 服务根目录为本脚本所在目录，即 server\ 下的 version.json 和 zip 包
param(
    [int]$Port = 8000
)

$Root = Split-Path $MyInvocation.MyCommand.Path -Parent
$Listener = New-Object System.Net.HttpListener
$Listener.Prefixes.Add("http://127.0.0.1:$Port/")
$Listener.Start()

Write-Host "Serving: $Root"
Write-Host "URL:     http://127.0.0.1:$Port/version.json"
Write-Host "按 Ctrl+C 停止服务" -ForegroundColor Yellow

try {
    while ($Listener.IsListening) {
        try {
            $Ctx = $Listener.GetContext()
            $Req = $Ctx.Request
            $RelPath = $Req.Url.AbsolutePath.TrimStart('/')
            $FullPath = Join-Path $Root $RelPath

            if (Test-Path $FullPath -PathType Leaf) {
                $Ctx.Response.StatusCode = 200
                if ($RelPath -match '\.json$') {
                    $Ctx.Response.ContentType = "application/json; charset=utf-8"
                } else {
                    $Ctx.Response.ContentType = "application/octet-stream"
                }
                if ($Req.HttpMethod -eq "HEAD") {
                    # HEAD 只返回头信息（Content-Length 由 HttpListener 按文件长度设置）
                    $Ctx.Response.ContentLength64 = (Get-Item $FullPath).Length
                    Write-Host ("HEAD {0} -> 200" -f $RelPath)
                } else {
                    $Bytes = [System.IO.File]::ReadAllBytes($FullPath)
                    $Ctx.Response.ContentLength64 = $Bytes.Length
                    $Ctx.Response.OutputStream.Write($Bytes, 0, $Bytes.Length)
                    Write-Host ("{0} {1} -> 200 ({2} bytes)" -f $Req.HttpMethod, $RelPath, $Bytes.Length)
                }
            } else {
                $Ctx.Response.StatusCode = 404
                Write-Host ("{0} {1} -> 404" -f $Req.HttpMethod, $RelPath) -ForegroundColor Red
            }
            $Ctx.Response.Close()
        } catch {
            # 单个请求异常不影响服务继续运行
            Write-Host ("请求处理异常: {0}" -f $_.Exception.Message) -ForegroundColor Red
            try { $Ctx.Response.Close() } catch {}
        }
    }
} finally {
    $Listener.Stop()
}
