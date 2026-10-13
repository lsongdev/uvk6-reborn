#!/usr/bin/env python3
"""Report linked memory, excluding discarded sections and debug information.

With LTO, an input section is a function/constant table, not an original module.
.data consumes both a Flash load image and RAM; .bss consumes only RAM.
"""
import argparse
from pathlib import Path
import re

HEX = r"(0x[0-9a-fA-F]+)"


def analyze(filename):
    text = Path(filename).read_text(encoding="utf-8", errors="replace")
    regions = {}
    for name in ("FLASH", "RAM"):
        match = re.search(rf"^{name}\s+{HEX}\s+{HEX}", text, re.MULTILINE)
        if not match:
            raise ValueError(f"Missing {name} memory region")
        regions[name] = tuple(int(x, 16) for x in match.groups())
    live = text.split("Linker script and memory map", 1)[1]
    output, inputs = [], []
    pending = None
    for line in live.splitlines():
        match = re.match(r"^( ?)(\.[\w.$]+)(.*)$", line)
        if match:
            pending = (bool(match[1]), match[2])
            rest = match[3]
        elif pending:
            rest = line
        else:
            continue
        match = re.match(rf"\s+{HEX}\s+{HEX}(?:\s|$)", rest)
        if not match:
            continue
        addr, size = (int(x, 16) for x in match.groups())
        is_input, name = pending
        entry = {"name": name, "address": addr, "size": size}
        (inputs if is_input else output).append(entry)
        pending = None
    flash_at, flash_size = regions["FLASH"]
    ram_at, ram_size = regions["RAM"]
    flash_sections = {".isr_vector", ".ARM", ".ARM.extab", ".ARM.exidx",
                      ".preinit_array", ".init_array", ".fini_array"}
    flash = sum(x["size"] for x in output if
                x["name"].startswith((".text", ".rodata")) or x["name"] in flash_sections)
    flash += sum(x["size"] for x in output if x["name"] == ".data")
    static_ram = sum(x["size"] for x in output if x["name"] in (".data", ".bss"))
    reserved = sum(x["size"] for x in output if x["name"] == "._user_heap_stack")
    linked = [x for x in inputs if x["size"] and
              (flash_at <= x["address"] < flash_at+flash_size or
               ram_at <= x["address"] < ram_at+ram_size)]
    code = sum(x["size"] for x in linked if x["name"].startswith(".text"))
    constants = sum(x["size"] for x in linked if x["name"].startswith(".rodata"))
    flash_inputs = [x for x in linked if x["name"].startswith((".text", ".rodata", ".data"))]
    return {"flash": flash, "flash_budget": flash_size, "static_ram": static_ram,
            "reserved_ram": reserved, "ram_budget": ram_size, "code": code,
            "constants": constants, "inputs": sorted(flash_inputs, key=lambda x: x["size"], reverse=True)}


def parse_map(filename, limit=20):
    report = analyze(filename)
    print(f"Flash: {report['flash']:,} / {report['flash_budget']:,} B; free {report['flash_budget']-report['flash']:,} B")
    print(f"Static RAM: {report['static_ram']:,} B; linker stack/heap reservation: {report['reserved_ram']:,} B; budget {report['ram_budget']:,} B")
    print(f"Code inputs: {report['code']:,} B; read-only inputs: {report['constants']:,} B (included in Flash)")
    print("\nLargest linked Flash inputs (LTO includes inlined callees):")
    for entry in report["inputs"][:limit]:
        print(f"{entry['size']:6}  {entry['name']}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mapfile")
    parser.add_argument("limit", nargs="?", type=int, default=20)
    args = parser.parse_args()
    parse_map(args.mapfile, args.limit)
