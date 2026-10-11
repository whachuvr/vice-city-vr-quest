param(
    [string]$LogPath = "$env:TEMP\ViceCityVR-Incremental-Build.log"
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$workRoot = 'C:\VCVRBuild'
$gradleLog = "$env:TEMP\ViceCityVR-Incremental-Build.gradle.log"

try { Start-Transcript -Path $LogPath -Force | Out-Null } catch {}

function Fail([string]$Message) {
    Write-Host ""
    Write-Host "ERROR: $Message"
    Write-Host "Nothing was installed on the headset."
    try { Stop-Transcript | Out-Null } catch {}
    exit 1
}

Write-Host ""
Write-Host "Vice City VR incremental builder"
Write-Host "Repository: $repo"
Write-Host "Work dir:   $workRoot"
Write-Host ""

if (-not (Test-Path $workRoot)) { Fail "Work directory does not exist: $workRoot" }

$buildTree = Get-ChildItem -Path $workRoot -Directory -Filter 'vice-city-vr-build*' -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if (-not $buildTree) { Fail "No existing assembled vice-city-vr-build tree was found under $workRoot. Run the normal builder once first." }
$buildRoot = $buildTree.FullName
$androidDir = Join-Path $buildRoot 'android'
if (-not (Test-Path $androidDir)) { Fail "Existing build tree has no android directory: $androidDir" }

Write-Host "Reusing assembled tree:"
Write-Host "  $buildRoot"
Write-Host ""

Write-Host "[1/5] Syncing only content-changed overlay files..."
$srcOverlay = Join-Path $repo 'overlay'
$dstOverlay = $buildRoot
if (-not (Test-Path $srcOverlay)) { Fail "Repository overlay directory was not found: $srcOverlay" }
# tools\assemble.ps1 overlays the CONTENTS of overlay\ directly onto the assembled root.
# Mirror that exact mapping here: overlay\src -> buildRoot\src, overlay\android -> buildRoot\android, etc.

$copied = 0
$unchanged = 0
Get-ChildItem -LiteralPath $srcOverlay -File -Recurse | ForEach-Object {
    $srcFile = $_.FullName
    $rel = $srcFile.Substring($srcOverlay.Length).TrimStart('\')
    $dstFile = Join-Path $dstOverlay $rel
    $dstDir = Split-Path -Parent $dstFile
    if (-not (Test-Path -LiteralPath $dstDir)) { New-Item -ItemType Directory -Force -Path $dstDir | Out-Null }

    $needsCopy = $true
    if (Test-Path -LiteralPath $dstFile -PathType Leaf) {
        $srcInfo = Get-Item -LiteralPath $srcFile
        $dstInfo = Get-Item -LiteralPath $dstFile
        if ($srcInfo.Length -eq $dstInfo.Length) {
            $srcHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $srcFile).Hash
            $dstHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $dstFile).Hash
            if ($srcHash -eq $dstHash) { $needsCopy = $false }
        }
    }

    if ($needsCopy) {
        Copy-Item -LiteralPath $srcFile -Destination $dstFile -Force
        # Use the current filesystem time, not archive/source timestamps. This prevents
        # future/stale timestamps from keeping CMake's build.ninja perpetually dirty.
        (Get-Item -LiteralPath $dstFile).LastWriteTime = Get-Date
        $copied++
    } else {
        $unchanged++
    }
}
Write-Host "  changed/new files copied: $copied"
Write-Host "  identical files left untouched: $unchanged"

Write-Host "[2/5] Checking source-side incremental hooks..."
$pedCandidates = @(
    (Join-Path $buildRoot 'src\peds\Ped.cpp'),
    (Join-Path $buildRoot 'reVC\src\peds\Ped.cpp'),
    (Join-Path $buildRoot 'game\src\peds\Ped.cpp')
)
$pedFile = $pedCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($pedFile) {
    $pedText = Get-Content -Raw -LiteralPath $pedFile
    if ($pedText -match 'VrBodyIK::Apply') {
        Write-Host "  Body IK hook already present; no patching needed."
    } else {
        Write-Host "  WARNING: Existing assembled Ped.cpp does not contain VrBodyIK::Apply."
        Write-Host "  This script will not guess-patch core source. Re-run the Phase 1 patch once if needed."
    }
} else {
    Write-Host "  Ped.cpp path not found in assembled tree; continuing without modifying core source."
}

