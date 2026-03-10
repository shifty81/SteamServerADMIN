# ──────────────────────────────────────────────────────────────
# SSA – Steam Server ADMIN  ·  Automated build script (Windows)
# Dependencies (GLFW, ImGui, nlohmann/json, GoogleTest) are fetched
# automatically by CMake FetchContent.  No external UI framework
# installation is required.
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

try {

# ── 0. Set up logging ────────────────────────────────────────
# Resolve project root with fallback for different execution contexts
# $PSScriptRoot is the script's directory; $MyInvocation.MyCommand.Path includes the filename
$ProjectRoot = $null
if ($PSScriptRoot) {
    $ProjectRoot = Split-Path -Parent $PSScriptRoot
} elseif ($MyInvocation.MyCommand.Path) {
    $ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
}
if (-not $ProjectRoot -or $ProjectRoot -eq "") { $ProjectRoot = (Get-Location).Path }

$LogDir      = Join-Path $ProjectRoot "logs"
if (-not (Test-Path $LogDir)) { New-Item -ItemType Directory -Path $LogDir -Force | Out-Null }

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
try {
    Start-Transcript -Path $FullLog -Append
} catch {
    Warn "Start-Transcript failed: $($_.Exception.Message)"
    Warn "Console output will not be captured in full.log (per-phase logs still work)."
}

Info "Logs will be written to: $LogDir"

# ── 1. Check for cmake ───────────────────────────────────────
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    $msg = "CMake is not installed. Please install CMake 3.22+ and re-run this script."
    Fatal $msg $ConfigureLog
    $ExitCode = 1
    throw $msg
}

# ── 2. Configure ─────────────────────────────────────────────
Info "Configuring ($BuildType) …"
cmake -B $BuildDir -DCMAKE_BUILD_TYPE=$BuildType 2>&1 | Tee-Object -FilePath $ConfigureLog -Append
if ($LASTEXITCODE -ne 0) {
    Fatal "CMake configuration failed. See $ConfigureLog" $ConfigureLog
    $ExitCode = 1
    throw "CMake configuration failed."
}

# ── 3. Build ─────────────────────────────────────────────────
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

# ── 4. Run tests ─────────────────────────────────────────────
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
