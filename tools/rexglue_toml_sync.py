"""Pure logic for merging Ghidra-discovered function names into the
[functions] section of a ReXGlue codegen-fix TOML file (config/default.toml),
in place, without reordering or dropping anything the merge doesn't touch --
existing entries, their key order, their comments, and every value the merge
doesn't specifically set (including value shapes this module doesn't itself
understand, like booleans, arrays, or single-quoted strings) stay exactly as
they were.

No file I/O and no Ghidra dependency here, so this is testable with plain
`python3 -m unittest`. tools/ghidra_rename_and_export.py is the headless
Ghidra driver that collects function data from the open program and calls
sync_functions().
"""

import re
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

# Matches one [functions] entry line: `0xADDR = { ... }` with an optional trailing
# comment. The body is captured whole and never re-parsed into a typed dict -- see
# _set_key_in_body, which edits it as text so unrecognized value shapes survive.
_ENTRY_RE = re.compile(r'^(0[xX][0-9a-fA-F]+)\s*=\s*\{(.*)\}\s*(#.*)?$')

# A [section] or [[array_table]] header, optionally with a trailing comment. Either
# form ends the [functions] section.
_SECTION_HEADER_RE = re.compile(r'^\[\[?[^\[\]]+\]\]?\s*(#.*)?$')

_NAME_VALUE_RE = re.compile(r'\bname\s*=\s*("(?:[^"\\]|\\.)*")')

# Recognized scalar/array value shapes for locating an existing `key = value` pair
# inside an entry body, so it can be replaced without disturbing anything else on
# the line. This does not need to recognize every TOML shape ReXGlue's schema
# supports -- only enough to find OUR OWN previously-written values (name, in
# practice) for in-place replacement; a value shape it doesn't recognize is never
# an existing occurrence of a key we're setting, so it's simply left alone.
_VALUE_RE = (
    r'"(?:[^"\\]|\\.)*"'      # double-quoted string
    r"|'[^']*'"               # single-quoted string
    r'|0[xX][0-9a-fA-F]+'     # hex integer
    r'|-?\d+(?:\.\d+)?'       # integer or float
    r'|true|false'            # boolean
    r'|\[[^\]]*\]'            # a flat array (no nested brackets)
)


@dataclass
class SyncResult:
    lines: List[str]
    updated: List[int] = field(default_factory=list)
    added: List[int] = field(default_factory=list)
    conflicts: List[Tuple[str, int, int]] = field(default_factory=list)


def parse_entry(line: str) -> Optional[Tuple[int, str, str, Optional[str]]]:
    """Parse one `0xADDR = { ... }` line. Returns
    (address, address_string_as_written, raw_body, trailing_comment_or_None), or
    None if the line isn't a functions-entry line (a comment, blank line, or
    section header). `raw_body` is the untouched text between the braces --
    never split into a typed dict, so nothing about it can be lost."""
    m = _ENTRY_RE.match(line.strip())
    if not m:
        return None
    addr_str = m.group(1)
    return int(addr_str, 16), addr_str, m.group(2), m.group(3)


def format_entry(addr_str: str, props: Dict[str, str]) -> str:
    """Render one BRAND-NEW entry line with a consistent key order (name,
    parent, size, then anything else). Only used for addresses that don't
    already have a line -- an existing line is edited in place by
    _set_key_in_body instead, so this never needs to round-trip unknown
    value shapes."""
    if not props:
        return f"{addr_str} = {{}}\n"
    key_order = ["name", "parent", "size"]
    parts = [f"{k} = {props[k]}" for k in key_order if k in props]
    parts += [f"{k} = {v}" for k, v in props.items() if k not in key_order]
    return f"{addr_str} = {{ {', '.join(parts)} }}\n"


def _set_key_in_body(body: str, key: str, value: str) -> str:
    """Insert or replace `key = value` inside an entry's `{...}` body, as a
    text edit -- every other key, its value (whatever shape it is), and all
    surrounding whitespace/formatting is left byte-for-byte untouched."""
    pattern = re.compile(r'\b' + re.escape(key) + r'\s*=\s*(?:' + _VALUE_RE + r')')
    replacement = f'{key} = {value}'
    new_body, count = pattern.subn(replacement, body, count=1)
    if count:
        return new_body
    stripped = body.strip()
    if not stripped:
        return replacement
    return f'{replacement}, {stripped}'


