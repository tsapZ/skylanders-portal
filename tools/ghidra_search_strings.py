#!/usr/bin/env python3
"""Search every defined string in the program for one or more terms
(case-insensitive substring match), printing each hit's address, the
string itself, and any functions that reference it -- a starting point
for finding code with no symbols to go on, before ever calling `dump`
on a guessed address.

Uses PyGhidra directly (see tools/ghidra_dump_function.py for why: no
analyzeHeadless -postScript needed).

Usage: python3 ghidra_search_strings.py <project_dir> <project_name> <term> [<term> ...]
Requires JAVA_HOME and GHIDRA_INSTALL_DIR to be set (tools/ghidra_re.ps1 sets both).
"""

import os
import sys

import pyghidra

pyghidra.start()


def find_referencing_functions(program, addr):
    func_manager = program.getFunctionManager()
    names = []
    for ref in program.getReferenceManager().getReferencesTo(addr):
        func = func_manager.getFunctionContaining(ref.getFromAddress())
        if func is not None:
            names.append(func.getName())
    return sorted(set(names))


def main():
    if len(sys.argv) < 4:
        raise SystemExit(
            "Usage: ghidra_search_strings.py <project_dir> <project_name> <term> [<term> ...]"
        )
    project_dir, project_name = sys.argv[1:3]
    terms = [t.lower() for t in sys.argv[3:]]

    with pyghidra.open_project(os.path.abspath(project_dir), project_name) as project:
        with pyghidra.program_context(project, "/default.xex") as program:
            listing = program.getListing()
            hits = 0
            for data in listing.getDefinedData(True):
                if not data.hasStringValue():
                    continue
                value = data.getValue()
                if value is None:
                    continue
                text = str(value)
                lower = text.lower()
                if not any(term in lower for term in terms):
                    continue
                hits += 1
                addr = data.getAddress()
                referencing = find_referencing_functions(program, addr)
                print("{} : {!r}".format(addr, text))
                if referencing:
                    print("    referenced by: {}".format(", ".join(referencing)))

            print("{} match(es) for {}".format(hits, terms))


if __name__ == "__main__":
    main()