Write-Host "[3/5] Restoring Android SDK location..."
$sdkCandidates = New-Object System.Collections.Generic.List[string]
if ($env:ANDROID_HOME) { $sdkCandidates.Add($env:ANDROID_HOME) }
if ($env:ANDROID_SDK_ROOT) { $sdkCandidates.Add($env:ANDROID_SDK_ROOT) }
if ($env:LOCALAPPDATA) { $sdkCandidates.Add((Join-Path $env:LOCALAPPDATA 'Android\Sdk')) }
$sdkCandidates.Add('C:\VCVRBuild\.android-sdk')

$sdk = $null
foreach ($candidate in $sdkCandidates) {
    if ([string]::IsNullOrWhiteSpace($candidate)) { continue }
    $trimmed = $candidate.Trim().Trim('"')
    if (Test-Path -LiteralPath $trimmed -PathType Container) {
        try { $sdk = (Resolve-Path -LiteralPath $trimmed).Path } catch { $sdk = $trimmed }
        break
    }
}
if (-not $sdk) {
    Fail "Android SDK was not found. Checked ANDROID_HOME, ANDROID_SDK_ROOT, %LOCALAPPDATA%\Android\Sdk, and C:\VCVRBuild\.android-sdk."
}

# Java .properties treats backslashes as escapes. Always write a forward-slash path.
$sdkForProperties = $sdk -replace '\\','/'
$localProperties = Join-Path $androidDir 'local.properties'
$localPropertiesText = "sdk.dir=$sdkForProperties"
$writeLocalProperties = $true
if (Test-Path -LiteralPath $localProperties -PathType Leaf) {
    $existingLocalProperties = (Get-Content -Raw -LiteralPath $localProperties).Trim()
    if ($existingLocalProperties -eq $localPropertiesText) { $writeLocalProperties = $false }
}
if ($writeLocalProperties) {
    $localPropertiesText | Set-Content -LiteralPath $localProperties -Encoding ASCII
    Write-Host "  local.properties updated."
} else {
    Write-Host "  local.properties already correct; timestamp left untouched."
}
$env:ANDROID_HOME = $sdk
$env:ANDROID_SDK_ROOT = $sdk

Write-Host "  SDK directory: $sdk"
Write-Host "  local.properties: $localProperties"
Write-Host "  wrote: sdk.dir=$sdkForProperties"

if (-not (Test-Path (Join-Path $sdk 'platform-tools')) -and -not (Test-Path (Join-Path $sdk 'ndk'))) {
    Write-Host "  WARNING: SDK exists but neither platform-tools nor ndk was found at its root. Gradle will report the exact missing component if needed."
}

Write-Host "[4/5] Restoring JDK 21 environment..."
$javaCandidates = New-Object System.Collections.Generic.List[string]
if ($env:JAVA_HOME) { $javaCandidates.Add($env:JAVA_HOME) }
$javaCandidates.Add((Join-Path $workRoot ".tools\jdk-21.0.11+10"))
$javaCandidates.Add((Join-Path $workRoot ".tools\jdk-21.0.11_10"))
$javaCandidates.Add("C:\Program Files\Android\Android Studio\jbr")
if ($env:LOCALAPPDATA) { $javaCandidates.Add((Join-Path $env:LOCALAPPDATA "Programs\Android Studio\jbr")) }

