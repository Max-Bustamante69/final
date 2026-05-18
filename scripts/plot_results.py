import csv
from pathlib import Path

import matplotlib.pyplot as plt


ROOT = Path(__file__).resolve().parents[1]
CSV_PATH = ROOT / "docs" / "metrics.csv"
DOCS = ROOT / "docs"


def load_rows():
    rows = []
    with CSV_PATH.open("r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append(
                {
                    "scenario": row["scenario"],
                    "input_mb": float(row["input_bytes"]) / (1024 * 1024),
                    "output_mb": float(row["output_bytes"]) / (1024 * 1024),
                    "wall_ms": float(row["wall_ms"]),
                    "cpu_ms": float(row["cpu_ms"]),
                    "io_wait_ms_est": float(row["io_wait_ms_est"]),
                }
            )
    return rows


def save_size_chart(rows):
    labels = [r["scenario"] for r in rows]
    output_mb = [r["output_mb"] for r in rows]
    input_mb = [r["input_mb"] for r in rows]

    plt.figure(figsize=(10, 5))
    plt.bar(labels, output_mb, color=["#4C78A8", "#72B7B2", "#54A24B", "#E45756"])
    plt.plot(labels, input_mb, color="black", marker="o", linewidth=1.5, label="Tamano de entrada")
    plt.title("Tamano transmitido por escenario")
    plt.ylabel("MB")
    plt.xticks(rotation=15, ha="right")
    plt.legend()
    plt.tight_layout()
    plt.savefig(DOCS / "grafica_tamano.png", dpi=140)
    plt.close()


def save_time_chart(rows):
    labels = [r["scenario"] for r in rows]
    cpu = [r["cpu_ms"] for r in rows]
    io_wait = [r["io_wait_ms_est"] for r in rows]

    x = range(len(labels))
    plt.figure(figsize=(10, 5))
    plt.bar(x, cpu, label="CPU ms", color="#F58518")
    plt.bar(x, io_wait, bottom=cpu, label="Espera I/O estimada ms", color="#B279A2")
    plt.xticks(list(x), labels, rotation=15, ha="right")
    plt.title("Descomposicion de tiempo total")
    plt.ylabel("Milisegundos")
    plt.legend()
    plt.tight_layout()
    plt.savefig(DOCS / "grafica_tiempos.png", dpi=140)
    plt.close()


def save_wall_chart(rows):
    labels = [r["scenario"] for r in rows]
    wall = [r["wall_ms"] for r in rows]

    plt.figure(figsize=(10, 5))
    plt.bar(labels, wall, color=["#4C78A8", "#72B7B2", "#54A24B", "#E45756"])
    plt.title("Tiempo total (wall-clock) por escenario")
    plt.ylabel("Milisegundos")
    plt.xticks(rotation=15, ha="right")
    plt.tight_layout()
    plt.savefig(DOCS / "grafica_wall.png", dpi=140)
    plt.close()


def main():
    if not CSV_PATH.exists():
        raise FileNotFoundError(f"No existe {CSV_PATH}. Ejecuta benchmark.py primero.")
    DOCS.mkdir(parents=True, exist_ok=True)
    rows = load_rows()
    save_size_chart(rows)
    save_time_chart(rows)
    save_wall_chart(rows)
    print("Graficas generadas en docs/")


if __name__ == "__main__":
    main()
