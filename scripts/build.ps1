# ──────────────────────────────────────────────────────────────
# SSA – Steam Server ADMIN  ·  Automated build script (Windows)
# ──────────────────────────────────────────────────────────────
#Requires -Version 5.1
param(
    [ValidateSet("Release","Debug","RelWithDebInfo","MinSizeRel")]
    [string]$BuildType = "Release"
)

$ErrorActionPreference = 'Continue'
$BuildDir = "build"
$ExitCode = 0

function Info  { param([string]$msg) Write-Host ">> $msg" -ForegroundColor Cyan }
function Warn  { param([string]$msg) Write-Host "!! $msg" -ForegroundColor Yellow }
function Fatal { param([string]$msg, [string]$LogFile)
    Write-Host "!! $msg" -ForegroundColor Red
    if ($LogFile -and $LogFile -ne "" -and (Test-Path $LogFile)) {
        Add-Content -Path $LogFile -Value $msg
    }
}

# ── 0. Set up logging ────────────────────────────────────────
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$LogDir      = Join-Path $ProjectRoot "logs"
if (-not (Test-Path $LogDir)) { New-Item -ItemType Directory -Path $LogDir | Out-Null }

$ConfigureLog = Join-Path $LogDir "configure.log"
$BuildLog     = Join-Path $LogDir "build.log"
$TestLog      = Join-Path $LogDir "test.log"
$FullLog      = Join-Path $LogDir "full.log"

# Initialize every log file with a header so they are never empty
$Timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
foreach ($lf in @($ConfigureLog, $BuildLog, $TestLog, $FullLog)) {
    Set-Content -Path $lf -Value "=== SSA Build - $Timestamp - $BuildType ==="
}

# Start a transcript to capture ALL console output to full.log
try { Stop-Transcript -ErrorAction SilentlyContinue } catch {}
Start-Transcript -Path $FullLog -Append

try {

Info "Logs will be written to: $LogDir"

# ── 1. Check for cmake ───────────────────────────────────────
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    $msg = "CMake is not installed. Please install CMake 3.22+ and re-run this script."
    Fatal $msg $ConfigureLog
    $ExitCode = 1
    throw $msg
}

# ── 2. Locate or install Qt6 ─────────────────────────────────
function Find-Qt6 {
    # Check common installation paths on Windows
    $searchPaths = @(
        "$env:QT_DIR",
        "$env:Qt6_DIR",
        "$env:QTDIR",
        "C:\Qt",
        "$env:USERPROFILE\Qt",
        "C:\Qt\6*\msvc*_64",
        "C:\Qt\6*\mingw*_64",
        "$env:USERPROFILE\Qt\6*\msvc*_64",
        "$env:USERPROFILE\Qt\6*\mingw*_64"
    ) | Where-Object { $_ -and $_ -ne "" }

    foreach ($base in $searchPaths) {
        $resolved = Resolve-Path -Path $base -ErrorAction SilentlyContinue
        foreach ($p in $resolved) {
            $configFile = Join-Path $p.Path "lib\cmake\Qt6\Qt6Config.cmake"
            if (Test-Path $configFile) {
                return $p.Path
            }
        }
    }
    return $null
}

function Install-Qt6 {
    # Try aqtinstall (Python-based Qt installer – the standard CI tool)
    $pipCmd = Get-Command pip3 -ErrorAction SilentlyContinue
    if (-not $pipCmd) { $pipCmd = Get-Command pip -ErrorAction SilentlyContinue }

    if ($pipCmd) {
        Info "Attempting to install Qt via aqtinstall (pip) …"
        $pipOutput = & $pipCmd.Source install aqtinstall 2>&1
        if ($LASTEXITCODE -ne 0) {
            Warn "pip install aqtinstall failed: $pipOutput"
            return $false
        }
        # Find aqt: check PATH, then Python user-scripts dir, then python -m aqt
        $aqtExe = $null
        $usePythonModule = $false
        $aqtCmd = Get-Command aqt -ErrorAction SilentlyContinue
        if ($aqtCmd) {
            $aqtExe = $aqtCmd.Source
        } else {
            # aqt not on PATH — look in Python user-scripts directory
            $pyCmd2 = Get-Command python3 -ErrorAction SilentlyContinue
            if (-not $pyCmd2) { $pyCmd2 = Get-Command python -ErrorAction SilentlyContinue }
            if ($pyCmd2) {
                $userScripts = & $pyCmd2.Source -c "import sysconfig,os;print(sysconfig.get_path('scripts',f'{os.name}_user'))" 2>$null
                if ($userScripts) {
                    $aqtPath = Join-Path $userScripts "aqt.exe"
                    if (Test-Path $aqtPath) {
                        $aqtExe = $aqtPath
                        Info "Found aqt at: $aqtExe"
                    }
                }
                # Last resort: python -m aqt
                if (-not $aqtExe) {
                    $aqtExe = $pyCmd2.Source
                    $usePythonModule = $true
                    Info "Using 'python -m aqt' (aqt not found on PATH)"
                }
            }
        }

        if ($aqtExe) {
            $qtVer  = if ($env:SSA_QT_VERSION) { $env:SSA_QT_VERSION } else { "6.7.2" }
            $qtArch = if ($env:SSA_QT_ARCH)    { $env:SSA_QT_ARCH }    else { "win64_msvc2019_64" }
            $qtInstallDir = if ($env:QT_BASEDIR) { $env:QT_BASEDIR } else { Join-Path $env:USERPROFILE "Qt" }
            Info "Installing Qt $qtVer ($qtArch) to $qtInstallDir …"
            if ($usePythonModule) {
                & $aqtExe -m aqt install-qt windows desktop $qtVer $qtArch --outputdir $qtInstallDir 2>&1
            } else {
                & $aqtExe install-qt windows desktop $qtVer $qtArch --outputdir $qtInstallDir 2>&1
            }
            if ($LASTEXITCODE -eq 0) { return $true }
        }
        Warn "aqtinstall did not succeed."
    }

    return $false
}