$javaHome = $null
foreach ($candidate in $javaCandidates) {
    if ([string]::IsNullOrWhiteSpace($candidate)) { continue }
    $trimmed = $candidate.Trim().Trim('"')
    $javaExe = Join-Path $trimmed "bin\java.exe"
    if (-not (Test-Path -LiteralPath $javaExe -PathType Leaf)) { continue }
    $savedErrorPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try { $versionText = (& $javaExe -version 2>&1 | Out-String) } finally { $ErrorActionPreference = $savedErrorPreference }
    if ($versionText -match 'version "21(?:\.|\")') {
        try { $javaHome = (Resolve-Path -LiteralPath $trimmed).Path } catch { $javaHome = $trimmed }
        break
    }
}
if (-not $javaHome) {
    $foundJava = Get-ChildItem -LiteralPath (Join-Path $workRoot ".tools") -Filter java.exe -File -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\jdk-[^\\]+\\bin\\java\.exe$' } |
        Select-Object -First 1
    if ($foundJava) { $javaHome = Split-Path -Parent (Split-Path -Parent $foundJava.FullName) }
}
if (-not $javaHome) {
    Fail "JDK 21 was not found. The incremental builder will not download tools; run the normal builder once so its pinned JDK is prepared under $workRoot\.tools, or install Android Studio/JDK 21."
}
$env:JAVA_HOME = $javaHome
$javaBin = Join-Path $javaHome "bin"
if (($env:PATH -split ';') -notcontains $javaBin) { $env:PATH = "$javaBin;$env:PATH" }
Write-Host "  JAVA_HOME: $javaHome"
Write-Host "  java.exe:  $(Join-Path $javaHome 'bin\java.exe')"

Write-Host "[5/6] Normalizing future-dated CMake inputs..."
# A source/archive extraction can leave CMake configure inputs timestamped in the
# future relative to Windows' current clock. If any RERUN_CMAKE dependency is in
# the future, CMake regenerates build.ninja using the current time; Ninja then
# immediately sees that future input as newer and reruns CMake forever, ending in
# "manifest 'build.ninja' still dirty after 100 tries".
#
# Fix the cause, not the generated manifest: only normalize FUTURE-dated CMake
# configure inputs back to the current filesystem clock. Content is untouched,
# compiled objects are untouched, and ordinary incremental dependency tracking
# remains intact.
$clockNow = Get-Date
$futureCutoff = $clockNow.AddSeconds(5)
$normalizedCMakeInputs = 0
$cmakeInputCandidates = Get-ChildItem -LiteralPath $buildRoot -File -Recurse -ErrorAction SilentlyContinue |
    Where-Object {
        $_.Name -eq 'CMakeLists.txt' -or
        $_.Extension -eq '.cmake'
    }
foreach ($cmakeInput in $cmakeInputCandidates) {
    if ($cmakeInput.LastWriteTime -gt $futureCutoff) {
        # Keep the file safely behind "now" so a CMake regeneration that writes
        # build.ninja at the current clock can become newer and stay newer.
        $newTime = $clockNow.AddSeconds(-2)
        try {
            $oldTime = $cmakeInput.LastWriteTime
            $cmakeInput.LastWriteTime = $newTime
            $normalizedCMakeInputs++
            Write-Host ("  normalized future timestamp: {0}" -f $cmakeInput.FullName)
            Write-Host ("    {0:yyyy-MM-dd HH:mm:ss} -> {1:yyyy-MM-dd HH:mm:ss}" -f $oldTime, $newTime)
        } catch {
            Fail ("Could not normalize future-dated CMake input: " + $cmakeInput.FullName + " -- " + $_.Exception.Message)
        }
    }
}
if ($normalizedCMakeInputs -eq 0) {
    Write-Host "  no future-dated CMake inputs found."
} else {
    Write-Host "  normalized CMake inputs: $normalizedCMakeInputs"
}

Write-Host "[6/6] Running Gradle incremental APK build..."
Write-Host "  Gradle log: $gradleLog"
Remove-Item -LiteralPath $gradleLog -Force -ErrorAction SilentlyContinue

