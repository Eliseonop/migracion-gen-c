"""Tiempo del export de video-yt (compose.render) sobre un proyecto real, a un MP4 fuera de video-yt.

    <video-yt>/.venv/Scripts/python.exe tools/dev/bench_export.py <backend> <id> <salida.mp4> [nvenc]

`backend`: la carpeta `backend` de video-yt. Solo lee video-yt: el nivel 2 de la caché de
«Eliminar fondo» (los mattes derivados) y los horneados de los efectos de vídeo (`fx_bake`, que
escribiría en `data/fxcache`) van a carpetas temporales que se borran al acabar; así un proyecto
con efectos de vídeo los hornea en cada medida, como en su primer export. Los mattes, en cambio,
se derivan antes (como en el primer export, cronometrado aparte) y compose.render ya los
reutiliza (como en un export repetido). Con `nvenc`, el export usa NVENC aunque la sonda
de gpu.py lo descarte (codifica 64×64, por debajo del mínimo de NVENC): se fuerza en memoria.
Lo usa tools/dev/benchmark.ps1.
"""
import json
import shutil
import sys
import tempfile
import time
from pathlib import Path


def main() -> None:
    backend, pid, out = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
    sys.path.insert(0, str(backend))
    from app import compose, migrations, schemas  # noqa: PLC0415
    from app import fx_bake  # noqa: PLC0415
    from app.bg import service as bg_service  # noqa: PLC0415

    if len(sys.argv) > 4 and sys.argv[4] == "nvenc":
        from app import gpu  # noqa: PLC0415
        gpu.hw_encoder = lambda: "h264_nvenc"
    data = json.loads((backend / "data" / "projects.json").read_text(encoding="utf-8"))
    items = data if isinstance(data, list) else data.get("projects", data)
    raw = next(p for p in (items.values() if isinstance(items, dict) else items) if p.get("id") == pid)
    raw = dict(raw)
    raw["timeline"] = migrations.migrate_timeline(raw["timeline"])
    project = schemas.Project.model_validate(raw)
    timeline = project.timeline
    masks = Path(tempfile.mkdtemp(prefix="gc_bench_mask_"))
    fxcache = Path(tempfile.mkdtemp(prefix="gc_bench_fx_"))
    bg_service.MASK_ROOT = masks
    fx_bake.CACHE_DIR = fxcache
    try:
        t0 = time.perf_counter()
        specs = bg_service.build_timeline_bg_masks(timeline, int(timeline.fps or 30))
        t_bg = time.perf_counter() - t0
        t0 = time.perf_counter()
        compose.render(project, timeline, out, lambda f, m: None)
        t_render = time.perf_counter() - t0
    finally:
        shutil.rmtree(masks, ignore_errors=True)
        shutil.rmtree(fxcache, ignore_errors=True)
    print(f"{out}: compose.render {t_render:.1f} s; derivar los mattes la primera vez "
          f"({len(specs)} clips) {t_bg:.1f} s")


if __name__ == "__main__":
    main()
