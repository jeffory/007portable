#!/usr/bin/env python3
"""Generate port/src/rom_symbols.ld for the PC port.

Every asset the game loads at runtime is referenced through a linker symbol
whose value, on N64, is the file's ROM offset (assigned by ge007.ld). The
retail US ROM is byte-matched, so those offsets are stable constants
recorded in scripts/filelist.u.csv (offset,size,path,...) — no IDO build
needed. This script joins:

  - assets/obseg/ob_seg.s      (the obseg symbol list: bg_file_seg /
                                 obseg_file_rz / obseg_file_Z macro calls)
  - scripts/filelist.u.csv     (ROM offset + size per extracted file)
  - a static map for the named segments (fonts, music, animation tables...)

and emits PROVIDE(symbol = absolute) lines consumed via -Wl,-T at link.

Run from the repo root:  python3 port/tools/gen_rom_symbols.py
"""
import csv
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CSV_PATH = os.path.join(ROOT, "scripts", "filelist.u.csv")
IMAGELIST_PATH = os.path.join(ROOT, "imagelist.u.csv")
OBSEG_S = os.path.join(ROOT, "assets", "obseg", "ob_seg.s")
OUT_PATH = os.path.join(ROOT, "port", "src", "rom_symbols.ld")


def load_filelist():
    """basename (no extension) -> list of (offset, size, path)"""
    table = {}
    with open(CSV_PATH, newline="") as f:
        for row in csv.reader(f):
            if len(row) < 3:
                continue
            offset, size, path = int(row[0]), int(row[1]), row[2]
            base = os.path.basename(path)
            if base.endswith(".bin"):
                base = base[:-4]
            table.setdefault(base, []).append((offset, size, path))
    return table


def parse_obseg_symbols():
    """[(symbol, csv_basename)] from ob_seg.s macro invocations."""
    syms = []
    pat = re.compile(r"^\s*(bg_file_seg|obseg_file_rz|obseg_file_Z)\s+"
                     r"([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)")
    with open(OBSEG_S) as f:
        for line in f:
            m = pat.match(line)
            if not m:
                continue
            macro, a, b = m.groups()
            if macro == "bg_file_seg":
                syms.append((a, b))       # symbol, file basename
            else:
                syms.append((b, b))       # dir, name -> symbol == basename
    return syms


# Named segments: symbol -> (csv basename, which end).
# RomStart = file offset, RomEnd = offset + size.
NAMED = {
    "_fontdlSegmentRomStart":            ("ge007.u.117880.jfont_dl", "start"),
    "_fontdlSegmentRomEnd":              ("ge007.u.117880.jfont_dl", "end"),
    "_jfontchardataSegmentRomStart":     ("ge007.u.117940.jfont_chardata", "start"),
    "_jfontchardataSegmentRomEnd":       ("ge007.u.117940.jfont_chardata", "end"),
    "_efontchardataSegmentRomStart":     ("ge007.u.123040.efont_chardata", "start"),
    "_efontchardataSegmentRomEnd":       ("ge007.u.123040.efont_chardata", "end"),
    "_animation_entriesSegmentRomStart": ("animationtable_entries", "start"),
    "_animation_entriesSegmentRomEnd":   ("animationtable_entries", "end"),
    "_animation_dataSegmentRomStart":    ("animationtable_data", "start"),
    "_animation_dataSegmentRomEnd":      ("animationtable_data", "end"),
    "_GlobalimagetableSegmentRomStart":  ("ge007.u.29D160.Globalimagetable", "start"),
    "_GlobalimagetableSegmentRomEnd":    ("ge007.u.29D160.Globalimagetable", "end"),
    "_rarewarelogoSegmentRomStart":      ("rarewarelogo", "start"),
    "_rarewarelogoSegmentRomEnd":        ("rarewarelogo", "end"),
    "_sfxctlSegmentRomStart":            ("sfx.ctl", "start"),
    "_sfxctlSegmentRomEnd":              ("sfx.ctl", "end"),
    "_sfxtblSegmentRomStart":            ("sfx.tbl", "start"),
    "_sfxtblSegmentRomEnd":              ("sfx.tbl", "end"),
    "_instrumentsctlSegmentRomStart":    ("instruments.ctl", "start"),
    "_instrumentsctlSegmentRomEnd":      ("instruments.ctl", "end"),
    "_instrumentstblSegmentRomStart":    ("instruments.tbl", "start"),
    "_instrumentstblSegmentRomEnd":      ("instruments.tbl", "end"),
    # .music/.musiccompressed sections of music.o start at the sbk blob
    "_musicsampletblSegmentRomStart":    ("music.sbk", "start"),
    # romfiles2.s blob, romCopy'd by title.c
    "unknown2":                          ("ge007.u.2A4D50.usedby7F008DE4", "start"),
    "unknown2_end":                      ("ge007.u.2A4D50.usedby7F008DE4", "end"),
    # fonts (segment = single object; kerning table comes first)
    "_fontbankgothicSegmentRomStart":    ("fontBankGothic_kerning", "start"),
    "_fontzurichboldSegmentRomStart":    ("fontZurichBold_kerning", "start"),
}