def find_functions_section(lines: List[str]) -> Tuple[int, int]:
    """Return (start, end): start is the index of the '[functions]' header
    line itself, end is the index of the next top-level '[section]' or
    '[[array_table]]' header, or len(lines) if [functions] runs to the end
    of the file (it's the last section in config/default.toml today)."""
    start = None
    for i, line in enumerate(lines):
        if line.strip() == "[functions]":
            start = i
            break
    if start is None:
        raise ValueError("No [functions] section found in the TOML file.")
    end = len(lines)
    for i in range(start + 1, len(lines)):
        if _SECTION_HEADER_RE.match(lines[i].strip()):
            end = i
            break
    return start, end


def quoted_name(raw_name: str) -> str:
    """Sanitize a Ghidra function name into a valid C/C++ identifier and
    wrap it as a TOML string value, e.g. 'Update Camera!' -> '"Update_Camera"'.
    Raises ValueError if nothing valid survives sanitizing (e.g. a name made
    entirely of punctuation) -- writing an empty `name = ""` would produce an
    invalid C++ identifier and break the codegen build."""
    name = re.sub(r'[^a-zA-Z0-9_]', '_', raw_name)
    name = re.sub(r'_+', '_', name).strip('_')
    if name and name[0].isdigit():
        name = '_' + name
    if not name:
        raise ValueError(f"{raw_name!r} sanitizes to an empty/invalid identifier")
    return f'"{name}"'


def decimal_size(size: int) -> str:
    """Format a byte count as the project's decimal convention (every
    existing entry in config/default.toml uses e.g. `size = 12`, never hex)."""
    return str(size)


def sync_functions(lines: List[str], updates: Dict[int, Dict[str, str]]) -> SyncResult:
    """Merge `updates` (address -> {"name": '"Foo"', ...}, values already
    formatted as raw TOML) into the [functions] section of `lines`.

    Existing entries are edited in place as text (_set_key_in_body): only the
    given keys change, and every other key, value shape, and trailing comment
    on that line is preserved verbatim. Addresses not already present are
    appended at the end of the section, sorted by address.

    Conflict checking is batch-aware and all-or-nothing: it computes what
    every involved address's `name` would be *after* this whole batch is
    applied (so two addresses trading names in the same call is fine), and if
    any name would end up shared by more than one address, the ENTIRE batch
    is refused -- lines are returned unchanged, updated/added are empty, and
    conflicts lists what collided. This is deliberate: applying some of a
    batch while refusing the rest would leave the caller's other system
    (Ghidra's own function names, in practice) and this file out of sync."""
    start, end = find_functions_section(lines)
    out = list(lines)

    existing = {}  # addr_int -> (line_index, addr_str, body, comment)
    for i in range(start + 1, end):
        parsed = parse_entry(out[i])
        if parsed:
            addr_int, addr_str, body, comment = parsed
            existing[addr_int] = (i, addr_str, body, comment)

    final_name: Dict[int, str] = {}
    for addr_int, (_, _, body, _) in existing.items():
        name = _NAME_VALUE_RE.search(body)
        if name:
            final_name[addr_int] = name.group(1)
    for addr_int, kv in updates.items():
        if "name" in kv:
            final_name[addr_int] = kv["name"]

    owners: Dict[str, List[int]] = {}
    for addr_int, name in final_name.items():
        owners.setdefault(name, []).append(addr_int)

    conflicts = [
        (name, max(addrs), min(addrs)) for name, addrs in owners.items() if len(addrs) > 1
    ]
    if conflicts:
        return SyncResult(lines=out, conflicts=conflicts)

    result = SyncResult(lines=out)
    remaining = dict(updates)

    for addr_int, (i, addr_str, body, comment) in existing.items():
        if addr_int not in remaining:
            continue
        kv = remaining.pop(addr_int)
        new_body = body
        for key, value in kv.items():
            new_body = _set_key_in_body(new_body, key, value)
        if new_body != body:
            suffix = f" {comment}" if comment else ""
            out[i] = f"{addr_str} = {{ {new_body.strip()} }}{suffix}\n"
            result.updated.append(addr_int)

    insert_at = end
    for addr_int in sorted(remaining):
        addr_str = f"0x{addr_int:08X}"
        out.insert(insert_at, format_entry(addr_str, dict(remaining[addr_int])))
        insert_at += 1
        result.added.append(addr_int)

    return result
