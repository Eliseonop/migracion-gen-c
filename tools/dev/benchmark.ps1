# Benchmark del export: genclip render (con el codificador que elija y con OpenH264) frente a
# compose.render de video-yt (x264, el de hoy, y NVENC forzado), proyecto a proyecto.
#
#   powershell -File tools\dev\benchmark.ps1 [-Projects 38b3e5bd,c1dcc884] [-Out carpeta] [-Force]
#
# Usa la build Release de MSVC (build\msvc-vcpkg-release; compílala antes) y el Python de video-yt.
# Con el PC ocupado (un proceso con más de 1,5 núcleos o más de un 15 % de carga) no mide: las
# cifras no valdrían. -Force mide igual (para probar el script, no para dar cifras). Los MP4
# quedan en -Out (por defecto %TEMP%\gc_bench).
param(
    [string[]]$Projects = @("38b3e5bd", "c1dcc884"),
    [string]$Out = (Join-Path $env:TEMP "gc_bench"),
    [string]$VideoYt = "E:\proyectos\video-yt",
    [switch]$Force
)
# Continue, no Stop: en PowerShell 5.1, `2>&1` sobre un ejecutable convierte cada línea de stderr
# (los avisos de genclip, los logs de Python) en un error que con Stop aborta el script.
$ErrorActionPreference = "Continue"
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe = Join-Path $repo "build\msvc-vcpkg-release\apps\cli\genclip.exe"
$py = Join-Path $VideoYt ".venv\Scripts\python.exe"
$backend = Join-Path $VideoYt "backend"
$bench = Join-Path $PSScriptRoot "bench_export.py"
foreach ($f in @($exe, $py, $bench)) {
    if (-not (Test-Path $f)) {
        "no existe: $f"
        exit 1
    }
}
New-Item -ItemType Directory -Force $Out | Out-Null

function Cpu() { (Get-CimInstance Win32_PerfFormattedData_PerfOS_Processor -Filter "Name='_Total'").PercentProcessorTime }

function Assert-Quiet() {
    $busy = Get-CimInstance Win32_PerfFormattedData_PerfProc_Process |
        Where-Object { $_.Name -notin '_Total', 'Idle' -and $_.PercentProcessorTime -ge 150 }
    $load = [math]::Round(((1..5 | ForEach-Object { Cpu; Start-Sleep -Milliseconds 400 }) | Measure-Object -Average).Average)
    if ($busy -or $load -gt 15) {
        $who = ($busy | ForEach-Object { $_.Name }) -join ', '
        if (-not $Force) {
            "PC ocupado (carga media $load %; procesos: $who): no se mide"
            exit 2
        }
        "aviso: PC ocupado (carga media $load %; procesos: $who): estas cifras no valen"
        return
    }
    "carga antes: $load %"
}

# Ejecuta un programa con su stderr junto al stdout, sin que una línea de stderr pare el script.
function Invoke-Native([string]$file, [string[]]$arguments) {
    & $file @arguments 2>&1 | ForEach-Object { "$_" }
}

foreach ($id in $Projects) {
    "== $id"
    foreach ($run in @(@{ n = "C++ (auto)"; a = @() }, @{ n = "C++ OpenH264"; a = @("--codificador", "libopenh264") })) {
        Assert-Quiet
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $r = Invoke-Native $exe (@("render", $backend, $id, "-o", (Join-Path $Out "${id}_cpp.mp4"), "--tiempos") + $run.a)
        "$($run.n): $([math]::Round($sw.Elapsed.TotalSeconds, 1)) s (código $LASTEXITCODE)"
        $r | Where-Object { $_ -match "fotogramas|tiempos|error|aviso" }
    }
    foreach ($run in @(@{ n = "Python x264"; a = @() }, @{ n = "Python NVENC"; a = @("nvenc") })) {
        Assert-Quiet
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $r = Invoke-Native $py (@($bench, $backend, $id, (Join-Path $Out "${id}_py.mp4")) + $run.a)
        "$($run.n): $([math]::Round($sw.Elapsed.TotalSeconds, 1)) s (código $LASTEXITCODE)"
        $r | Where-Object { $_ -match "compose.render|Error|Traceback" }
    }
}
