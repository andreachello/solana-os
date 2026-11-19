#!/usr/bin/env python3
"""Write a stand-in firmware/sdkconfig for emulator builds on a machine that
has no ESP-IDF toolchain (so no `idf.py reconfigure`).

The emulator only needs the CONFIG_* values LVGL and the portable firmware
read (emulator/tools/gen_sdkconfig.py turns sdkconfig into sdkconfig.h). This
resolves LVGL's own Kconfig and main/Kconfig.projbuild with kconfiglib, over
sdkconfig.defaults, and adds the few ESP-IDF values the shims read at their
ESP-IDF defaults. A real `idf.py reconfigure` overwrites the file and is
always the better source: use this only when idf.py is not installed.

    pip install kconfiglib
    python3 emulator/local/gen_standin_sdkconfig.py     # writes ./sdkconfig, never over a real one
"""
import os
import sys
import tempfile

import kconfiglib

FW = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(FW, "sdkconfig")
MARK = "# Stand-in written by emulator/local/gen_standin_sdkconfig.py (no ESP-IDF here)."

# What ESP-IDF itself would define and the emulator's shims or the firmware
# read, at ESP-IDF v6.0.1's defaults for this board (unless sdkconfig.defaults
# says otherwise, which wins below).
IDF_DEFAULTS = {
    "CONFIG_ESP_MAIN_TASK_STACK_SIZE": "3584",
    "CONFIG_FREERTOS_HZ": "100",
    "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ": "240",
    "CONFIG_ESP_CONSOLE_UART_NUM": "0",
    "CONFIG_APP_PROJECT_VER": '"local"',
}


def main():
    if os.path.exists(OUT) and MARK not in open(OUT).read():
        sys.exit(f"{OUT} exists and is a real one (from idf.py): leaving it alone.")
    lvgl = os.path.join(FW, "managed_components", "lvgl__lvgl", "Kconfig")
    if not os.path.exists(lvgl):
        sys.exit("managed_components/lvgl__lvgl/Kconfig is missing: fetch LVGL first.")

    with tempfile.NamedTemporaryFile("w", suffix=".Kconfig", delete=False) as top:
        top.write('mainmenu "Solana OS emulator stand-in"\n')
        top.write(f'source "{lvgl}"\n')
        top.write(f'source "{os.path.join(FW, "main", "Kconfig.projbuild")}"\n')
    # LVGL's Kconfig tests IDF_* symbols that only exist under ESP-IDF.
    os.environ.setdefault("IDF_TARGET", "esp32c5")
    kconf = kconfiglib.Kconfig(top.name, warn=False)
    os.unlink(top.name)

    # sdkconfig.defaults: the symbols these Kconfigs know go through
    # kconfiglib (so dependent defaults follow); the rest are copied as they are.
    passthrough = {}
    known = []
    for raw in open(os.path.join(FW, "sdkconfig.defaults")):
        line = raw.strip()
        if not line.startswith("CONFIG_") or "=" not in line:
            continue
        key, val = line.split("=", 1)
        if key[len("CONFIG_"):] in kconf.syms:
            known.append(line)
        else:
            passthrough[key] = val
    with tempfile.NamedTemporaryFile("w", suffix=".defaults", delete=False) as d:
        d.write("\n".join(known) + "\n")
    kconf.load_config(d.name, replace=False)
    os.unlink(d.name)

    with tempfile.NamedTemporaryFile("w", suffix=".config", delete=False) as resolved:
        pass
    kconf.write_config(resolved.name)
    body = open(resolved.name).read()
    os.unlink(resolved.name)

    extra = dict(IDF_DEFAULTS)
    extra.update(passthrough)
    with open(OUT, "w") as f:
        f.write(MARK + "\n# Not for the badge build: run `idf.py reconfigure` for a real one.\n")
        f.write(body)
        f.write("\n# ESP-IDF values (defaults, then sdkconfig.defaults)\n")
        for k in sorted(extra):
            f.write(f"{k}={extra[k]}\n")
    print(f"wrote {OUT}: {len(kconf.syms)} LVGL/firmware symbols, {len(extra)} ESP-IDF values")


if __name__ == "__main__":
    main()
