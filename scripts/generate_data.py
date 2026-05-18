from pathlib import Path


TARGET_BYTES = 50 * 1024 * 1024
OUTPUT = Path("data/test_50mb.txt")


def main() -> None:
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    chunk = (
        b"A" * 16384
        + b"B" * 16384
        + b"C" * 16384
        + b"D" * 16384
        + b"SISTEMAS_OPERATIVOS_C2661\n"
    )

    with OUTPUT.open("wb") as f:
        written = 0
        while written < TARGET_BYTES:
            remaining = TARGET_BYTES - written
            part = chunk if remaining >= len(chunk) else chunk[:remaining]
            f.write(part)
            written += len(part)

    print(f"Archivo generado: {OUTPUT} ({TARGET_BYTES} bytes)")


if __name__ == "__main__":
    main()
