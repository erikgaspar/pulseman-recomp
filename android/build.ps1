# Monta o APK do Pulseman para Android (build/android/Pulseman.apk), sem Gradle: a parte
# nativa com o CMake e o NDK, o Java com o javac do Android Studio, e o empacotamento com as
# build-tools do SDK (d8, aapt2, zipalign, apksigner).
#
#   powershell -ExecutionPolicy Bypass -File android/build.ps1
#
# Precisa de: Android Studio (Java em jbr/), Android SDK com platforms/android-34 e
# build-tools/36.0.0, o NDK r27c em tools/android-ndk-r27c, o código-fonte do SDL 2.30.8 em
# tools/SDL2-2.30.8-src, o Ninja em tools/ninja e a versão distribuível já compilada no PC
# (build_dist: os arquivos do Musashi gerados pelo m68kmake vêm de lá).
#
# O NDK não aceita caminhos com acento ("decompilação"): tudo é compilado por um atalho de
# pasta (junction) em %LOCALAPPDATA%\pulseman-android\src.
#
# A chave de assinatura fica em %LOCALAPPDATA%\pulseman-android\pulseman.jks (criada na
# primeira vez). Guarde-a: um APK assinado com outra chave não instala por cima do anterior.
# -Debug: APK depurável (Pulseman-debug.apk), para testes com adb (run-as) no emulador
param([string[]]$Abis = @('arm64-v8a', 'armeabi-v7a', 'x86_64'), [switch]$Debug)   # x86_64: emulador e Chromebooks
$ErrorActionPreference = 'Stop'

$root = Split-Path $PSScriptRoot -Parent
$work = Join-Path $env:LOCALAPPDATA 'pulseman-android'
$src = Join-Path $work 'src'
$sdk = Join-Path $env:LOCALAPPDATA 'Android\Sdk'
$bt = Join-Path $sdk 'build-tools\36.0.0'   # o d8 da 34 falha com as classes do javac novo
$androidJar = Join-Path $sdk 'platforms\android-34\android.jar'
$jbr = 'C:\Program Files\Android\Android Studio\jbr'
$env:JAVA_HOME = $jbr
$env:PATH = "$jbr\bin;$env:PATH"

# versão: a mesma do instalador do PC
$version = (Select-String -Path (Join-Path $root 'installer\build.ps1') -Pattern "^\`$version = '(.+)'").Matches[0].Groups[1].Value
$parts = $version.Split('.') + @('0', '0')
$versionCode = [int]$parts[0] * 10000 + [int]$parts[1] * 100 + [int]$parts[2]
Write-Host "Pulseman Android $version (código $versionCode)"

New-Item -ItemType Directory -Force $work | Out-Null
if (-not (Test-Path $src)) { New-Item -ItemType Junction -Path $src -Target $root | Out-Null }
$py = (Get-Command py).Source

