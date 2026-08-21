# deploy.ps1
param(
    [switch]$d,
    [switch]$r,
    [switch]$NoGpuAcceleration   # -NoGpuAcceleration: 不打包 GPU 加速依赖 (DirectML.dll)
)

$ErrorActionPreference = "Stop"

# --- Configuration ---
$ProjectName = "QuickShot"

# 获取脚本所在目录
$ScriptDir = Split-Path $MyInvocation.MyCommand.Path -Parent
# 项目根目录 (deploy/win/ -> ../../)
$ProjectRoot = Resolve-Path (Join-Path $ScriptDir "..\..")

# 从 CMakeLists.txt 自动读取版本号 (project(QuickShot VERSION x.y.z))
$CmakeListsPath = Join-Path $ProjectRoot "CMakeLists.txt"
if (-not (Test-Path $CmakeListsPath)) {
    Write-Error "CMakeLists.txt not found at: $CmakeListsPath"
    exit 1
}
$CmakeContent = Get-Content $CmakeListsPath -Raw
if ($CmakeContent -match 'project\(\s*' + [regex]::Escape($ProjectName) + '\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
    $Version = $Matches[1]
    Write-Host "Version read from CMakeLists.txt: $Version" -ForegroundColor Green
} else {
    Write-Error "Failed to parse version from CMakeLists.txt. Expected pattern: project($ProjectName VERSION x.y.z)"
    exit 1
}

# 平台标识
$PlatformSuffix = "Windows-x64"

# Paths (Adjust based on your system if needed, but these match the discovered layout)
$QtRoot = "C:\Software\Qt"
$QtVersion = "6.10.2"
$QtKit = "mingw_64"

# Tool Paths
$QtBinDir = "$QtRoot\$QtVersion\$QtKit\bin"
# Note: Adjust mingw version directory if different on target machine
$MingwBinDir = "$QtRoot\Tools\mingw1310_64\bin" 
$CMakeBinDir = "$QtRoot\Tools\CMake_64\bin"
$NinjaDir = "$QtRoot\Tools\Ninja"

# Verify Paths
$RequiredPaths = @($QtBinDir, $MingwBinDir, $CMakeBinDir)
foreach ($Path in $RequiredPaths) {
    if (-not (Test-Path $Path)) {
        Write-Error "Required path not found: $Path"
        exit 1
    }
}

# Add to PATH (Temporary for this session)
$env:Path = "$QtBinDir;$MingwBinDir;$CMakeBinDir;$NinjaDir;$env:Path"

# Check tools
Write-Host "Checking tools..."
cmake --version | Select-Object -First 1
g++ --version | Select-Object -First 1
windeployqt --version | Select-Object -First 1

