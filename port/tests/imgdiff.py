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


def hist(buf, ch):
    h = [0] * 32
    for i in range(ch, len(buf), 3):
        h[buf[i] >> 3] += 1
    n = len(buf) // 3
    return [v / n for v in h]


def main():
    args = [a for a in sys.argv[1:] if a != "--hist"]
    use_hist = "--hist" in sys.argv
    if len(args) < 3:
        sys.exit(__doc__)
    aw, ah, a = read_ppm(args[0])
    bw, bh, b = read_ppm(args[1])
    max_val = float(args[2])
    tol = int(args[3]) if len(args) > 3 else 8

    if (aw, ah) != (bw, bh):
        print(f"imgdiff: size mismatch {aw}x{ah} vs {bw}x{bh}")
        sys.exit(1)

    if use_hist:
        # Per-channel 32-bin histogram L1 distance (0..2): tolerant to
        # small view/animation-phase shifts (the pre-phase-2 world has no
        # deterministic clock) while catching palette/black-screen/geometry
        # regressions.
        dist = max(
            sum(abs(x - y) for x, y in zip(hist(a, c), hist(b, c)))
            for c in range(3)
        )
        ok = dist <= max_val
        print(f"imgdiff(hist): {args[0]} vs {args[1]}: "
              f"L1 {dist:.4f} (limit {max_val}) -> {'OK' if ok else 'FAIL'}")
        sys.exit(0 if ok else 1)

    diff = sum(1 for x, y in zip(a, b) if abs(x - y) > tol)
    frac = diff / max(len(a), 1)
    ok = frac <= max_val
    print(f"imgdiff: {args[0]} vs {args[1]}: "
          f"{diff}/{len(a)} bytes differ (>{tol}) = {frac:.4%} "
          f"(limit {max_val:.4%}) -> {'OK' if ok else 'FAIL'}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