# ---------- parte nativa ----------
$cmake = Join-Path $src 'tools\cmake-3.30.5-windows-x86_64\bin\cmake.exe'
$ndk = Join-Path $src 'tools\android-ndk-r27c'
$strip = Join-Path $ndk 'toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-strip.exe'
$stage = Join-Path $work 'apk'
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force "$stage\lib" | Out-Null
foreach ($abi in $Abis) {
    $b = Join-Path $work "build\$abi"
    & $cmake -S "$src\android" -B $b -G Ninja "-DCMAKE_MAKE_PROGRAM=$src\tools\ninja\ninja.exe" `
        "-DCMAKE_TOOLCHAIN_FILE=$ndk\build\cmake\android.toolchain.cmake" "-DANDROID_ABI=$abi" `
        -DANDROID_PLATFORM=android-21 -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release `
        "-DPython3_EXECUTABLE=$py" | Out-Null
    if ($LASTEXITCODE) { throw "cmake ($abi) falhou" }
    & $cmake --build $b
    if ($LASTEXITCODE) { throw "compilação ($abi) falhou" }
    New-Item -ItemType Directory -Force "$stage\lib\$abi" | Out-Null
    & $strip -o "$stage\lib\$abi\libmain.so" "$b\libmain.so"
    & $strip -o "$stage\lib\$abi\libSDL2.so" "$b\SDL2\libSDL2.so"
}

# ---------- Java: SDL2 (org.libsdl.app) e as telas do app ----------
$classes = Join-Path $work 'classes'
if (Test-Path $classes) { Remove-Item $classes -Recurse -Force }
New-Item -ItemType Directory -Force $classes | Out-Null
$java = @(Get-ChildItem "$src\tools\SDL2-2.30.8-src\android-project\app\src\main\java" -Recurse -Filter *.java) +
        @(Get-ChildItem "$src\android\java" -Recurse -Filter *.java) | ForEach-Object { $_.FullName }
& "$jbr\bin\javac.exe" -nowarn -Xlint:-options -source 8 -target 8 -encoding UTF-8 -classpath $androidJar -d $classes $java
if ($LASTEXITCODE) { throw 'javac falhou' }
$jar = Join-Path $work 'classes.jar'
& "$jbr\bin\jar.exe" cf $jar -C $classes .
if ($LASTEXITCODE) { throw 'jar falhou' }
& "$bt\d8.bat" --release --min-api 21 --lib $androidJar --output $stage $jar
if ($LASTEXITCODE) { throw 'd8 falhou' }

# ---------- recursos, manifesto, empacotamento ----------
$res = Join-Path $work 'res.zip'
& "$bt\aapt2.exe" compile --dir "$src\android\res" -o $res
if ($LASTEXITCODE) { throw 'aapt2 compile falhou' }
$base = Join-Path $work 'base.apk'
& "$bt\aapt2.exe" link -o $base -I $androidJar --manifest "$src\android\AndroidManifest.xml" `
    --min-sdk-version 21 --target-sdk-version 34 --version-code $versionCode --version-name $version `
    $(if ($Debug) { '--debug-mode' }) $res
if ($LASTEXITCODE) { throw 'aapt2 link falhou' }

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::Open($base, 'Update')
try {
    [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, "$stage\classes.dex", 'classes.dex') | Out-Null
    foreach ($so in Get-ChildItem "$stage\lib" -Recurse -Filter *.so) {
        $entry = 'lib/' + $so.Directory.Name + '/' + $so.Name
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $so.FullName, $entry) | Out-Null
    }
} finally { $zip.Dispose() }

$aligned = Join-Path $work 'aligned.apk'
& "$bt\zipalign.exe" -p -f 4 $base $aligned
if ($LASTEXITCODE) { throw 'zipalign falhou' }

$ks = Join-Path $work 'pulseman.jks'
if (-not (Test-Path $ks)) {
    & "$jbr\bin\keytool.exe" -genkeypair -keystore $ks -storepass pulseman -keypass pulseman -alias pulseman `
        -keyalg RSA -keysize 2048 -validity 10000 -dname 'CN=Shin Gattai Ports, O=Pulseman PC'
    if ($LASTEXITCODE) { throw 'keytool falhou' }
}
$outDir = Join-Path $root 'build\android'
New-Item -ItemType Directory -Force $outDir | Out-Null
$apk = Join-Path $outDir $(if ($Debug) { 'Pulseman-debug.apk' } else { 'Pulseman.apk' })
& "$bt\apksigner.bat" sign --ks $ks --ks-pass pass:pulseman --key-pass pass:pulseman --ks-key-alias pulseman --out $apk $aligned
if ($LASTEXITCODE) { throw 'apksigner falhou' }
& "$bt\apksigner.bat" verify $apk
if ($LASTEXITCODE) { throw 'apksigner verify falhou' }
Write-Host ("APK: {0} ({1:N1} MB)" -f $apk, ((Get-Item $apk).Length / 1MB))
