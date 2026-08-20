#!/usr/bin/env python3
"""Fuzzy PPM comparison for golden frames (phase 0).

Usage: imgdiff.py <a.ppm> <b.ppm> <max_diff_fraction> [byte_tolerance]

Exits 0 when the fraction of RGB bytes differing by more than
byte_tolerance (default 8) is at most max_diff_fraction. Pure python, no
dependencies; binary P6 PPMs only (what gfx_dbg_frame_dump writes).
"""
import sys


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    if not data.startswith(b"P6"):
        sys.exit(f"imgdiff: {path}: not a binary PPM")
    # header: P6 <w> <h> <maxval> then a single whitespace byte
    fields = []
    i = 2
    while len(fields) < 3:
        while i < len(data) and data[i] in b" \t\r\n":
            i += 1
        if data[i : i + 1] == b"#":  # comment line
            while data[i] not in b"\r\n":
                i += 1
            continue
        j = i
        while data[j] not in b" \t\r\n":
            j += 1
        fields.append(int(data[i:j]))
        i = j
    i += 1  # the single whitespace after maxval
    w, h, _ = fields
    return w, h, data[i : i + w * h * 3]


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    aw, ah, a = read_ppm(sys.argv[1])
    bw, bh, b = read_ppm(sys.argv[2])
    max_frac = float(sys.argv[3])
    tol = int(sys.argv[4]) if len(sys.argv) > 4 else 8

    if (aw, ah) != (bw, bh):
        print(f"imgdiff: size mismatch {aw}x{ah} vs {bw}x{bh}")
        sys.exit(1)

    diff = sum(1 for x, y in zip(a, b) if abs(x - y) > tol)
    frac = diff / max(len(a), 1)
    ok = frac <= max_frac
    print(f"imgdiff: {sys.argv[1]} vs {sys.argv[2]}: "
          f"{diff}/{len(a)} bytes differ (>{tol}) = {frac:.4%} "
          f"(limit {max_frac:.4%}) -> {'OK' if ok else 'FAIL'}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
