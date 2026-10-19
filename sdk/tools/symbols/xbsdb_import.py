#!/usr/bin/env python3
"""Fill a game config's [symbols] and [data] from XbSymbolDatabase, for an XDK build the SDK has no signatures for.

XbSymbolDatabase (github.com/Cxbx-Reloaded/XbSymbolDatabase, MIT) knows the libraries of every XDK build. Its CLI
prints "LIB__name = 0xaddr" lines; this keeps the names the SDK's runtime implements (the names of an existing
signatures.toml), renames the few it spells differently, and rewrites those two tables of the config in place.
Names it does not find stay 0, which the runtime treats as absent.

  XbSymbolDatabaseCLI assets/default.xbe > found.txt
  xbsdb_import.py found.txt <sdk> config/game.toml

The names: every function the runtime replaces (runtime/xdk: HLE_STDCALL(name, ...) and the like), every [symbols]
name of the SDK's signature sets, and their [data] names.
"""
import re
import sys


def lines(path):
    with open(path) as f:
        return f.read().splitlines(True)

# XbSymbolDatabase name -> SDK name
RENAME = {
    "D3D_g_pDevice": "D3D_pDevice",
    "D3D_g_RenderState": "D3D_RenderState",
    "D3D_g_DeferredTextureState": "D3D_DeferredTextureState",
    "D3D_g_Stream": "D3D_Streams",
    "D3D_MakeRequestedSpace_8": "D3D_MakeRequestedSpace",
    "g_DeviceType_Gamepad": "XPP_GamepadType",
    "g_DeviceType_MU": "XPP_MemoryUnitType",
}


def wanted_names(signatures):
    """[symbols] and [data] names of an SDK signature set, by table"""
    names, table = {"symbols": [], "data": []}, None
    for line in lines(signatures):
        m = re.match(r"\[(\w+)\]", line)
        if m:
            table = m.group(1)
            continue
        m = re.match(r"(\w+) = ", line)
        if m and table in names:
            names[table].append(m.group(1))
    return names


def runtime_names(sdk):
    """functions the runtime replaces by name"""
    import glob
    import os
    names = set()
    for path in glob.glob(os.path.join(sdk, "runtime", "xdk", "*.cpp")):
        for line in lines(path):
            m = re.match(r"\s*HLE_(?:STDCALL|FASTCALL|CDECL|WRAP)\((\w+)", line)
            if m:
                names.add(m.group(1))
    return names


def sdk_names(sdk):
    """[symbols] and [data] names the SDK knows: its signature sets and the functions its runtime replaces"""
    import glob
    import os
    names = {"symbols": set(runtime_names(sdk)), "data": set()}
    for sig in glob.glob(os.path.join(sdk, "xdk", "*", "signatures.toml")):
        for table, n in wanted_names(sig).items():
            names[table].update(n)
    return {t: sorted(n) for t, n in names.items()}


def found(path):
    """name -> address, the first library's when two print one name"""
    out = {}
    for line in lines(path):
        m = re.match(r"(\w+?)__(\w+) = (0x[0-9a-fA-F]+)", line)
        if m:
            name = RENAME.get(m.group(2), m.group(2))
            out.setdefault(name, int(m.group(3), 16))
    return out


def table_text(names, addresses, keep):
    width = max(len(n) for n in names)
    lines = []
    for n in sorted(names, key=str.lower):
        if n in keep:
            lines.append(keep[n])
            continue
        a = addresses.get(n, 0)
        lines.append(f"{n:<{width}} = {a:#X}".replace("0X", "0x") if a else f"{n:<{width}} = 0")
    return "\n".join(lines) + "\n"


def replace_table(config, name, body):
    """the table's lines up to the next table or comment block, replaced; kept values written by hand survive"""
    pat = re.compile(r"(^\[" + name + r"\]\n)(.*?)(?=^\[|^#|\Z)", re.S | re.M)
    if not pat.search(config):
        return config + f"\n[{name}]\n{body}"
    return pat.sub(lambda m: m.group(1) + body + "\n", config, count=1)


def hand_values(config, name):
    """values a person set in the table, kept: a comment after the address marks one"""
    m = re.search(r"^\[" + name + r"\]\n(.*?)(?=^\[|\Z)", config, re.S | re.M)
    out = {}
    for line in (m.group(1) if m else "").splitlines():
        v = re.match(r"(\w+)\s*=\s*(0x[0-9A-Fa-f]+)\s*#", line)
        if v:
            out[v.group(1)] = line
    return out


def main(argv):
    if len(argv) != 4:
        print(__doc__)
        return 1
    addresses, names = found(argv[1]), sdk_names(argv[2])
    config = "".join(lines(argv[3]))
    total = 0
    for table in ("symbols", "data"):
        keep = hand_values(config, table)
        config = replace_table(config, table, table_text(names[table], addresses, keep))
        total += sum(1 for n in names[table] if n in keep or addresses.get(n))
    with open(argv[3], "w") as f:
        f.write(config)
    print(f"{total} of {len(names['symbols']) + len(names['data'])} names found")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
