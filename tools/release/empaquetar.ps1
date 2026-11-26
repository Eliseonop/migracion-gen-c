<#
Empaqueta GenClipPro para Windows en un .zip portátil: la «release local».

Compila genclip-studio en Release (RelWithDebInfo, con el preset vs-release de CMakeUserPresets.json o el que
digas con -Preset) y monta la carpeta portátil: el .exe con sus DLL, los plugins y los módulos QML de Qt con su
qt.conf, el runtime de MSVC y genclip-release.json, que la marca como la de producción (apps/studio/updater.hpp).
Comprueba que no lleva ajustes ni ninguna de tus claves y que arranca sola desde el .zip, y deja
GenClipPro-<versión>-win64.zip y su .sha256 en las releases locales (%LOCALAPPDATA%\GenClipPro\releases). La de
producción las mira ahí antes que en GitHub (Ajustes › Versión) y tools\release\instalar.ps1 instala la más nueva.

  powershell -ExecutionPolicy Bypass -File tools\release\empaquetar.ps1 [-SinCompilar] [-Preset vs-release]
      [-Salida <carpeta>] [-ConClaves <carpetas de video-yt>]

-SinCompilar empaqueta lo que ya hay en build\<preset>. Las claves que se buscan en el paquete son las de los
settings.json y .env de la carpeta de los proyectos de GenClipPro y de -ConClaves; nunca se escriben.
#>
param(
    [switch]$SinCompilar,
    [string]$Preset = 'vs-release',
    [string]$Salida = (Join-Path $env:LOCALAPPDATA 'GenClipPro\releases'),
    [string[]]$ConClaves = @('E:\proyectos\video-yt')
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$build = Join-Path $repo "build\$Preset"
$cmakeLists = Get-Content -LiteralPath (Join-Path $repo 'CMakeLists.txt') -Raw
if ($cmakeLists -notmatch 'project\(GenClipPro\s+VERSION\s+(\d+\.\d+\.\d+)') { throw 'No encuentro la versión en CMakeLists.txt' }
$version = $Matches[1]
$name = "GenClipPro-$version-win64"
Write-Host "== $name"

# El Visual Studio con MSVC y CMake (el de vswhere).
function Find-VisualStudio {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'No encuentro vswhere: hace falta Visual Studio con MSVC' }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath
    if (-not $vs) { throw 'No hay ningún Visual Studio con MSVC y CMake' }
    return @($vs)[0]
}

# Una carpeta entera sin los .pdb ni las carpetas `Skip` (robocopy).
function Copy-Folder([string]$From, [string]$To, [string[]]$Skip = @()) {
    $opts = @($From, $To, '/E', '/XF', '*.pdb', '/NFL', '/NDL', '/NJH', '/NJS', '/NP', '/R:1', '/W:1')
    if ($Skip.Count -gt 0) { $opts += '/XD'; $opts += $Skip }
    & robocopy.exe @opts | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy no pudo copiar $From (código $LASTEXITCODE)" }
}

$vs = Find-VisualStudio
if (-not $SinCompilar) {
    # Con la consola en UTF-8: si no, ninja no apunta de qué cabeceras depende cada objeto (MSVC en español).
    $bat = Join-Path $env:TEMP "genclip-empaquetar-$PID.bat"
    Set-Content -LiteralPath $bat -Encoding Ascii -Value @(
        '@echo off',
        'chcp 65001 >nul',
        "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul || exit /b 1",
        "cd /d `"$repo`" || exit /b 1",
        "cmake --preset $Preset || exit /b 1",
        "cmake --build `"$build`" --target genclip-studio || exit /b 1")
    & cmd.exe /c $bat
    $code = $LASTEXITCODE
    Remove-Item -LiteralPath $bat -ErrorAction SilentlyContinue
    if ($code -ne 0) { throw "No compiló (código $code)" }
}

$bin = Join-Path $build 'apps\studio'
$exe = Join-Path $bin 'genclip-studio.exe'
$qt = Join-Path $build 'vcpkg_installed\x64-windows\Qt6'
if (-not (Test-Path -LiteralPath $exe)) { throw "No está $exe (quita -SinCompilar)" }
if (Test-Path -LiteralPath (Join-Path $bin 'Qt6Cored.dll')) { throw "$bin tiene las DLL de Debug: hace falta un preset Release" }

# La carpeta portátil.
$stage = Join-Path $env:TEMP $name
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath $exe -Destination $stage
Get-ChildItem -LiteralPath $bin -Filter *.dll | Copy-Item -Destination $stage
# Los plugins: la plataforma (también offscreen), las imágenes, TLS (el actualizador va a GitHub por HTTPS) y la red.
foreach ($p in 'platforms', 'imageformats', 'iconengines', 'tls', 'generic', 'networkinformation') {
    if (Test-Path -LiteralPath (Join-Path $qt "plugins\$p")) { Copy-Folder (Join-Path $qt "plugins\$p") (Join-Path $stage "plugins\$p") }
}
# Los módulos QML, sin lo de desarrollo (qmllint, el diseñador, QtTest).
Copy-Folder (Join-Path $qt 'qml') (Join-Path $stage 'qml') @('tooling', 'designer', 'QtTest')
Set-Content -LiteralPath (Join-Path $stage 'qt.conf') -Encoding Ascii -Value @('[Paths]', 'Prefix=.', 'Plugins=plugins', 'QmlImports=qml')
# El runtime de MSVC, para que arranque también en un PC sin el Visual C++ Redistributable.
$redist = Get-ChildItem -LiteralPath (Join-Path $vs 'VC\Redist\MSVC') -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } | Sort-Object { [version]$_.Name } | Select-Object -Last 1
$crt = if ($redist) { Get-ChildItem -LiteralPath (Join-Path $redist.FullName 'x64') -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue | Select-Object -First 1 }
if ($crt) {
    Get-ChildItem -LiteralPath $crt.FullName -Filter *.dll | Copy-Item -Destination $stage
} else {
    Write-Warning 'Sin el runtime de MSVC (VC\Redist): en otro PC hará falta el Visual C++ Redistributable.'
}

