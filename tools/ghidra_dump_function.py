#!/usr/bin/env python3
"""Print decompiled C, callers, callees, and referenced strings for one
function, so an agent (or a developer) can decide what to name it without
ever opening the Ghidra GUI.

Uses PyGhidra directly (native CPython, no analyzeHeadless -postScript):
Ghidra 12's .py GhidraScript provider requires PyGhidra, which is itself a
standalone Python library that starts its own embedded JVM -- there's no
longer a Jython .py script-running path in headless mode. This also means
no analyzeHeadless subprocess is needed at all for read operations; this
script is run directly with `python3`.

Usage: python3 ghidra_dump_function.py <project_dir> <project_name> <hex address>
Requires JAVA_HOME and GHIDRA_INSTALL_DIR to be set (tools/ghidra_re.ps1 sets both).
"""

import os
import sys

import pyghidra

pyghidra.start()

from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor


def get_function_at(program, addr_str):
    addr = program.getAddressFactory().getAddress(addr_str)
    func = program.getFunctionManager().getFunctionAt(addr)
    if func is None:
        raise Exception("No function at {}".format(addr_str))
    return func


def print_decompiled(program, func):
    decompiler = DecompInterface()
    decompiler.openProgram(program)
    try:
        results = decompiler.decompileFunction(func, 60, ConsoleTaskMonitor())
        if not results.decompileCompleted():
            print("-- decompile failed: {}".format(results.getErrorMessage()))
            return
        print("-- decompiled C --")
        print(results.getDecompiledFunction().getC())
    finally:
        decompiler.dispose()


def print_callers_callees(program, func):
    print("-- callers --")
    for ref in program.getReferenceManager().getReferencesTo(func.getEntryPoint()):
        if ref.getReferenceType().isCall():
            caller = program.getFunctionManager().getFunctionContaining(ref.getFromAddress())
            caller_name = caller.getName() if caller else "?"
            print("  {} ({})".format(ref.getFromAddress(), caller_name))

    print("-- callees --")
    for instr in program.getListing().getInstructions(func.getBody(), True):
        for ref in instr.getReferencesFrom():
            if ref.getReferenceType().isCall():
                callee = program.getFunctionManager().getFunctionAt(ref.getToAddress())
                if callee:
                    print("  {} ({})".format(ref.getToAddress(), callee.getName()))


def print_strings(program, func):
    print("-- referenced strings --")
    listing = program.getListing()
    for instr in listing.getInstructions(func.getBody(), True):
        for ref in instr.getReferencesFrom():
            data = listing.getDataAt(ref.getToAddress())
            if data is not None and data.hasStringValue():
                print("  {} : {}".format(ref.getToAddress(), data.getValue()))


def main():
    if len(sys.argv) != 4:
        raise SystemExit(
            "Usage: ghidra_dump_function.py <project_dir> <project_name> <hex address>"
        )
    project_dir, project_name, addr_str = sys.argv[1:4]

    with pyghidra.open_project(os.path.abspath(project_dir), project_name) as project:
        with pyghidra.program_context(project, "/default.xex") as program:
            func = get_function_at(program, addr_str)
            print("Function: {} @ {}".format(func.getName(), func.getEntryPoint()))
            print("Size: {} bytes".format(func.getBody().getNumAddresses()))
            print_decompiled(program, func)
            print_callers_callees(program, func)
            print_strings(program, func)


if __name__ == "__main__":
    main()
