#!/usr/bin/env python3
"""Turn the init code of a MIPI DSI panel vendor into the init-sequence of a panel-dsi node.

    init2dts.py INIT.txt            print the lines to paste into init-sequence = < ... >;

Input, one command per line, all numbers in hex:

    TYPE DELAY COUNT BYTE...        e.g.  39 00 04 df 91 68 f9
                                          05 78 01 11

  TYPE   the MIPI DSI data type: 05 / 15 / 39 are DCS writes, 03 / 13 / 23 / 29 are generic writes
  DELAY  milliseconds to wait after the command
  COUNT  number of bytes that follow (the DCS command byte counts)

Lines that are empty or start with // are skipped, and so is a // comment at the end of a line.
"""
import re
import sys

DCS = {0x05, 0x15, 0x39}
GENERIC = {0x03, 0x13, 0x23, 0x29}


def convert(text):
    out = []
    for no, line in enumerate(text.splitlines(), 1):
        line = line.split("//", 1)[0].strip()
        if not line:
            continue
        try:
            v = [int(t, 16) for t in line.split()]
        except ValueError:
            sys.exit(f"line {no}: not hex numbers: {line}")
        if len(v) < 4:
            sys.exit(f"line {no}: a command needs a type, a delay, a count and at least one byte")
        kind, delay, count, data = v[0], v[1], v[2], v[3:]
        if count != len(data):
            sys.exit(f"line {no}: the count says {count} bytes, the line has {len(data)}")
        if kind in DCS:
            macro = "SUNXI_CMD_DCS"
        elif kind in GENERIC:
            macro = "SUNXI_CMD_GENERIC"
        else:
            sys.exit(f"line {no}: data type {kind:02x} is not a DSI write")
        out.append(f"{macro}({delay}, {count}, " + " ".join(f"0x{b:02x}" for b in data) + ")")
    return out


if __name__ == "__main__":
    cmds = convert(open(sys.argv[1], encoding="utf-8").read())
    print("init-sequence = <" + "\n\t\t ".join(cmds) + ">;")
