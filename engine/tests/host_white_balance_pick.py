#!/usr/bin/env python3
"""Real LibRaw/host picker gate using generated, uncompressed Bayer DNGs.

No private camera fixture or Python imaging dependency is needed. The neutral
patch has exactly the camera response recorded in AsShotNeutral; the picker
must recover that illuminant independently of film, grade and current WB.
"""
import argparse
from pathlib import Path
import struct
import tempfile

from host_smoke import Host


def write_dng(path, orientation=1, value_kind="neutral"):
    width, height = 96, 64
    def shorts(*values):
        return struct.pack("<" + "H" * len(values), *values)
    def longs(*values):
        return struct.pack("<" + "I" * len(values), *values)
    def rational(*values, signed=False):
        return b"".join(struct.pack("<ii" if signed else "<II", n, d) for n, d in values)
    def text(value):
        return value.encode("ascii") + b"\0"
    entries = {
        254: (4, longs(0)), 256: (4, longs(width)), 257: (4, longs(height)),
        258: (3, shorts(16)), 259: (3, shorts(1)), 262: (3, shorts(32803)),
        271: (2, text("Synthetic")), 272: (2, text("Bayer neutral test")),
        273: (4, longs(0)), 274: (3, shorts(orientation)), 277: (3, shorts(1)),
        278: (4, longs(height)), 279: (4, longs(width * height * 2)),
        284: (3, shorts(1)), 33421: (3, shorts(2, 2)), 33422: (1, bytes([0, 1, 1, 2])),
        50706: (1, bytes([1, 4, 0, 0])), 50707: (1, bytes([1, 1, 0, 0])),
        50708: (2, text("SpektraLab generated Bayer")),
        50710: (1, bytes([0, 1, 2])), 50711: (3, shorts(1)),
        50713: (3, shorts(1, 1)), 50714: (5, rational((64, 1))),
        50717: (4, longs(4095)), 50719: (4, longs(0, 0)),
        50720: (4, longs(width, height)),
        50721: (10, rational(*[(1 if r == c else 0, 1) for r in range(3) for c in range(3)], signed=True)),
        50728: (5, rational((9, 10), (1, 1), (8, 10))),
        50778: (3, shorts(21)), 50829: (4, longs(0, 0, height, width)),
    }
    sizes = {1: 1, 2: 1, 3: 2, 4: 4, 5: 8, 10: 8}
    extra_start = 8 + 2 + len(entries) * 12 + 4
    extra = bytearray()
    fields = []
    for tag, (kind, data) in sorted(entries.items()):
        if len(data) <= 4:
            field = data.ljust(4, b"\0")
        else:
            field = longs(extra_start + len(extra))
            extra.extend(data)
            if len(extra) % 2:
                extra.append(0)
        fields.append([tag, kind, len(data) // sizes[kind], field])
    offset = extra_start + len(extra)
    for field in fields:
        if field[0] == 273:
            field[3] = longs(offset)
    pixels = []
    for y in range(height):
        for x in range(width):
            channel = ((0, 1), (1, 2))[y % 2][x % 2]
            if value_kind == "black":
                value = 64
            elif value_kind == "clipped":
                value = 4095
            else:
                # Separate illuminants let orientation errors select a visibly
                # different neutral while keeping the patch uniform.
                response = (900, 1000, 800) if x < width // 2 else (1100, 1000, 1300)
                value = 64 + response[channel]
            pixels.append(value)
    header = b"II" + shorts(42) + longs(8) + shorts(len(fields))
    directory = b"".join(struct.pack("<HHI", *field[:3]) + field[3] for field in fields) + longs(0)
    path.write_bytes(header + directory + extra + shorts(*pixels))


def require(value, message):
    if not value:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--resources", required=True)
    args = parser.parse_args()
    host = Host([str(Path(args.host).resolve()), "--resources", str(Path(args.resources).resolve())])
    try:
        hello, _ = host.call("hello")
        require("white_balance_pick" in hello["result"]["methods"], "picker missing from hello")
        with tempfile.TemporaryDirectory(prefix="spk-picker-") as folder:
            for orientation, point in ((1, (0.25, 0.5)), (3, (0.75, 0.5)), (6, (0.5, 0.25)), (8, (0.5, 0.75))):
                path = Path(folder) / f"neutral-{orientation}.dng"
                write_dng(path, orientation)
                opened, _ = host.call("open", {"path": str(path), "params": {"grain_active": False, "glare_active": False}})
                require(opened["ok"], f"DNG open failed: {opened.get('error')}")
                sid = opened["result"]["session"]
                before, _ = host.call("get_params", {"session": sid})
                picked, _ = host.call("white_balance_pick", {"session": sid, "x": point[0], "y": point[1]})
                require(picked["ok"], f"picker failed: {picked.get('error')}")
                values = picked["result"]
                require("as_shot" in opened["result"]["metadata"], f"DNG has no as-shot illuminant: {opened['result']['metadata']}, pick={values}")
                shot = opened["result"]["metadata"]["as_shot"]
                require(abs(values["temperature_k"] - shot["temperature_k"]) < 10, f"wrong RAW neutral / EXIF position: {values} vs {shot}")
                require(abs(values["tint"] - shot["tint"]) < 0.5, "wrong RAW neutral tint")
                after, _ = host.call("get_params", {"session": sid})
                require(before["result"] == after["result"], "picker mutated film parameters")
                # Sampling is independent of the current WB, which is undone
                # by raw sensor sampling, not by guessing from print RGB.
                changed, _ = host.call("redecode", {"session": sid, "decode": {"white_balance": {"mode": "custom", "temperature_k": 4200, "tint": 10}}})
                require(changed["ok"], f"custom decode failed: {changed.get('error')}")
                again, _ = host.call("white_balance_pick", {"session": sid, "x": point[0], "y": point[1]})
                require(again["ok"] and again["result"] == values, "picker depends on current white balance")
                invalid, _ = host.call("white_balance_pick", {"session": sid, "x": -0.1, "y": 0.5})
                require(not invalid["ok"], "out-of-image coordinate accepted")
                host.call("close", {"session": sid})
            for kind in ("black", "clipped"):
                path = Path(folder) / f"{kind}.dng"
                write_dng(path, value_kind=kind)
                opened, _ = host.call("open", {"path": str(path)})
                require(opened["ok"], f"{kind} DNG failed to open: {opened.get('error')}")
                sid = opened["result"]["session"]
                picked, _ = host.call("white_balance_pick", {"session": sid, "x": 0.25, "y": 0.5})
                require(not picked["ok"], f"{kind} patch accepted")
                host.call("close", {"session": sid})
        print("RAW picker: real LibRaw neutral, orientation, current-WB independence and rejection gates passed")
    finally:
        host.call("shutdown")
        host.p.wait(timeout=10)


if __name__ == "__main__":
    main()
