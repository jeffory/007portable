#!/usr/bin/env python3
"""Generate port/src/propdef_layout.inc — the per-type propdef layout table.

The setup blob stores prop definitions in 32-bit layout, walked with the
strides 32-bit sizepropdef() returns (a mix of sizeof()s and Rare's
hardcoded word counts). On 64-bit hosts the game ACCESSES records through
native structs, so pointer-bearing types must be transcribed to native
layout at load, and sizepropdef() must return the native stride for them
(portPropdefWords64, emitted here) or the game's walk desyncs from the
emission.

For every propdef type this emits {blobBytes, nativeBytes, pointer-slot
offset pairs}: blob strides are the frozen 32-bit walk values, native
layout comes from compiling a -m32 and a native probe and reading both
with `gdb ptype /o`. A generic expander copies non-pointer gaps verbatim
(asserted equal-length here, modulo pre-pointer ABI padding) and
zero-extends pointer slots.

Needs an x86 host with gcc -m32 and gdb; the committed .inc is static
data (the blob format is frozen), so other hosts just build with it.
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "port", "src", "propdef_layout.inc")

# (PROPDEF enum name, blob words or None = 32-bit sizeof/4, record struct or
#  None = no native struct / layout identical -> copied verbatim).
# Blob word counts mirror 32-bit sizepropdef() (loadobjectmodel.c) exactly.
TYPES = [
    ("PROPDEF_DOOR",          None, "DoorRecord"),
    ("PROPDEF_DOOR_SCALE",    None, "GlobalDoorScaleRecord"),
    ("PROPDEF_PROP",          None, "ObjectRecord"),
    ("PROPDEF_KEY",           None, "KeyRecord"),
    ("PROPDEF_ALARM",         None, "ObjectRecord"),
    ("PROPDEF_CCTV",          0x3B, "CCTVRecord"),
    ("PROPDEF_MAGAZINE",      0x21, "WeaponObjRecord"),
    ("PROPDEF_COLLECTABLE",   0x22, "WeaponObjRecord"),
    ("PROPDEF_GUARD",         None, "GuardRecord"),
    ("PROPDEF_MONITOR",       0x40, "MonitorObjRecord"),
    ("PROPDEF_MULTI_MONITOR", 0x95, "MultiMonitorObjRecord"),
    ("PROPDEF_RACK",          None, "ObjectRecord"),
    ("PROPDEF_AUTOGUN",       0x36, "AutogunRecord"),
    ("PROPDEF_LINK",          3,    "LinkRecord"),
    ("PROPDEF_HAT",           None, "ObjectRecord"),
    ("PROPDEF_GUARD_ATTRIBUTE", 3,  "GuardAttributeRecord"),
    ("PROPDEF_SWITCH",        4,    None),
    ("PROPDEF_SAFE",          None, "ObjectRecord"),
    ("PROPDEF_SAFE_ITEM",     5,    "SafeObjectRecord"),
    ("PROPDEF_AMMO",          0x2D, "MultiAmmoCrateRecord"),
    ("PROPDEF_ARMOUR",        0x22, "BodyArmourRecord"),
    ("PROPDEF_TAG",           4,    "TagObjectRecord"),
    ("PROPDEF_RENAME",        10,   "RenameObjectRecord"),
    ("PROPDEF_OBJECTIVE_START", 4,  "struct objective_entry"),
    ("PROPDEF_OBJECTIVE_END",   1,  None),
    ("PROPDEF_OBJECTIVE_DESTROY_OBJECT",     2, "MissionObjectiveRecord"),
    ("PROPDEF_OBJECTIVE_COMPLETE_CONDITION", 2, "MissionObjectiveRecord"),
    ("PROPDEF_OBJECTIVE_FAIL_CONDITION",     2, "MissionObjectiveRecord"),
    ("PROPDEF_OBJECTIVE_COLLECT_OBJECT",     2, "MissionObjectiveRecord"),
    ("PROPDEF_OBJECTIVE_DEPOSIT_OBJECT",     2, "MissionObjectiveRecord"),
    ("PROPDEF_OBJECTIVE_PHOTOGRAPH", 4, "struct criteria_picture"),
    ("PROPDEF_OBJECTIVE_NULL",  1,  None),
    ("PROPDEF_OBJECTIVE_ENTER_ROOM", 4, "struct criteria_roomentered"),
    ("PROPDEF_OBJECTIVE_DEPOSIT_OBJECT_IN_ROOM", 5, "struct criteria_deposit"),
    ("PROPDEF_OBJECTIVE_COPY_ITEM", 1, None),
    ("PROPDEF_WATCH_MENU_OBJECTIVE_TEXT", 4, "struct watchMenuObjectiveText"),
    ("PROPDEF_LOCK_DOOR",     4,    "LockDoorRecord"),
    ("PROPDEF_VEHICHLE",      0x2C, "VehichleRecord"),
    ("PROPDEF_AIRCRAFT",      0x2D, "AircraftRecord"),
    ("PROPDEF_UNK41",         1,    None),
    ("PROPDEF_TANK",          0x38, "TankRecord"),
    ("PROPDEF_CAMERAPOS",     7,    None),
    ("PROPDEF_TINTED_GLASS",  None, "TintedGlassRecord"),
    ("PROPDEF_GLASS",         None, "ObjectRecord"),
    ("PROPDEF_GAS_RELEASING", None, "ObjectRecord"),
]

CFLAGS = ["-g", "-c", "-std=gnu89", "-fms-extensions", "-fsigned-char",
          "-DVERSION_US", "-DLANG_US", "-DREFRESH_NTSC", "-DLEFTOVERDEBUG",
          "-DLEFTOVERSPECTRUM", "-DBUGFIX_R0", "-D_LANGUAGE_C"]
INCS = [os.path.join(ROOT, d) for d in
        ["port/include", "include", "src", "src/game", ".", "include/PR"]]


def build_probe(m32):
    structs = sorted({st for _, _, st in TYPES if st})
    probe = ("#include <ultra64.h>\n#include <bondgame.h>\n"
             "PROPDEF_TYPE probe_enum_anchor;\n" + "".join(
        f"{st} probe_{i};\n" for i, st in enumerate(structs)))
    src = tempfile.NamedTemporaryFile(suffix=".c", delete=False, mode="w")
    src.write(probe)
    src.close()
    obj = src.name[:-2] + ".o"
    cmd = ["gcc"] + (["-m32"] if m32 else []) + CFLAGS + \
          [f"-I{d}" for d in INCS] + ["-o", obj, src.name]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"probe compile failed:\n{r.stderr[:2000]}")
    return obj


MEMBER = re.compile(r"/\*\s+(\d+)(:\s*\d+)?\s+\|\s+(\d+)\s+\*/\s+(.*)")
UNION_ALT = re.compile(r"/\*\s+(\d+)\s+\*/\s+(.*)")
PTRDECL = re.compile(r"\*\s*\w+\s*(\[\w*\])?\s*;")
EMBED = re.compile(r"^(?:struct |union )?([A-Za-z_]\w*)\s+\w+\s*(\[(\d+)\])?;$")


def parse_one(obj, struct):
    """One ptype pass: (size, [(off, decl)] pointers, [(off, typename, count, elemsize)] embedded)."""
    txt = subprocess.run(["gdb", "-batch", "-ex", f"ptype /o {struct}", obj],
                         check=True, capture_output=True, text=True).stdout
    size = None
    ptrs = []
    embeds = []
    union_off = None
    depth_off = []

    for line in txt.splitlines():
        m = MEMBER.match(line)
        if m and m.group(2):
            sys.exit(f"{struct}: bitfield member not supported: {line.strip()}")
        decl = None
        off = None
        msize = None
        if m:
            off = int(m.group(1))
            msize = int(m.group(3))
            decl = m.group(4)
        else:
            a = UNION_ALT.match(line)
            if a:
                decl = a.group(2)
                off = union_off
        if decl is not None and "{" in decl:
            depth_off.append(off if "union" in decl else None)
            if "union" in decl and off is not None:
                union_off = off
            continue
        if re.match(r"\s*}", line) and depth_off:
            depth_off.pop()
            union_off = next((o for o in reversed(depth_off) if o is not None), None)
        if decl is not None and off is not None:
            if PTRDECL.search(decl):
                if not ptrs or ptrs[-1][0] != off:
                    ptrs.append((off, decl.strip()))
            elif msize is not None and msize >= 8:
                e = EMBED.match(decl.strip())
                if e and not e.group(1).startswith(("u8", "u16", "u32", "u64",
                                                    "s8", "s16", "s32", "s64",
                                                    "f32", "f64", "char", "int",
                                                    "short", "long", "float",
                                                    "double", "coord3d", "Mtxf",
                                                    "rgba_u8")):
                    count = int(e.group(3)) if e.group(3) else 1
                    embeds.append((off, e.group(1), count, msize // count))
        t = re.search(r"/\* total size \(bytes\):\s+(\d+)\s+\*/", line)
        if t:
            size = int(t.group(1))
    return size, ptrs, embeds


def parse(obj, struct, _depth=0):
    """Recursive: absolute pointer offsets including embedded struct members."""
    if _depth > 6:
        sys.exit(f"{struct}: embed recursion too deep")
    size, ptrs, embeds = parse_one(obj, struct)
    for off, tname, count, elemsize in embeds:
        try:
            esize, eptrs = parse(obj, tname, _depth + 1)
        except SystemExit:
            raise
        except Exception:
            continue  # not a struct gdb knows; scalar-array etc.
        if esize is None or not eptrs:
            continue
        for i in range(count):
            base = off + i * elemsize
            for eo, ed in eptrs:
                ptrs.append((base + eo, ed))
    ptrs.sort(key=lambda t: t[0])
    return size, ptrs


def enum_value(obj, name):
    txt = subprocess.run(["gdb", "-batch", "-ex", f"print (int){name}", obj],
                         check=True, capture_output=True, text=True).stdout
    m = re.search(r"= (\d+)", txt)
    if not m:
        sys.exit(f"cannot resolve enum {name}")
    return int(m.group(1))


def main():
    o32 = build_probe(True)
    o64 = build_probe(False)

    layouts = {}   # struct -> (s32z, s64z, p32, p64)
    for st in sorted({st for _, _, st in TYPES if st}):
        s32z, p32 = parse(o32, st)
        s64z, p64 = parse(o64, st)
        if len(p32) != len(p64):
            sys.exit(f"{st}: pointer count differs")
        prev32, prev64 = 0, 0
        for (a, da), (b, _) in zip(p32, p64):
            pad = (b - prev64) - (a - prev32)
            if pad < 0 or pad > 4:
                sys.exit(f"{st}: gap before {da!r} differs beyond pointer "
                         f"padding: 32[{prev32}..{a}) vs 64[{prev64}..{b})")
            prev32, prev64 = a + 4, b + 8
        if not 0 <= (s64z - prev64) - (s32z - prev32) <= 7:
            sys.exit(f"{st}: tail gap differs")
        layouts[st] = (s32z, s64z, [o for o, _ in p32], [o for o, _ in p64])

    rows = []      # (typeval, typename, blobBytes, nativeBytes, struct)
    for tname, words, st in TYPES:
        tv = enum_value(o32, tname)
        if st:
            s32z, s64z, _, _ = layouts[st]
            blob = (words * 4) if words is not None else s32z
            native = s64z
            if blob > s32z:
                print(f"note: {tname}: blob stride {blob} > struct32 {s32z} "
                      f"(tail copied verbatim past the struct)")
        else:
            blob = native = (words or 1) * 4
        rows.append((tv, tname, blob, native, st))
    rows.sort()
    maxtv = rows[-1][0]

    out = [
        "/* Generated by port/tools/gen_propdef_layout.py — do not edit.",
        " * Per-propdef-type layout: blob (32-bit) stride, native stride and",
        " * pointer-slot offsets. Consumed by expandPropdefRecord() /",
        " * propdefLayout() (preprocess_setup.c) and portPropdefWords64()",
        " * (64-bit sizepropdef override). Regenerate on an x86 host after",
        " * editing the record structs in bondtypes.h. */",
        "",
    ]
    for st in sorted(layouts):
        _, _, p32, p64 = layouts[st]
        cname = st.replace("struct ", "")
        l32 = ", ".join(map(str, p32)) or "0 /* none */"
        l64 = ", ".join(map(str, p64)) or "0 /* none */"
        out.append(f"static const u16 kPtr32_{cname}[] = {{{l32}}};")
        out.append(f"static const u16 kPtr64_{cname}[] = {{{l64}}};")
    out.append("")
    out.append("struct PortPropdefLayout {")
    out.append("    u16 blobBytes;   /* 32-bit walk stride (0 = type unknown) */")
    out.append("    u16 nativeBytes; /* native walk stride */")
    out.append("    u16 structBytes32; /* 32-bit sizeof of the access struct */")
    out.append("    u8  nptr;")
    out.append("    const u16 *p32;")
    out.append("    const u16 *p64;")
    out.append("};")
    out.append("")
    out.append(f"#define PORT_PROPDEF_LAYOUT_COUNT {maxtv + 1}")
    out.append("static const struct PortPropdefLayout kPropdefLayouts[PORT_PROPDEF_LAYOUT_COUNT] = {")
    for tv, tname, blob, native, st in rows:
        if st:
            cname = st.replace("struct ", "")
            s32z, _, p32, _ = layouts[st]
            out.append(f"    [{tv}] = {{{blob}, {native}, {s32z}, {len(p32)}, "
                       f"kPtr32_{cname}, kPtr64_{cname}}}, /* {tname}: {st} */")
        else:
            out.append(f"    [{tv}] = {{{blob}, {native}, {blob}, 0, NULL, NULL}}, /* {tname} */")
    out.append("};")

    with open(OUT, "w") as f:
        f.write("\n".join(out) + "\n")
    print(f"wrote {os.path.relpath(OUT, ROOT)}: {len(rows)} types, "
          f"{len(layouts)} structs, max type {maxtv}")


if __name__ == "__main__":
    main()