# La marca de la de producción: la versión, de qué repositorio salen las siguientes y de qué commit es esta.
$commit = (& git -C $repo rev-parse HEAD).Trim()
$dirty = [bool](& git -C $repo status --porcelain)
if ($dirty) { Write-Warning 'Hay cambios sin commit: la release no corresponde a ningún commit.' }
$origin = (& git -C $repo remote get-url origin)
$repoName = if ($origin -match 'github\.com[:/](.+?)(\.git)?$') { $Matches[1] } else { 'Eliseonop/migracion-gen-c' }
[ordered]@{ app = 'GenClipPro'; version = $version; repo = $repoName; commit = $commit; dirty = $dirty; built = (Get-Date).ToString('s') } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stage 'genclip-release.json') -Encoding Ascii

# Ni ajustes ni .env ni certificados.
$files = @(Get-ChildItem -LiteralPath $stage -Recurse -File)
$bad = @($files | Where-Object { $_.Name -in @('settings.json', 'studio.json', 'projects.json') -or $_.Name -like '.env*' -or $_.Extension -in @('.pem', '.key', '.pfx') })
if ($bad.Count -gt 0) { throw ('El paquete lleva archivos que no debe: ' + (($bad | ForEach-Object { $_.FullName.Substring($stage.Length + 1) }) -join ', ')) }

# Ninguna de tus claves en ningún archivo: las de los settings.json y .env que conozco (solo en memoria).
$secrets = @{}
function Add-Secret([object]$Value, [string]$Provider) {
    if ($Value -is [string] -and $Value.Trim().Length -ge 12) { $secrets[$Value.Trim()] = $Provider }
}
function Read-SettingsKeys([string]$File) {
    if (-not (Test-Path -LiteralPath $File)) { return }
    $j = Get-Content -LiteralPath $File -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($j.PSObject.Properties['api_keys'] -and $j.api_keys) {
        foreach ($p in $j.api_keys.PSObject.Properties) { Add-Secret $p.Value $p.Name }
    }
    if ($j.PSObject.Properties['api_keys_extra'] -and $j.api_keys_extra) {
        foreach ($p in $j.api_keys_extra.PSObject.Properties) { foreach ($k in @($p.Value)) { Add-Secret $k $p.Name } }
    }
    if ($j.PSObject.Properties['gemini_api_key']) { Add-Secret $j.gemini_api_key 'gemini' }
}
function Read-EnvKeys([string]$File) {
    if (-not (Test-Path -LiteralPath $File)) { return }
    foreach ($line in Get-Content -LiteralPath $File) {
        if ($line -match '^\s*(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*(?:KEY|TOKEN))\s*=\s*["'']?([^"''#\s]+)') { Add-Secret $Matches[2] $Matches[1] }
    }
}
$roots = @(Join-Path $env:APPDATA 'GenClipPro')
$studioJson = Join-Path $env:LOCALAPPDATA 'GenClipPro\studio.json'
if (Test-Path -LiteralPath $studioJson) {
    $chosen = Get-Content -LiteralPath $studioJson -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($chosen.PSObject.Properties['root'] -and $chosen.root) { $roots += $chosen.root }
}
foreach ($dir in @($roots + $ConClaves + $repo)) {
    foreach ($base in $dir, (Join-Path $dir 'backend')) {
        Read-SettingsKeys (Join-Path $base 'data\settings.json')
        Read-EnvKeys (Join-Path $base '.env')
    }
}
if ($secrets.Count -gt 0) {
    $hits = @($files | Select-String -SimpleMatch -CaseSensitive -List -Pattern @($secrets.Keys))
    if ($hits.Count -gt 0) {
        throw ('El paquete lleva claves (de ' + (($hits | ForEach-Object { $_.Path.Substring($stage.Length + 1) + ': ' + $secrets[$_.Pattern] }) -join ', ') + ')')
    }
}
# Y nada con pinta de clave en los archivos de texto.
$text = @($files | Where-Object { $_.Extension -in @('.json', '.conf', '.qml', '.js', '.txt', '.ini') -or $_.Name -eq 'qmldir' })
$looks = @($text | Select-String -CaseSensitive -List -Pattern 'AIza[0-9A-Za-z_\-]{35}', 'sk-[A-Za-z0-9_\-]{20,}', 'gsk_[A-Za-z0-9]{20,}', 'hf_[A-Za-z0-9]{20,}')
if ($looks.Count -gt 0) { throw ('Algo con pinta de clave en ' + (($looks | ForEach-Object { $_.Path.Substring($stage.Length + 1) }) -join ', ')) }
Write-Host ("Sin claves: comprobadas " + $secrets.Count + " tuyas en " + $files.Count + " archivos.")

