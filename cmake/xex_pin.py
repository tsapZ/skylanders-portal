"""xex_pin.py <default.xex>: prints the file's code pin, "<code address, 8 hex digits>:<SHA-256 of the XEX2 page
descriptors from the first code page to the end>", the same value src/xex/xex_header.h computes (ParseXexHeader +
CodePin). cmake/game_xex.cmake runs it on the game's own rom/default.xex at configure time: a build can only run discs
with the code it was recompiled from (Core of Light #118 and #135, 2026-11-22: SuperChargers' French/Dutch and
Scandinavian discs have other code than the downloads were built from, and crashed at start). Prints nothing for a file
that isn't an XEX2 with code pages. Stdlib only."""
import hashlib
import struct
import sys


def be32(data, offset):
    return struct.unpack_from(">I", data, offset)[0]


def code_pin(data):
    if len(data) < 0x18 or data[:4] != b"XEX2":
        return None
    header_size = be32(data, 0x08)
    security = be32(data, 0x10)
    if header_size > len(data) or security > header_size or header_size - security < 0x184:
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
        with open(sys.argv[1], "rb") as f:
            head = f.read(16)
            if len(head) < 16:
                return
            size = struct.unpack_from(">I", head, 8)[0]
            f.seek(0)
            data = f.read(min(size, 64 * 1024 * 1024))
    except (OSError, IndexError):
        return
    pin = code_pin(data)
    if pin:
        print(pin)


main()