# RAM-side segment symbols the game references (sizes/segment addresses).
# Values reproduce ge007.ld's rampos assignments.
RAM_SEGMENTS = [
    # (StartSym, EndSym, csv basenames summed for size, base vaddr)
    ("_GlobalimagetableSegmentStart", "_GlobalimagetableSegmentEnd",
     ["ge007.u.29D160.Globalimagetable"], 0x02000000),
    ("_rarewarelogoSegmentStart", "_rarewarelogoSegmentEnd",
     ["rarewarelogo"], 0x02000000),
    ("_animation_dataSegmentStart", "_animation_dataSegmentEnd",
     ["animationtable_data"], 0x00000000),
    ("_fontbankgothicSegmentStart", "_fontbankgothicSegmentEnd",
     ["fontBankGothic_kerning", "fontBankGothic_fontchartable"], 0x00000000),
    ("_fontzurichboldSegmentStart", "_fontzurichboldSegmentEnd",
     ["fontZurichBold_kerning", "fontZurichBold_fontchartable"], 0x00000000),
]


def main():
    files = load_filelist()
    out = []
    missing = []

    # The CSV records only part of some segments. Globalimagetable's entry
    # covers just the leading Gfx block (0xAC8 bytes); the sImageTableEntry
    # arrays (s_genericimage .. s_mpstageselimages) continue to the next ROM
    # file (rarewarelogo @ 0x29E560). texReset copies Start..End, so a short
    # end leaves every image table zeroed → black sky/water and wild
    # texSelect derefs in-mission.
    SIZE_OVERRIDES = {"ge007.u.29D160.Globalimagetable": 0x1400}

    def lookup(base):
        if base not in files:
            return None
        entries = files[base]
        e = entries[0]
        if len(entries) > 1:
            # version-specific duplicates (setup/u/ ...): prefer the /u/ path,
            # else the first occurrence
            for cand in entries:
                if "/u/" in cand[2]:
                    e = cand
                    break
        if base in SIZE_OVERRIDES:
            e = (e[0], SIZE_OVERRIDES[base], e[2])
        return e

    out.append("/* Generated by port/tools/gen_rom_symbols.py — do not edit.")
    out.append(" * ROM offsets of every runtime-loaded asset in the byte-matched")
    out.append(" * US ROM, from scripts/filelist.u.csv. */")
    out.append("")

    count = 0
    for sym, base in parse_obseg_symbols():
        e = lookup(base)
        if e is None:
            # EU/JP-only file absent from the US ROM; the symbol may still be
            # referenced by the resource table, so define it as 0 (never
            # loaded by the US game).
            out.append("PROVIDE(%s = 0); /* not in US ROM */" % sym)
            missing.append((sym, base))
            continue
        out.append("PROVIDE(%s = 0x%X);" % (sym, e[0]))
        count += 1

    out.append("")
    for sym, (base, which) in sorted(NAMED.items()):
        e = lookup(base)
        if e is None:
            missing.append((sym, base))
            continue
        val = e[0] if which == "start" else e[0] + e[1]
        out.append("PROVIDE(%s = 0x%X);" % (sym, val))
        count += 1

    out.append("")
    for start_sym, end_sym, bases, vaddr in RAM_SEGMENTS:
        size = 0
        ok = True
        for b in bases:
            e = lookup(b)
            if e is None:
                missing.append((start_sym, b))
                ok = False
                break
            size += e[1]
        if not ok:
            continue
        out.append("PROVIDE(%s = 0x%X);" % (start_sym, vaddr))
        out.append("PROVIDE(%s = 0x%X);" % (end_sym, vaddr + size))
        count += 2

    # ramrom demo files: symbol name == basename (title.c romCopy's them)
    out.append("")
    with open(CSV_PATH, newline="") as f:
        for row in csv.reader(f):
            if len(row) >= 3 and row[2].startswith("assets/ramrom/"):
                base = os.path.basename(row[2])[:-4]
                out.append("PROVIDE(%s = 0x%X);" % (base, int(row[0])))
                count += 1

    # obseg end marker (== start of the images segment region)
    obseg_end = None
    with open(CSV_PATH, newline="") as f:
        rows = [r for r in csv.reader(f) if len(r) >= 3 and "/obseg/" in r[2]]
        if rows:
            last = max(rows, key=lambda r: int(r[0]))
            obseg_end = int(last[0]) + int(last[1])
    if obseg_end is not None:
        out.append("PROVIDE(ob__ob_end_seg = 0x%X);" % obseg_end)
        count += 1

    # Segment-offset symbols defined in tracked asset .c files. On N64 these
    # objects are linked at fixed segment bases and game code treats the
    # symbol addresses as OFFSETS (added to a runtime rdram base or used as
    # blob indices), so we compile each file natively (data layout only:
    # -fno-toplevel-reorder -malign-data=abi keeps GCC's .data layout in
    # declaration order at ABI alignment like IDO's) and emit base+offset.
    out.append("")
    for relpath, base in [
        ("assets/animationtable_data.c", 0x00000000),  # animation_data seg
        ("assets/oddtextures.c",        0x02000000),   # Globalimagetable seg
        ("assets/font_dl.c",            0x01000000),   # fontdl seg
        ("assets/rarewarelogo.c",       0x02000000),   # rarewarelogo seg
    ]:
        src = os.path.join(ROOT, relpath)
        if not os.path.exists(src):
            missing.append(("<object>", relpath))
            continue
        with tempfile.NamedTemporaryFile(suffix=".o", delete=False) as tf:
            objpath = tf.name
        try:
            cmd = ["gcc", "-m32", "-c", "-fno-toplevel-reorder",
                   "-malign-data=abi", "-std=gnu89", "-fms-extensions",
                   "-fno-strict-aliasing",
                   "-Wno-implicit-function-declaration", "-Wno-int-conversion",
                   "-Wno-incompatible-pointer-types",
                   "-DTARGET_PC", "-D_LANGUAGE_C", "-DVERSION_US", "-DLANG_US",
                   "-DREFRESH_NTSC", "-DLEFTOVERDEBUG", "-DLEFTOVERSPECTRUM",
                   "-DBUGFIX_R0",
                   "-I" + os.path.join(ROOT, "port", "include"),
                   "-I" + os.path.join(ROOT, "include"),
                   "-I" + os.path.join(ROOT, "src"),
                   "-I" + os.path.join(ROOT, "src", "game"),
                   "-I" + ROOT,
                   "-I" + os.path.join(ROOT, "include", "PR"),
                   "-I" + os.path.join(ROOT, "port", "compat32"),
                   "-o", objpath, src]
            subprocess.run(cmd, check=True, capture_output=True)
            nm = subprocess.run(["nm", objpath], check=True,
                                capture_output=True, text=True).stdout
            out.append("/* %s @ 0x%X */" % (relpath, base))
            raw4 = []
            for line in nm.splitlines():
                parts = line.split()
                if len(parts) == 3 and parts[1] in ("D", "d", "R", "B"):
                    if parts[1] == "d":
                        continue  # local
                    raw4.append((int(parts[0], 16), parts[2]))
            raw4.sort()
            # Ground truth overrides: assets/animationtable_data.h #defines
            # PTR_ANIM_<name> with each ANIM_DATA_<name>'s true table offset
            # (validated: PTR_ANIM_idle 0x1C). The nm+drift heuristic
            # accumulates GCC-vs-IDO layout error across hundreds of arrays,
            # so prefer the macros wherever they exist.
            truth4 = {}
            if relpath == "assets/animationtable_data.c":
                hdr_path = os.path.join(ROOT, "assets", "animationtable_data.h")
                if os.path.exists(hdr_path):
                    for mline in open(hdr_path):
                        mm = re.match(r"\s*#define\s+PTR_ANIM_(\w+)\s+(0[xX][0-9A-Fa-f]+|\d+)", mline)
                        if mm:
                            truth4["ANIM_DATA_" + mm.group(1)] = int(mm.group(2), 0)
            # GCC-vs-IDO drift correction as in the spans pass: named
            # symbols carry true offsets, unnamed inherit the last delta
            delta4 = 0
            nsyms = 0
            for nm_off4, sym4 in raw4:
                off4 = nm_off4 + delta4
                if sym4 in truth4:
                    off4 = truth4[sym4]
                    delta4 = off4 - nm_off4
                else:
                    m4 = re.search(r"(?:0x|_|[a-z])([0-9A-Fa-f]{3,8})$", sym4)
                    if m4:
                        v = int(m4.group(1), 16)
                        if v >= 0x02000000:
                            v -= 0x02000000
                        if v < 0x1000000:
                            off4 = v
                            delta4 = v - nm_off4
                out.append("PROVIDE(%s = 0x%X);" % (sym4, base + off4))
                nsyms += 1
            count += nsyms
        except subprocess.CalledProcessError as e:
            print("WARN: could not compile %s: %s" % (relpath,
                  e.stderr.decode()[:400] if e.stderr else e))
            missing.append(("<object>", relpath))
        finally:
            if os.path.exists(objpath):
                os.unlink(objpath)

    # Blob swap spans: several segments are romCopy'd big-endian at runtime
    # but exist as tracked C files, so symbol-typed swap tables can be
    # generated (kind 0 = Gfx u32 words, 1 = sImageTableEntry stride 12,
    # 2 = Vtx 16-byte halfword pattern; u8/u32 texture data keeps N64 byte
    # order for the renderer's importers).
    KINDMAP = {"Gfx": 0, "sImageTableEntry": 1, "Vtx": 2, "Vertex": 2}
    for src_rel, inc_name, var_name in [
        ("assets/oddtextures.c", "globalimagetable_spans.inc", "sGlobalImagetableSpans"),
        ("assets/rarewarelogo.c", "rarewarelogo_spans.inc", "sRarewareLogoSpans"),
    ]:
      try:
        src_txt = open(os.path.join(ROOT, src_rel)).read()
        kinds = {}
        for m2 in re.finditer(r"^(?:static\s+)?([A-Za-z_][A-Za-z0-9_]*)\s+([A-Za-z0-9_]+)\s*\[",
                              src_txt, re.M):
            if m2.group(1) in KINDMAP:
                kinds[m2.group(2)] = KINDMAP[m2.group(1)]
        with tempfile.NamedTemporaryFile(suffix=".o", delete=False) as tf:
            objpath = tf.name
        cmd = ["gcc", "-m32", "-c", "-fno-toplevel-reorder", "-malign-data=abi",
               "-std=gnu89", "-fms-extensions", "-fno-strict-aliasing",
               "-Wno-implicit-function-declaration", "-Wno-int-conversion",
               "-Wno-incompatible-pointer-types",
               "-DTARGET_PC", "-D_LANGUAGE_C", "-DVERSION_US", "-DLANG_US",
               "-DREFRESH_NTSC", "-DLEFTOVERDEBUG", "-DLEFTOVERSPECTRUM",
               "-DBUGFIX_R0",
               "-I" + os.path.join(ROOT, "port", "include"),
               "-I" + os.path.join(ROOT, "include"),
               "-I" + os.path.join(ROOT, "src"),
               "-I" + os.path.join(ROOT, "src", "game"),
               "-I" + ROOT,
               "-I" + os.path.join(ROOT, "include", "PR"),
               "-I" + os.path.join(ROOT, "port", "compat32"),
               "-o", objpath, os.path.join(ROOT, src_rel)]
        subprocess.run(cmd, check=True, capture_output=True)
        nm = subprocess.run(["nm", "-S", objpath], check=True,
                            capture_output=True, text=True).stdout
        def name_offset(sym):
            # GCC's data layout can drift a few bytes from IDO's, but these
            # generated asset files encode each symbol's true blob offset in
            # its name (DL_0x0018, verts4358, rgba0014, D_020043E8, ...).
            m3 = re.search(r"(?:0x|_|[a-z])([0-9A-Fa-f]{3,8})$", sym)
            if m3:
                val = int(m3.group(1), 16)
                if val >= 0x02000000:
                    val -= 0x02000000
                return val
            return None

        raw = []
        for line in nm.splitlines():
            parts = line.split()
            if len(parts) == 4 and parts[2] in ("D", "d") and parts[3] in kinds:
                raw.append((int(parts[0], 16), int(parts[1], 16),
                            kinds[parts[3]], parts[3]))
        os.unlink(objpath)
        raw.sort()
        # correct GCC-vs-IDO layout drift: named symbols carry their true
        # offsets; unnamed ones inherit the delta of the nearest preceding
        # named neighbor
        spans = []
        delta = 0
        for nm_off, sz, kind, name in raw:
            true = name_offset(name)
            if true is not None:
                delta = true - nm_off
            spans.append((nm_off + delta, sz, kind, name))
        spans.sort()
        with open(os.path.join(ROOT, "port", "src", inc_name), "w") as f:
            f.write("/* Generated by gen_rom_symbols.py from %s\n"
                    " * kind 0 = Gfx (swap u32 words), 1 = sImageTableEntry[]\n"
                    " * (stride 12, swap u32 index), 2 = Vtx[] (halfwords) */\n"
                    % src_rel)
            f.write("static const struct portgitspan %s[] = {\n" % var_name)
            for off, size, kind, name in spans:
                f.write("    { 0x%X, 0x%X, %d }, /* %s */\n" % (off, size, kind, name))
            f.write("};\n")
        print("wrote port/src/%s: %d spans" % (inc_name, len(spans)))
      except (OSError, subprocess.CalledProcessError) as e:
        print("WARN: could not generate %s: %s" % (inc_name, e))

    # address-named data symbols inside the Globalimagetable segment
    out.append("")
    for sym in ["D_020043E8", "D_02004758", "D_02004FE8", "D_02005FF0"]:
        out.append("PROVIDE(%s = 0x%s);" % (sym, sym[2:]))
        count += 1

    # images segment: first image offset from imagelist.u.csv
    try:
        with open(IMAGELIST_PATH, newline="") as f:
            first = None
            for row in csv.reader(f):
                try:
                    off = int(row[0])
                except (ValueError, IndexError):
                    continue
                first = off if first is None else min(first, off)
            if first is not None:
                out.append("")
                out.append("PROVIDE(_imagesSegmentRomStart = 0x%X);" % first)
                count += 1
    except OSError:
        missing.append(("_imagesSegmentRomStart", IMAGELIST_PATH))

    with open(OUT_PATH, "w") as f:
        f.write("\n".join(out) + "\n")

    print("wrote %s: %d symbols" % (os.path.relpath(OUT_PATH, ROOT), count))
    if missing:
        print("%d symbols had no CSV entry (EU/JP-only files are expected):"
              % len(missing))
        for sym, base in missing[:15]:
            print("  %-40s (%s)" % (sym, base))
        if len(missing) > 15:
            print("  ... and %d more" % (len(missing) - 15))
    return 0


if __name__ == "__main__":
    sys.exit(main())