# El .zip (con tar.exe de Windows) y su .sha256.
New-Item -ItemType Directory -Force -Path $Salida | Out-Null
$zip = Join-Path $Salida "$name.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
$tar = Join-Path $env:SystemRoot 'System32\tar.exe'
$items = @(Get-ChildItem -LiteralPath $stage | ForEach-Object { $_.Name })
& $tar -a -c -f $zip -C $stage @items
if ($LASTEXITCODE -ne 0) { throw "tar no pudo crear $zip (código $LASTEXITCODE)" }
$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -LiteralPath "$zip.sha256" -Encoding Ascii -Value "$hash  $name.zip"

# Que arranca sola desde el .zip: sin pantalla, con una carpeta de proyectos vacía, abre Ajustes y guarda una captura.
$probe = Join-Path $env:TEMP "$name-prueba"
if (Test-Path -LiteralPath $probe) { Remove-Item -LiteralPath $probe -Recurse -Force }
New-Item -ItemType Directory -Path (Join-Path $probe 'app') | Out-Null
& $tar -xf $zip -C (Join-Path $probe 'app')
if ($LASTEXITCODE -ne 0) { throw "tar no pudo descomprimir $zip (código $LASTEXITCODE)" }
$saved = @{}
foreach ($v in 'QT_QPA_PLATFORM', 'QT_QPA_FONTDIR', 'QT_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH') { $saved[$v] = [Environment]::GetEnvironmentVariable($v) }
$grab = Join-Path $probe 'ajustes.png'
try {
    $env:QT_QPA_PLATFORM = 'offscreen'
    # Sin pantalla, Qt en Windows no mira las fuentes del sistema (como en los tests): sin esto, la captura sale sin letras.
    $env:QT_QPA_FONTDIR = Join-Path $env:SystemRoot 'Fonts'
    foreach ($v in 'QT_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH') { Remove-Item -LiteralPath "Env:$v" -ErrorAction SilentlyContinue }
    $proc = Start-Process -FilePath (Join-Path $probe 'app\genclip-studio.exe') -PassThru -WorkingDirectory $probe `
        -ArgumentList '--root', "`"$(Join-Path $probe 'proyectos')`"", '--settings', '--grab', "`"$grab`""
    $null = $proc.Handle # para que ExitCode tenga valor
    Wait-Process -Id $proc.Id -Timeout 120 -ErrorAction SilentlyContinue
    if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; throw 'La del .zip no salió en 2 minutos' }
    if ($proc.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $grab)) { throw "La del .zip no arranca sola (código $($proc.ExitCode))" }
} finally {
    foreach ($v in $saved.Keys) { [Environment]::SetEnvironmentVariable($v, $saved[$v]) }
}
Copy-Item -LiteralPath $grab -Destination (Join-Path $Salida "$name-ajustes.png") -Force
Remove-Item -LiteralPath $probe -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue

$mb = [math]::Round((Get-Item -LiteralPath $zip).Length / 1MB, 1)
Write-Host "Lista: $zip ($mb MB, $($files.Count) archivos)"
Write-Host "SHA-256: $hash"
Write-Host "Arranca sola: captura de Ajustes en $(Join-Path $Salida "$name-ajustes.png")"
Write-Host "Instalarla: tools\release\instalar.ps1. Publicarla: gh release create v$version `"$zip`" `"$zip.sha256`""
