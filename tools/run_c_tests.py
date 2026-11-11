#!/usr/bin/env python3
"""Compile and run portable C suites with every discovered host compiler."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from c_compiler import (compile_executable, discover_compilers,
                        sanitizer_capabilities)  # noqa: E402
from c_test_matrix import (REQUIRED_C_SUITES, SANITIZED,  # noqa: E402
                           execute_compiler_matrix, matrix_output)

COMMON = [
    ROOT / 'libraries/AutoLinkCore/src/autolink_core.c',
    ROOT / 'libraries/AutoLinkCore/src/autolink_node_engine.c',
    ROOT / 'libraries/AutoLinkCore/src/autolink_persistence.c',
    ROOT / 'libraries/AutoLinkCore/src/autolink_master.c',
    ROOT / 'libraries/AutoLinkCore/src/generated/autolink_semantics.c',
    ROOT / 'libraries/GSProto/src/gs_frame.c',
    ROOT / 'libraries/GSProto/src/gs_crc16.c',
    ROOT / 'libraries/GSProto/src/gs_murmur3.c',
]
INCLUDES = [ROOT / 'libraries/AutoLinkCore/src',
            ROOT / 'libraries/GSProto/src']
TESTS = {name: ROOT / 'tests' / (name + '.c')
         for name in REQUIRED_C_SUITES}


def main():
    compilers = discover_compilers()
    if not compilers:
        raise SystemExit('no supported C compiler discovered')
    suffix = '.exe' if sys.platform == 'win32' else ''

    def execute(compiler, cell):
        test = TESTS[cell.suite]
        sanitized = cell.mode == SANITIZED
        stem = cell.suite + ('-sanitize' if sanitized else '')
        output = ROOT / 'build' / 'c-tests' / compiler.family / (stem + suffix)
        compile_executable(compiler, output, [test, *COMMON], INCLUDES,
                           sanitize=sanitized)
        subprocess.run([str(output)], env=compiler.environment, check=True)
        detail = (f' sanitizers={",".join(cell.sanitizers)}'
                  if sanitized else '')
        print(f'compiler={compiler.family} suite={cell.suite}'
              f'{detail} PASS')

    report = execute_compiler_matrix(compilers, execute,
                                     sanitizer_capabilities)
    print(matrix_output(report))


if __name__ == '__main__':
    main()
