<#
Instala (o pone al día) la de producción de GenClipPro: la release local más nueva (las que deja empaquetar.ps1
en %LOCALAPPDATA%\GenClipPro\releases) o la que digas con -Zip, en %LOCALAPPDATA%\Programs\GenClipPro, si su .sha256
coincide. Deja en el menú Inicio «GenClipPro» (esa: después se actualiza sola desde Ajustes › Versión) y
«GenClipPro Dev» (la build de desarrollo de este repositorio, la última que se compila). Las dos comparten la
carpeta de los proyectos y sus claves (data\settings.json de esa carpeta, solo en este PC).

  powershell -ExecutionPolicy Bypass -File tools\release\instalar.ps1 [-Zip <GenClipPro-x.y.z-win64.zip>]
      [-Destino <carpeta>] [-Dev <genclip-studio.exe de desarrollo>]
#>
param(
    [string]$Zip,
    [string]$Destino = (Join-Path $env:LOCALAPPDATA 'Programs\GenClipPro'),
    [string]$Dev = (Join-Path $PSScriptRoot '..\..\build\msvc-vcpkg-debug\apps\studio\genclip-studio.exe')
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

$pattern = '^GenClipPro-(\d+\.\d+\.\d+)-win64\.zip$'
if (-not $Zip) {
    $dir = Join-Path $env:LOCALAPPDATA 'GenClipPro\releases'
    $best = Get-ChildItem -LiteralPath $dir -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -match $pattern } |
        Sort-Object { [version]($_.Name -replace $pattern, '$1') } | Select-Object -Last 1
    if (-not $best) { throw "No hay ninguna release en $dir`: créala con tools\release\empaquetar.ps1" }
    $Zip = $best.FullName
}
$Zip = (Resolve-Path -LiteralPath $Zip).Path

# Como el actualizador: sin su .sha256, o si no coincide, no se instala.
$shaFile = "$Zip.sha256"
if (-not (Test-Path -LiteralPath $shaFile)) { throw "Falta $shaFile`: sin él no se instala" }
$expected = (((Get-Content -LiteralPath $shaFile -Raw) -replace '^﻿', '').Trim() -split '\s+')[0].ToLowerInvariant()
$actual = (Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash.ToLowerInvariant()
if ($expected -ne $actual) { throw "$Zip no coincide con su .sha256: no se instala" }

$running = @(Get-Process -Name genclip-studio -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path -like "$Destino\*" })
if ($running.Count -gt 0) { throw 'Cierra GenClipPro (la de producción) antes de instalar' }

# Se descomprime al lado y se cambia la carpeta entera, como hace el actualizador.
$new = "$Destino.nueva"
$old = "$Destino.vieja"
foreach ($d in $new, $old) { if (Test-Path -LiteralPath $d) { Remove-Item -LiteralPath $d -Recurse -Force } }
New-Item -ItemType Directory -Path $new -Force | Out-Null
& (Join-Path $env:SystemRoot 'System32\tar.exe') -xf $Zip -C $new
if ($LASTEXITCODE -ne 0) { throw "tar no pudo descomprimir $Zip (código $LASTEXITCODE)" }
foreach ($f in 'genclip-studio.exe', 'genclip-release.json') {
    if (-not (Test-Path -LiteralPath (Join-Path $new $f))) { Remove-Item -LiteralPath $new -Recurse -Force; throw "El .zip no trae $f`: no es una release" }
}
if (Test-Path -LiteralPath $Destino) { Move-Item -LiteralPath $Destino -Destination $old }
Move-Item -LiteralPath $new -Destination $Destino
if (Test-Path -LiteralPath $old) { Remove-Item -LiteralPath $old -Recurse -Force }
$version = (Get-Content -LiteralPath (Join-Path $Destino 'genclip-release.json') -Raw | ConvertFrom-Json).version

# Los accesos del menú Inicio.
$menu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs'
$shell = New-Object -ComObject WScript.Shell
function Set-Shortcut([string]$Name, [string]$Target, [string]$Description) {
    $lnk = $shell.CreateShortcut((Join-Path $menu "$Name.lnk"))
    $lnk.TargetPath = $Target
    $lnk.WorkingDirectory = Split-Path -Parent $Target
    $lnk.Description = $Description
    $lnk.Save()
}
Set-Shortcut 'GenClipPro' (Join-Path $Destino 'genclip-studio.exe') 'GenClipPro: la de producción, se actualiza desde Ajustes › Versión'
if (Test-Path -LiteralPath $Dev) {
    Set-Shortcut 'GenClipPro Dev' (Resolve-Path -LiteralPath $Dev).Path 'GenClipPro Dev: la build de desarrollo, la última que se compila'
    $devNote = '«GenClipPro Dev» (' + (Resolve-Path -LiteralPath $Dev).Path + ')'
} else {
    Write-Warning "No está la build de desarrollo ($Dev): sin acceso «GenClipPro Dev»."
    $devNote = $null
}
Write-Host "Instalada la $version en $Destino."
Write-Host ('En el menú Inicio: «GenClipPro»' + $(if ($devNote) { " y $devNote" } else { '' }) + '.')
