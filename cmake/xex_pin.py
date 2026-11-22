"""xex_pin.py <default.xex>: prints the file's code pin, "<code address, 8 hex digits>:<SHA-256 of the XEX2 page
descriptors from the first code page to the end>", the same value src/xex/xex_verify.cpp's ReadHeaderFacts and
src/xex/xex_header.h's ParseXexHeader + CodePin compute: the same read limits (the first 64 KiB, the whole header when
it is at most 1 MiB) and the same checks (the optional-header table, the execution info 0x00040006). cmake/game_xex.cmake
runs it on the game's own rom/default.xex at configure time: a build can only run discs with the code it was
recompiled from (Core of Light #118 and #135, 2026-11-22: SuperChargers' French/Dutch and Scandinavian discs have other
code than the downloads were built from, and crashed at start). Prints nothing (exit 0) for a file the game wouldn't
get a pin for either; exit 1 when the file can't be read. Stdlib only."""
import hashlib
import struct
import sys


def be32(data, offset):
    return struct.unpack_from(">I", data, offset)[0]


def read_head(path):
    """The bytes ReadHeaderFacts looks at."""
    with open(path, "rb") as f:
        head = f.read(64 * 1024)
        if len(head) >= 12:
            header_size = be32(head, 8)
            if len(head) < header_size <= (1 << 20):
                head += f.read(header_size - len(head))
    return head


def code_pin(data):
    """ParseXexHeader's checks, then the pin; None where it returns nullopt or there are no code pages."""
    size = len(data)
    if size < 0x18 or data[:4] != b"XEX2":
        return None
    header_size = be32(data, 0x08)
    security = be32(data, 0x10)
    count = be32(data, 0x14)
    if (header_size > size or count > header_size // 8 or 0x18 + count * 8 > header_size or security > header_size
            or header_size - security < 0x184):
        return None
    execution = False
    for i in range(count):
        entry = 0x18 + i * 8
        if be32(data, entry) != 0x00040006:
            continue
        at = be32(data, entry + 4)
        if at > header_size or header_size - at < 24:
            return None
        execution = True
    if not execution:
        return None
    flags = be32(data, security + 0x10C)
    page_size = 0x1000 if flags & 0x10000000 else 0x10000
    load = be32(data, security + 0x110)
    pages = be32(data, security + 0x180)
    if pages > (header_size - security - 0x184) // 24:
        return None
    page = 0
    for i in range(pages):
        value = be32(data, security + 0x184 + i * 24)
        if value & 0xF == 1:  # XEX_SECTION_CODE
            start = security + 0x184 + i * 24
            end = security + 0x184 + pages * 24
            return "%08X:%s" % ((load + page * page_size) & 0xFFFFFFFF, hashlib.sha256(data[start:end]).hexdigest())
        page += value >> 4
    return None


def main():
    try:
        head = read_head(sys.argv[1])
    except (OSError, IndexError) as e:
        print("xex_pin.py: %s" % e, file=sys.stderr)
        sys.exit(1)
    pin = code_pin(head)
    if pin:
        print(pin)


main()
