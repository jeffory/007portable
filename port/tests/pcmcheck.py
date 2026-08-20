#!/usr/bin/env python3
"""Audio golden check (phase 0): spectral profile of a PORT_AUDIO_DUMP capture.

The synthesis sample stream is not yet run-to-run deterministic beyond the
first couple of seconds (chunk pacing interleaves with game-thread events),
so this checks a coarse profile instead of a hash: RMS level, the fraction
of nonzero samples, and the energy ratio in the 2756 Hz band (the historic
envelope-mixer whine — see the M4 fix log). An exact PCM hash replaces this
once the phase-2 deterministic clock lands.

Usage:
  pcmcheck.py --capture <dump.pcm> <profile.txt>   write the golden profile
  pcmcheck.py <dump.pcm> <profile.txt>             check against it

Format: 22050 Hz stereo s16le (PORT_AUDIO_DUMP). Pure python, no deps.
"""
import math
import struct
import sys

RATE = 22050
TONE_HZ = 2756.25  # 22050/8: period-8 artifacts from the Acmd mixer
MIN_SECONDS = 4
MAX_SECONDS = 8


def analyze(path):
    with open(path, "rb") as f:
        raw = f.read(RATE * 4 * MAX_SECONDS)
    n = len(raw) // 4
    if n < RATE * MIN_SECONDS:
        sys.exit(f"pcmcheck: {path}: only {n/RATE:.2f}s captured, need {MIN_SECONDS}s")

    mono = [0.0] * n
    nonzero = 0
    sq = 0.0
    for i in range(n):
        l, r = struct.unpack_from("<hh", raw, i * 4)
        if l or r:
            nonzero += 1
        s = (l + r) * 0.5
        mono[i] = s
        sq += s * s

    rms = math.sqrt(sq / n)

    # Goertzel at the whine frequency vs total energy
    w = 2.0 * math.pi * TONE_HZ / RATE
    coeff = 2.0 * math.cos(w)
    s0 = s1 = s2 = 0.0
    for x in mono:
        s0 = x + coeff * s1 - s2
        s2 = s1
        s1 = s0
    tone_power = s1 * s1 + s2 * s2 - coeff * s1 * s2
    total_power = sq * n / 2.0 if sq > 0 else 1.0  # scale-matched to Goertzel
    tone_ratio = tone_power / total_power

    return {
        "seconds": round(n / RATE, 2),
        "rms": round(rms, 1),
        "nonzero_frac": round(nonzero / n, 4),
        "tone_ratio": tone_ratio,
    }


def main():
    args = sys.argv[1:]
    capture = args and args[0] == "--capture"
    if capture:
        args = args[1:]
    if len(args) != 2:
        sys.exit(__doc__)
    pcm, profile = args

    got = analyze(pcm)
    if capture:
        with open(profile, "w") as f:
            for k, v in got.items():
                f.write(f"{k}={v}\n")
        print(f"pcmcheck: captured profile {got}")
        return

    want = {}
    with open(profile) as f:
        for line in f:
            k, v = line.strip().split("=")
            want[k] = float(v)

    fails = []
    if not want["rms"] * 0.7 <= got["rms"] <= want["rms"] * 1.3:
        fails.append(f"rms {got['rms']} outside ±30% of {want['rms']}")
    if got["nonzero_frac"] < want["nonzero_frac"] - 0.1:
        fails.append(f"nonzero_frac {got['nonzero_frac']} < {want['nonzero_frac']} - 0.1")
    tone_limit = max(want["tone_ratio"] * 3.0, 0.02)
    if got["tone_ratio"] > tone_limit:
        fails.append(f"tone_ratio {got['tone_ratio']:.5f} > {tone_limit:.5f} (whine?)")

    print(f"pcmcheck: {pcm}: {got} -> {'OK' if not fails else 'FAIL'}")
    for msg in fails:
        print(f"pcmcheck:   {msg}")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
