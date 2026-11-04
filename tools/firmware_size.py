"""Build the ATmega32 firmware with PlatformIO and record its flash/RAM use.

    python tools/firmware_size.py [path/to/pio]

Writes results/firmware_size.json from avr-size output of the real build.
"""
from __future__ import annotations

import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
FLASH, SRAM = 32768, 2048


def find_pio() -> str:
    if len(sys.argv) > 1:
        return sys.argv[1]
    for cand in (shutil.which("pio"), str(Path.home() / ".platformio/penv/Scripts/pio.exe"),
                 str(Path.home() / ".platformio/penv/bin/pio")):
        if cand and Path(cand).exists():
            return cand
    raise SystemExit("PlatformIO not found; pass the pio path as an argument")


def main() -> int:
    pio = find_pio()
    subprocess.run([pio, "run", "-d", str(REPO)], check=True)
    elf = REPO / ".pio" / "build" / "atmega32" / "firmware.elf"
    tools = Path.home() / ".platformio" / "packages" / "toolchain-atmelavr" / "bin"
    size = shutil.which("avr-size") or str(tools / "avr-size")
    text = subprocess.run([size, "-A", str(elf)], check=True, capture_output=True, text=True).stdout
    sec = {m.group(1): int(m.group(2)) for m in re.finditer(r"^\.(\w+)\s+(\d+)", text, re.M)}
    flash = sec.get("text", 0) + sec.get("data", 0)
    ram = sec.get("data", 0) + sec.get("bss", 0)
    out = {
        "toolchain": "avr-gcc 7.3.0 (PlatformIO toolchain-atmelavr), -Os, LTO",
        "mcu": "ATmega32 @ 16 MHz",
        "sections": {k: sec.get(k, 0) for k in ("text", "data", "bss", "eeprom")},
        "flash_bytes": flash, "flash_pct": round(100.0 * flash / FLASH, 1),
        "static_ram_bytes": ram, "static_ram_pct": round(100.0 * ram / SRAM, 1),
        "ram_left_for_stack_bytes": SRAM - ram,
        "note": "static RAM = .data + .bss; the stack is not included",
    }
    (REPO / "results").mkdir(exist_ok=True)
    (REPO / "results" / "firmware_size.json").write_text(json.dumps(out, indent=2), encoding="utf-8")
    print(json.dumps(out, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
