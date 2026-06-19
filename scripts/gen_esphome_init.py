#!/usr/bin/env python3
"""Port the SH8601 init sequence from components/bsp/display.c into the ESPHome
qspi_dbi `init_sequence:` block of esphome/knob.yaml.

components/bsp/display.c stays the single source of truth for the panel init —
re-run this after changing lcd_init_cmds[]. Usage: uv run python scripts/gen_esphome_init.py
"""
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DISPLAY_C = os.path.join(ROOT, "components", "bsp", "display.c")
YAML = os.path.join(ROOT, "esphome", "knob.yaml")

# {0xF0, (uint8_t[]){0x28}, 1, 0},  ->  cmd, "0x28", data_bytes, delay_ms
ENTRY = re.compile(
    r"\{\s*(0x[0-9A-Fa-f]+)\s*,\s*\(uint8_t\[\]\)\s*\{([^}]*)\}\s*,\s*(\d+)\s*,\s*(\d+)\s*\}"
)


def main():
    src = open(DISPLAY_C).read()
    # Only the lcd_init_cmds[] array body.
    body = src[src.index("lcd_init_cmds[]") : src.index("};", src.index("lcd_init_cmds[]"))]

    lines = ["      # Generated from components/bsp/display.c by scripts/gen_esphome_init.py.",
             "      # Edit the init there (single source of truth), then re-run that script."]
    n = 0
    for cmd, data, nbytes, delay in ENTRY.findall(body):
        nbytes = int(nbytes)
        bytes_ = [b.strip() for b in data.split(",") if b.strip()][:nbytes]
        row = ", ".join([cmd] + bytes_)
        lines.append(f"      - [{row}]")
        if int(delay) > 0:
            lines.append(f"      - delay {int(delay)}ms")
        n += 1

    block = "\n".join(lines) + "\n"

    # Replace everything between `    init_sequence:` and the next 4-space key.
    yaml = open(YAML).read()
    start = yaml.index("    init_sequence:\n") + len("    init_sequence:\n")
    end = yaml.index("    auto_clear_enabled:", start)
    yaml = yaml[:start] + block + yaml[end:]
    open(YAML, "w").write(yaml)
    print(f"wrote {n} init commands into {os.path.relpath(YAML, ROOT)}")


if __name__ == "__main__":
    main()