# --- Build Function ---
function Build-Config {
    param (
        [string]$Config
    )

    $BuildDir = Join-Path $ScriptDir "build_$Config"
    $PackageName = "$ProjectName-$Config-v$Version-$PlatformSuffix"
    $OutputDir = Join-Path $ScriptDir $PackageName

    Write-Host "`n========================================" -ForegroundColor Cyan
    Write-Host "Building $Config Configuration" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan

    # Clean previous build and output
    if (Test-Path $BuildDir) { 
        Write-Host "Cleaning build directory..."
        Remove-Item -Recurse -Force $BuildDir 
    }
    if (Test-Path $OutputDir) { 
        Write-Host "Cleaning output directory..."
        Remove-Item -Recurse -Force $OutputDir 
    }

    # Create directories
    New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

    # Configure
    Write-Host "Configuring CMake..."
    # Prefer Ninja if available, else MinGW Makefiles
    $Generator = "MinGW Makefiles"
    if (Test-Path "$NinjaDir\ninja.exe") {
        $Generator = "Ninja"
    }
    Write-Host "Using Generator: $Generator"

    # We pass CMAKE_PREFIX_PATH explicitly to ensure it finds the right Qt
    $QtPrefixPath = "$QtRoot\$QtVersion\$QtKit"
    
    # Use splatting for arguments
    # Note: Source directory is the project root
    $CMakeArgs = @(
        "-S", $ProjectRoot,
        "-B", $BuildDir,
        "-G", $Generator,
        "-DCMAKE_BUILD_TYPE=$Config",
        "-DCMAKE_PREFIX_PATH=$QtPrefixPath"
    )
    if ($NoGpuAcceleration) {
        $CMakeArgs += "-DENABLE_OCR_GPU_ACCELERATION=OFF"
        Write-Host "GPU acceleration disabled"
    }
    
    Write-Host "Running: cmake $CMakeArgs"
    & cmake $CMakeArgs
    if ($LASTEXITCODE -ne 0) { throw "CMake Configure failed" }
    
    # Build
    Write-Host "Building..."
    & cmake --build $BuildDir
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }

    # Deploy
    Write-Host "Deploying..."
    
    # Find the executable
    $ExeFile = Get-ChildItem -Path $BuildDir -Recurse -Filter "$ProjectName.exe" | Select-Object -First 1
    
    if (-not $ExeFile) {
        throw "Executable $ProjectName.exe not found in $BuildDir"
    }

    Write-Host "Found executable at: $($ExeFile.FullName)"

    # Copy Executable to Output
    $DestExe = Join-Path $OutputDir $ExeFile.Name
    Copy-Item $ExeFile.FullName -Destination $DestExe

    # Run windeployqt
    Write-Host "Running windeployqt..."
    
    $WindeployArgs = @("--dir", $OutputDir, "--compiler-runtime", $DestExe)
    
    if ($Config -eq "Debug") {
        # Check if Qt6Cored.dll exists, if not, use release libs
        if (Test-Path "$QtBinDir\Qt6Cored.dll") {
             $WindeployArgs += "--debug"
        } else {
             Write-Warning "Debug Qt libraries not found. Using Release libraries for deployment."
             $WindeployArgs += "--release"
        }
    } else {
        $WindeployArgs += "--release"
    }

    & windeployqt $WindeployArgs
    if ($LASTEXITCODE -ne 0) { Write-Warning "windeployqt returned exit code $LASTEXITCODE" }

    # Copy Language Files
    Write-Host "Copying language files..."
    $LanguagesSource = Join-Path $ProjectRoot "src\languages"
    $LanguagesDest = Join-Path $OutputDir "languages"
    New-Item -ItemType Directory -Force -Path $LanguagesDest | Out-Null
    Copy-Item -Path "$LanguagesSource\*" -Destination $LanguagesDest -Recurse

    # Copy ONNX Runtime DLL and related DLLs
    Write-Host "Copying ONNX Runtime..."
    $OnnxLibDir = Join-Path $ProjectRoot "third_party\onnxruntime\lib"
    $OnnxruntimeDll = Join-Path $OnnxLibDir "onnxruntime.dll"
    if (Test-Path $OnnxruntimeDll) {
        Copy-Item $OnnxruntimeDll -Destination $OutputDir
        Write-Host "ONNX Runtime DLL copied."
    } else {
        Write-Warning "ONNX Runtime DLL not found at: $OnnxruntimeDll"
    }

    # Copy DirectML DLL (required for GPU acceleration)
    if (-not $NoGpuAcceleration) {
        $DirectmlDll = Join-Path $OnnxLibDir "DirectML.dll"
        if (Test-Path $DirectmlDll) {
            Copy-Item $DirectmlDll -Destination $OutputDir
            Write-Host "DirectML DLL copied."
        } else {
            Write-Warning "DirectML DLL not found at: $DirectmlDll"
        }

        # Copy ONNX Runtime providers shared DLL
        $ProvidersDll = Join-Path $OnnxLibDir "onnxruntime_providers_shared.dll"
        if (Test-Path $ProvidersDll) {
            Copy-Item $ProvidersDll -Destination $OutputDir
            Write-Host "ONNX Runtime providers shared DLL copied."
        } else {
            Write-Warning "ONNX Runtime providers shared DLL not found at: $ProvidersDll"
        }
    } else {
        Write-Host "Skipping DirectML DLLs (GPU acceleration disabled)."
    }

    # Copy VC++ Runtime DLLs (required by onnxruntime.dll which is built with MSVC)
    Write-Host "Copying VC++ Runtime DLLs..."
    $SystemDir = "$env:SystemRoot\System32"
    $VcDlls = @("msvcp140.dll", "msvcp140_1.dll", "vcruntime140.dll", "vcruntime140_1.dll")
    foreach ($VcDll in $VcDlls) {
        $Src = Join-Path $SystemDir $VcDll
        if (Test-Path $Src) {
            Copy-Item $Src -Destination $OutputDir
        } else {
            Write-Warning "VC++ Runtime DLL not found: $Src"
        }
    }
    Write-Host "VC++ Runtime DLLs copied."

    # Copy OCR Model Files
    Write-Host "Copying OCR model files..."
    $ModelsSource = Join-Path $ProjectRoot "models"
    $ModelsDest = Join-Path $OutputDir "models"
    if (Test-Path $ModelsSource) {
        New-Item -ItemType Directory -Force -Path (Join-Path $ModelsDest "ocr") | Out-Null
        # 始终复制 mobile 模型
        $MobileSource = Join-Path $ModelsSource "ocr\mobile"
        if (Test-Path $MobileSource) {
            Copy-Item -Path $MobileSource -Destination (Join-Path $ModelsDest "ocr") -Recurse
            Write-Host "Mobile OCR models copied."
        }
    } else {
        Write-Warning "Models directory not found at: $ModelsSource"
    }

    # Cleanup Build Directory
    if (Test-Path $BuildDir) {
        Write-Host "Cleaning up build directory: $BuildDir"
        Remove-Item -Recurse -Force $BuildDir
    }

    Write-Host "Package created at: $OutputDir" -ForegroundColor Green
}

# --- Main Execution ---
try {
    if ($d -and $r) {
        Write-Host "Error: Cannot specify both -d and -r parameters." -ForegroundColor Red
        exit 1
    } elseif ($d) {
        Build-Config "Debug"
    } elseif ($r) {
        Build-Config "Release"
    } else {
        # Default behavior: build both
        Build-Config "Debug"
        Build-Config "Release"
    }
    Write-Host "`nBuild(s) completed successfully!" -ForegroundColor Green
} catch {
    Write-Error "Script failed: $_"
    exit 1
}