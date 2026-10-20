# Baixa as dependências de compilação para tools/ (não ficam no repositório):
#   - CMake 3.30.5 portátil (tools/cmake-3.30.5-windows-x86_64)
#   - SDL2 2.30.8 para Visual Studio (tools/SDL2-2.30.8)
#
#   powershell -ExecutionPolicy Bypass -File tools/baixar_dependencias.ps1
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$tools = $PSScriptRoot
$deps = @(
    @{ Name = 'CMake 3.30.5'; Dir = 'cmake-3.30.5-windows-x86_64'
       Url = 'https://github.com/Kitware/CMake/releases/download/v3.30.5/cmake-3.30.5-windows-x86_64.zip' },
    @{ Name = 'SDL2 2.30.8'; Dir = 'SDL2-2.30.8'
       Url = 'https://github.com/libsdl-org/SDL/releases/download/release-2.30.8/SDL2-devel-2.30.8-VC.zip' }
)
foreach ($d in $deps) {
    $target = Join-Path $tools $d.Dir
    if (Test-Path $target) { Write-Host "$($d.Name): já existe ($target)"; continue }
    $zip = Join-Path $env:TEMP ($d.Dir + '.zip')
    Write-Host "$($d.Name): baixando..."
    Invoke-WebRequest -Uri $d.Url -OutFile $zip -UseBasicParsing
    Expand-Archive -Path $zip -DestinationPath $tools -Force
    Remove-Item $zip
    if (-not (Test-Path $target)) { throw "$($d.Name): pasta $target não apareceu depois de extrair" }
    Write-Host "$($d.Name): ok"
}