# The official builder does not use a Gradle wrapper in the assembled tree.
# It downloads a verified Gradle 8.13 distribution under C:\VCVRBuild\.tools.
$gradleCandidates = New-Object System.Collections.Generic.List[string]
$gradleCandidates.Add((Join-Path $workRoot '.tools\gradle-8.13\bin\gradle.bat'))

# Preserve compatibility with a future assembled tree that may gain a wrapper.
$wrapperCandidate = Join-Path $androidDir 'gradlew.bat'
if (Test-Path -LiteralPath $wrapperCandidate -PathType Leaf) {
    $gradleCandidates.Insert(0, $wrapperCandidate)
}

$gradle = $null
foreach ($candidate in $gradleCandidates) {
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
        $gradle = (Resolve-Path -LiteralPath $candidate).Path
        break
    }
}

# Last-resort discovery only inside the existing WorkDir; never download or rebuild tools.
if (-not $gradle) {
    $foundGradle = Get-ChildItem -LiteralPath $workRoot -Filter 'gradle.bat' -File -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\.tools\\gradle-[^\\]+\\bin\\gradle\.bat$' } |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if ($foundGradle) { $gradle = $foundGradle.FullName }
}

if (-not $gradle) {
    Fail "Verified Gradle installation was not found under $workRoot\.tools. Run the normal builder once so it can prepare Gradle; this incremental script will not download or reinstall it."
}
Write-Host "  Gradle: $gradle"

function Invoke-IncrementalGradleBuild {
    param([string]$LogFile)
    Remove-Item -LiteralPath $LogFile -Force -ErrorAction SilentlyContinue
    Push-Location $androidDir
    try {
        $cmdLine = '"' + $gradle + '" :app:assembleDebug --no-daemon --console=plain --stacktrace > "' + $LogFile + '" 2>&1'
        & $env:ComSpec /d /s /c $cmdLine
        return $LASTEXITCODE
    } finally {
        Pop-Location
    }
}

$gradleExit = Invoke-IncrementalGradleBuild -LogFile $gradleLog

# No manifest-timestamp hacks: a dirty-manifest loop is handled by normalizing
# future-dated CMake inputs before Gradle/Ninja starts.

if ($gradleExit -ne 0) {
    Write-Host ""
    Write-Host "================ GRADLE ERROR SUMMARY ================"
    if (Test-Path $gradleLog) {
        $lines = Get-Content -LiteralPath $gradleLog
        $what = ($lines | Select-String -SimpleMatch '* What went wrong:' | Select-Object -First 1).LineNumber
        if ($what) {
            $start = [Math]::Max(0, $what - 1)
            $end = [Math]::Min($lines.Count - 1, $start + 90)
            $lines[$start..$end] | ForEach-Object { Write-Host $_ }
        }
        Write-Host ""
        Write-Host "---------------- KEY ERROR LINES ---------------------"
        $patterns = @('Caused by:',' error:','FAILED:','CMake Error','ninja:','SDK location','NDK','No such file','not found','undefined reference','The filename, directory name, or volume label syntax is incorrect')
        $seen = @{}
        foreach ($line in $lines) {
            foreach ($p in $patterns) {
                if ($line -like "*$p*") {
                    if (-not $seen.ContainsKey($line)) { Write-Host $line; $seen[$line] = $true }
                    break
                }
            }
        }
        Write-Host ""
        Write-Host "---------------- LAST 80 LINES -----------------------"
        $lines | Select-Object -Last 80 | ForEach-Object { Write-Host $_ }
    }
    Write-Host "====================================================="
    Fail "Android/ARM64 incremental build failed (exit code $gradleExit). Full Gradle log: $gradleLog"
}

$apkCandidates = Get-ChildItem -Path (Join-Path $androidDir 'app\build\outputs\apk') -Filter '*.apk' -Recurse -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending
Write-Host ""
Write-Host "SUCCESS: incremental APK build completed."
if ($apkCandidates) {
    Write-Host "APK:"
    Write-Host "  $($apkCandidates[0].FullName)"
}
Write-Host "Nothing was installed on the headset."
try { Stop-Transcript | Out-Null } catch {}
exit 0
