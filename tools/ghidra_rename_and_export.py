#!/usr/bin/env python3
"""Rename one or more functions in the current Ghidra program and sync
those names into config/default.toml's [functions] section in the same
call, so renaming and exporting is one non-interactive step.

Uses PyGhidra directly (native CPython, no analyzeHeadless -postScript) --
see tools/ghidra_dump_function.py for why: Ghidra 12's .py GhidraScript
provider requires PyGhidra, which is itself a standalone Python library
that starts its own embedded JVM, so there's no separate script-runner
step needed at all.

Only ever sets `name` in config/default.toml -- never `size` or any other
key. A [functions] entry's size/end define CONFIG-authority function
boundaries in ReXGlue's codegen (see thirdparty/rexglue-sdk's
FunctionAuthority::CONFIG); Ghidra's own idea of a function's size
(func.getBody().getNumAddresses(), which counts every address across all of
a function's chunks, not the contiguous extent ReXGlue's `size` means) has
no business overwriting a hand-verified boundary, and for a brand-new entry
it would silently pin one that was never actually verified.

The TOML sync is dry-run BEFORE any Ghidra database change: if the batch has
a name conflict, nothing is renamed in Ghidra and nothing is written to the
TOML, and the script exits non-zero -- so Ghidra's database and the TOML
file can never end up disagreeing with each other over a partially-applied
batch.

Usage: python3 ghidra_rename_and_export.py <project_dir> <project_name> <toml path> <hex address> <name> [<hex address> <name> ...]
Requires JAVA_HOME and GHIDRA_INSTALL_DIR to be set (tools/ghidra_re.ps1 sets both).
"""

import os
import sys

import pyghidra

pyghidra.start()

from ghidra.program.model.symbol import SourceType

from rexglue_toml_sync import quoted_name, sync_functions


def main():
    if len(sys.argv) < 6 or len(sys.argv) % 2 != 0:
        raise SystemExit(
            "Usage: ghidra_rename_and_export.py <project_dir> <project_name> <toml path> "
            "<hex address> <name> [<hex address> <name> ...]"
        )
    project_dir, project_name, toml_path = sys.argv[1:4]
    pairs = sys.argv[4:]

    with pyghidra.open_project(os.path.abspath(project_dir), project_name) as project:
        with pyghidra.program_context(project, "/default.xex") as program:
            addr_factory = program.getAddressFactory()
            updates = {}
            for i in range(0, len(pairs), 2):
                addr_str, name = pairs[i], pairs[i + 1]
                addr = addr_factory.getAddress(addr_str)
                updates[addr.getOffset()] = {"name": quoted_name(name)}

            with open(toml_path, "r") as f:
                lines = f.readlines()

            result = sync_functions(lines, updates)

            if result.conflicts:
                print("Export refused: name conflict(s), nothing renamed or saved:")
                for name, addr_a, addr_b in result.conflicts:
                    print("    {} : 0x{:08X} conflicts with 0x{:08X}".format(
                        name, addr_a, addr_b))
                raise SystemExit(1)

            func_manager = program.getFunctionManager()
            with pyghidra.transaction(program, "Rename functions"):
                for i in range(0, len(pairs), 2):
                    addr_str, name = pairs[i], pairs[i + 1]
                    addr = addr_factory.getAddress(addr_str)
                    func = func_manager.getFunctionAt(addr)
                    if func is None:
                        raise Exception("No function at {}".format(addr_str))
                    func.setName(name, SourceType.USER_DEFINED)

            program.save("Renamed functions", pyghidra.task_monitor())

    with open(toml_path, "w") as f:
        f.writelines(result.lines)

    print("Export complete.")
    print("  {} existing entries updated".format(len(result.updated)))
    print("  {} new entries added".format(len(result.added)))
    print("Saved to: {}".format(toml_path))


if __name__ == "__main__":
    main()