$qt6Path = Find-Qt6
if (-not $qt6Path) {
    Warn "Qt6 not found in common locations — attempting automatic installation …"
    $installed = Install-Qt6
    $qt6Path = Find-Qt6

    if (-not $qt6Path) {
        $msg = "Qt6 could not be found or installed automatically. Please install Qt 6.4+ from https://www.qt.io/download or via: pip install aqtinstall && aqt install-qt windows desktop 6.7.2 win64_msvc2019_64 --outputdir `$env:USERPROFILE\Qt"
        Write-Host ""
        Write-Host "=== $msg ===" -ForegroundColor Yellow
        Write-Host '  Set: $env:CMAKE_PREFIX_PATH = "C:\Qt\6.x.x\msvc20xx_64"' -ForegroundColor Yellow
        Write-Host '  Or:  cmake -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.x.x\..."' -ForegroundColor Yellow
        Fatal $msg $ConfigureLog
        $ExitCode = 1
        throw $msg
    }
}

Info "Qt6 found at: $qt6Path"

# ── 3. Configure ─────────────────────────────────────────────
$cmakeArgs = @("-B", $BuildDir, "-DCMAKE_BUILD_TYPE=$BuildType")
if ($qt6Path) {
    $cmakeArgs += "-DCMAKE_PREFIX_PATH=$qt6Path"
}

Info "Configuring ($BuildType) …"
cmake @cmakeArgs 2>&1 | Tee-Object -FilePath $ConfigureLog -Append
if ($LASTEXITCODE -ne 0) {
    Fatal "CMake configuration failed. See $ConfigureLog" $ConfigureLog
    $ExitCode = 1
    throw "CMake configuration failed."
}

# ── 4. Build ─────────────────────────────────────────────────
$cpuCount = 2
try { $cpuCount = (Get-CimInstance Win32_Processor -ErrorAction SilentlyContinue).NumberOfLogicalProcessors } catch {}
if (-not $cpuCount -or $cpuCount -lt 1) { $cpuCount = 2 }

Info "Building with $cpuCount parallel jobs …"
cmake --build $BuildDir --config $BuildType --parallel $cpuCount 2>&1 | Tee-Object -FilePath $BuildLog -Append
if ($LASTEXITCODE -ne 0) {
    Fatal "Build failed. See $BuildLog" $BuildLog
    $ExitCode = 1
    throw "Build failed."
}

# ── 5. Run tests ─────────────────────────────────────────────
$testBin = Join-Path $BuildDir "$BuildType\SSA_Tests.exe"
if (-not (Test-Path $testBin)) {
    $testBin = Join-Path $BuildDir "SSA_Tests.exe"
}
if (Test-Path $testBin) {
    Info "Running tests …"
    ctest --test-dir $BuildDir --build-config $BuildType --output-on-failure 2>&1 | Tee-Object -FilePath $TestLog -Append
    if ($LASTEXITCODE -ne 0) {
        Fatal "Tests failed. See $TestLog" $TestLog
        $ExitCode = 1
        throw "Tests failed."
    }
} else {
    Warn "Test binary not found — skipping tests."
    Add-Content -Path $TestLog -Value "Test binary not found - tests skipped."
}

Info "Build complete!  Binary: $BuildDir\$BuildType\SSA.exe  (or $BuildDir\SSA.exe)"
Info "Log files:"
Info "  Configure : $ConfigureLog"
Info "  Build     : $BuildLog"
Info "  Test      : $TestLog"
Info "  Full      : $FullLog"

} catch {
    if ($ExitCode -eq 0) { $ExitCode = 1 }
    Write-Host ""
    Write-Host "!! Error: $($_.Exception.Message)" -ForegroundColor Red
} finally {
    # Stop transcript so full.log is flushed
    try { Stop-Transcript -ErrorAction SilentlyContinue } catch {}

    # Keep the window open so the user can read the output
    if ($ExitCode -ne 0) {
        Write-Host ""
        Write-Host "!! Build failed. Check the log files above for details." -ForegroundColor Red
    }
    Write-Host ""
    if (-not $env:CI) {
        Read-Host "Press Enter to close …"
    }
    exit $ExitCode
}
