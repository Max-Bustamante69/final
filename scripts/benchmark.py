import csv
import hashlib
import os
import re
import statistics
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "bin" / "pipeline.exe"
SRC = ROOT / "src" / "pipeline.c"
DATA = ROOT / "data" / "test_50mb.txt"
OUT_DIR = ROOT / "out"
CSV_OUT = ROOT / "docs" / "metrics.csv"
RUNS = 5
KEY = "ClaveSegura-SO-2026"

METRIC_RE = re.compile(
    r"METRICS mode=(?P<mode>\S+) input_bytes=(?P<input>\d+) output_bytes=(?P<output>\d+) "
    r"wall_ms=(?P<wall>[0-9.]+) cpu_ms=(?P<cpu>[0-9.]+) compress_ms=(?P<comp>[0-9.]+) "
    r"decompress_ms=(?P<decomp>[0-9.]+) encrypt_ms=(?P<enc>[0-9.]+) decrypt_ms=(?P<dec>[0-9.]+)"
)


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def run_cmd(args: list[str], env: dict[str, str] | None = None) -> str:
    proc = subprocess.run(
        args,
        cwd=ROOT,
        capture_output=True,
        text=True,
        env=env,
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError(f"Fallo comando {' '.join(args)}\nSTDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}")
    return proc.stdout + proc.stderr


def ensure_binary() -> None:
    (ROOT / "bin").mkdir(parents=True, exist_ok=True)
    run_cmd(["gcc", "-O2", "-std=c11", "-Wall", "-Wextra", "-pedantic", "-o", str(BIN), str(SRC)])


def parse_metrics(output: str) -> dict[str, float]:
    m = METRIC_RE.search(output)
    if not m:
        raise ValueError(f"No se encontraron metricas en salida:\n{output}")
    return {
        "input_bytes": float(m.group("input")),
        "output_bytes": float(m.group("output")),
        "wall_ms": float(m.group("wall")),
        "cpu_ms": float(m.group("cpu")),
        "compress_ms": float(m.group("comp")),
        "decompress_ms": float(m.group("decomp")),
        "encrypt_ms": float(m.group("enc")),
        "decrypt_ms": float(m.group("dec")),
    }


def avg_rows(rows: list[dict[str, float]], scenario: str) -> dict[str, float | str]:
    out: dict[str, float | str] = {"scenario": scenario}
    for key in rows[0]:
        out[key] = statistics.mean(r[key] for r in rows)
    out["io_wait_ms_est"] = max(0.0, float(out["wall_ms"]) - float(out["cpu_ms"]))
    return out


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    (ROOT / "docs").mkdir(parents=True, exist_ok=True)

    if not DATA.exists():
        raise FileNotFoundError(f"No existe {DATA}. Ejecuta scripts/generate_data.py primero.")

    ensure_binary()
    original_hash = sha256_file(DATA)

    scenarios = {
        "A_classic_copy": ("classic-copy", DATA, OUT_DIR / "classic.bin"),
        "B_compress_only": ("compress-only", DATA, OUT_DIR / "compressed.rle"),
        "C_compress_then_encrypt": ("ce-encode", DATA, OUT_DIR / "ce.bin"),
        "D_encrypt_then_compress": ("ec-encode", DATA, OUT_DIR / "ec.bin"),
    }

    all_rows: list[dict[str, float | str]] = []
    env = os.environ.copy()
    env["PIPELINE_KEY"] = KEY

    for scenario, (mode, input_path, output_path) in scenarios.items():
        rows: list[dict[str, float]] = []
        for _ in range(RUNS):
            output = run_cmd([str(BIN), mode, str(input_path), str(output_path)], env=env)
            rows.append(parse_metrics(output))
        all_rows.append(avg_rows(rows, scenario))

    # Verificacion de integridad de ambos pipelines.
    run_cmd([str(BIN), "ce-decode", str(OUT_DIR / "ce.bin"), str(OUT_DIR / "ce_restored.txt")], env=env)
    run_cmd([str(BIN), "ec-decode", str(OUT_DIR / "ec.bin"), str(OUT_DIR / "ec_restored.txt")], env=env)
    run_cmd([str(BIN), "decompress-only", str(OUT_DIR / "compressed.rle"), str(OUT_DIR / "compressed_restored.txt")], env=env)

    if sha256_file(OUT_DIR / "ce_restored.txt") != original_hash:
        raise RuntimeError("Integridad fallida para pipeline CE.")
    if sha256_file(OUT_DIR / "ec_restored.txt") != original_hash:
        raise RuntimeError("Integridad fallida para pipeline EC.")
    if sha256_file(OUT_DIR / "compressed_restored.txt") != original_hash:
        raise RuntimeError("Integridad fallida para compresion sola.")

    fieldnames = [
        "scenario",
        "input_bytes",
        "output_bytes",
        "wall_ms",
        "cpu_ms",
        "compress_ms",
        "decompress_ms",
        "encrypt_ms",
        "decrypt_ms",
        "io_wait_ms_est",
    ]
    with CSV_OUT.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(all_rows)

    print(f"Benchmark completado. CSV: {CSV_OUT}")


if __name__ == "__main__":
    main()
