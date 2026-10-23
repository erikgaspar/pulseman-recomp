# Monta o instalador: build/installer/Pulseman-Setup.exe
#
#   powershell -ExecutionPolicy Bypass -File installer/build.ps1
#
# Compila Setup.cs com o compilador C# do .NET Framework 4.8 (já vem no Windows) e põe
# dentro do .exe: o jogo (build/Release), as imagens da abertura (img/*.png, *.bmp), o
# ícone e o papel de parede (wallpaper.jpg, .jpeg ou .png em installer/ ou img/; ele não é
# instalado com o jogo).
# O jogo que vai junto é a versão distribuível (pulseman_dist.exe, instalada como pulseman.exe):
# nenhum arquivo gerado a partir da ROM; o código do jogo roda direto da ROM do jogador. Ela
# compila sem a ROM: cmake -S . -B build_dist -DPM_ROM=nenhuma && cmake --build build_dist
# --config Release (ou vem junto no build normal, em build\Release). A ROM não vai junto.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$fw = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
$csc = Join-Path $fw 'csc.exe'
$out = Join-Path $root 'build\installer'
New-Item -ItemType Directory -Force $out | Out-Null

$release = 'build_dist\Release', 'build\Release' | ForEach-Object { Join-Path $root $_ } |
    Where-Object { Test-Path (Join-Path $_ 'pulseman_dist.exe') } | Select-Object -First 1
if (-not $release) { throw 'falta pulseman_dist.exe (build_dist\Release ou build\Release): compile o jogo antes' }
if (-not (Test-Path (Join-Path $release 'SDL2.dll'))) { throw "falta SDL2.dll em $release" }
Write-Host "jogo: $(Join-Path $release 'pulseman_dist.exe')"

# constantes do build: SHA-1 da ROM esperada (o mesmo do gerador de código) e versão
$sha = (Get-Content (Join-Path $root 'recomp\rom.sha1') -Raw).Trim().ToLower()
$version = '1.4.1'
$buildCs = Join-Path $out 'Build.cs'
Set-Content -Encoding UTF8 $buildCs @"
namespace PulsemanSetup {
    static class Build {
        public const string RomSha1 = "$sha";
        public const string Version = "$version";
    }
}
"@

$resources = @(
    "/resource:$(Join-Path $PSScriptRoot 'ui.xaml'),ui.xaml",
    "/resource:$(Join-Path $root 'icon\pulseman.ico'),icon.ico",
    "/resource:$(Join-Path $release 'pulseman_dist.exe'),payload/pulseman.exe",
    "/resource:$(Join-Path $release 'SDL2.dll'),payload/SDL2.dll"
)
Get-ChildItem (Join-Path $root 'img') -File |
    Where-Object { $_.Extension -in '.png', '.bmp' -and $_.BaseName -ne 'wallpaper' } | ForEach-Object {
    $resources += "/resource:$($_.FullName),payload/img/$($_.Name)"
}
$wall = foreach ($d in $PSScriptRoot, (Join-Path $root 'img')) {
    foreach ($n in 'wallpaper.jpg', 'wallpaper.jpeg', 'wallpaper.png') { Join-Path $d $n }
}
$wall = $wall | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($wall) {
    $resources += "/resource:$wall,wallpaper"
    Write-Host "papel de parede: $wall"
} else {
    Write-Host 'sem papel de parede (wallpaper.jpg em installer/ ou img/): usa o fundo animado'
}

$refs = @(
    "$fw\WPF\PresentationFramework.dll", "$fw\WPF\PresentationCore.dll", "$fw\WPF\WindowsBase.dll",
    "$fw\System.Xaml.dll", "$fw\System.IO.Compression.dll", "$fw\System.IO.Compression.FileSystem.dll",
    "$fw\System.Windows.Forms.dll", "$fw\System.Core.dll"
) | ForEach-Object { "/reference:$_" }

$exe = Join-Path $out 'Pulseman-Setup.exe'
& $csc /nologo /target:winexe /optimize+ /platform:anycpu /codepage:65001 `
    "/win32icon:$(Join-Path $root 'icon\pulseman.ico')" "/win32manifest:$(Join-Path $PSScriptRoot 'app.manifest')" `
    "/out:$exe" @refs @resources (Join-Path $PSScriptRoot 'Setup.cs') $buildCs
if ($LASTEXITCODE -ne 0) { throw 'csc falhou' }
$size = [math]::Round((Get-Item $exe).Length / 1MB, 1)
Write-Host "instalador: $exe ($size MB)"
