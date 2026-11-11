"""Single semantic owner for the portable host-C verification matrix."""
from dataclasses import dataclass
import hashlib
import json
import re

from c_compiler import (Compiler, canonical_compiler_executable,
                        sanitizer_capabilities)

REQUIRED_C_SUITES = (
    'test_autolink_core',
    'test_persistence',
    'test_master_core',
    'test_sc2_completion_architecture',
    'test_sc2_root_cause_closure',
    'test_sc2_round2_closure',
    'test_sc2_round4_closure',
    'test_sc2_round5_closure',
    'test_sc2_timer_observation_closure',
    'test_public_integration_boundary',
)
STRICT = 'strict'
SANITIZED = 'sanitized'
REPORT_PREFIX = 'AUTOLINK_C_MATRIX_JSON='
REPORT_SCHEMA = 'autolink-c-compiler-matrix-v2'
SUPPORTED_COMPILER_FAMILIES = ('gcc', 'clang', 'msvc')


class CompilerMatrixError(AssertionError):
    """The discovered, required and executed compiler matrices disagree."""


@dataclass(frozen=True)
class MatrixCell:
    family: str
    suite: str
    mode: str
    sanitizers: tuple = ()

    def as_dict(self):
        return {'family': self.family, 'suite': self.suite,
                'mode': self.mode, 'sanitizers': list(self.sanitizers)}

    @classmethod
    def from_dict(cls, value):
        if not isinstance(value, dict):
            raise CompilerMatrixError('matrix cell is not an object')
        try:
            return cls(value['family'], value['suite'], value['mode'],
                       tuple(value['sanitizers']))
        except (KeyError, TypeError) as error:
            raise CompilerMatrixError('malformed matrix cell') from error


@dataclass(frozen=True)
class CompilerMatrixPlan:
    compilers: tuple
    capabilities: tuple
    cells: tuple

    @property
    def families(self):
        return tuple(compiler.family for compiler in self.compilers)


def _normalize_compilers(compilers):
    normalized = []
    by_family = {}
    by_executable = {}
    for compiler in compilers:
        family = str(compiler.family).strip().lower()
        if not family or not compiler.executable:
            raise CompilerMatrixError('compiler family/executable is empty')
        if family not in SUPPORTED_COMPILER_FAMILIES:
            raise CompilerMatrixError(f'unsupported compiler family: {family}')
        if not compiler.identity_probe or not re.fullmatch(
                r'[0-9a-f]{64}', compiler.identity_sha256):
            raise CompilerMatrixError('compiler self-identity evidence missing')
        executable = canonical_compiler_executable(compiler.executable)
        if family in by_family:
            if by_family[family] == executable:
                continue
            raise CompilerMatrixError(
                f'duplicate compiler family has different executables: {family}')
        if executable in by_executable:
            raise CompilerMatrixError(
                'one executable classified as multiple compiler families: '
                f'{by_executable[executable]},{family}')
        by_family[family] = executable
        by_executable[executable] = family
        normalized.append(Compiler(
            family, compiler.executable, compiler.environment,
            identity_probe=compiler.identity_probe,
            identity_sha256=compiler.identity_sha256))
    if not normalized:
        raise CompilerMatrixError('no supported C compiler discovered')
    return tuple(normalized)


def build_compiler_matrix_plan(compilers,
                               capability_resolver=sanitizer_capabilities):
    if not REQUIRED_C_SUITES or \
            len(REQUIRED_C_SUITES) != len(set(REQUIRED_C_SUITES)):
        raise CompilerMatrixError('required C suite registry is empty/duplicate')
    normalized = _normalize_compilers(compilers)
    capabilities = []
    cells = []
    for compiler in normalized:
        sanitizers = tuple(dict.fromkeys(capability_resolver(compiler)))
        capabilities.append((compiler.family, sanitizers))
        cells.extend(MatrixCell(compiler.family, suite, STRICT)
                     for suite in REQUIRED_C_SUITES)
        if sanitizers:
            cells.extend(MatrixCell(compiler.family, suite, SANITIZED,
                                    sanitizers)
                         for suite in REQUIRED_C_SUITES)
    return CompilerMatrixPlan(normalized, tuple(capabilities), tuple(cells))


def _cell_text(cell):
    return f'{cell.family}/{cell.suite}/{cell.mode}'


def validate_execution_records(plan, records):
    records = tuple(records)
    if len(set(records)) != len(records):
        raise CompilerMatrixError('duplicate matrix cell in execution records')
    expected = set(plan.cells)
    actual = set(records)
    missing = sorted(expected - actual,
                     key=lambda cell: (cell.family, cell.suite, cell.mode))
    if missing:
        raise CompilerMatrixError('missing matrix cell: ' +
                                  _cell_text(missing[0]))
    unexpected = sorted(actual - expected,
                        key=lambda cell: (cell.family, cell.suite, cell.mode))
    if unexpected:
        raise CompilerMatrixError('unexpected matrix cell: ' +
                                  _cell_text(unexpected[0]))
    return True


def _capability_rows(plan):
    by_family = {compiler.family: hashlib.sha256(
        canonical_compiler_executable(compiler.executable).encode('utf-8')
    ).hexdigest() for compiler in plan.compilers}
    compilers = {compiler.family: compiler for compiler in plan.compilers}
    return [{'family': family, 'executable_id': by_family[family],
             'identity_probe': compilers[family].identity_probe,
             'identity_sha256': compilers[family].identity_sha256,
             'sanitizers': list(sanitizers)}
            for family, sanitizers in plan.capabilities]


def build_matrix_report(plan, records):
    records = tuple(records)
    validate_execution_records(plan, records)
    return {
        'schema': REPORT_SCHEMA,
        'status': 'PASS',
        'required_suites': list(REQUIRED_C_SUITES),
        'compilers': _capability_rows(plan),
        'required_cells': [cell.as_dict() for cell in plan.cells],
        'executed_cells': [cell.as_dict() for cell in records],
        'execution_count': len(records),
    }


def _parse_cells(value, label):
    if not isinstance(value, list):
        raise CompilerMatrixError(f'{label} is not a list')
    return tuple(MatrixCell.from_dict(item) for item in value)


def validate_matrix_report(report, compilers,
                           capability_resolver=sanitizer_capabilities):
    if not isinstance(report, dict) or report.get('schema') != REPORT_SCHEMA or \
            report.get('status') != 'PASS':
        raise CompilerMatrixError('invalid compiler matrix report identity')
    plan = build_compiler_matrix_plan(compilers, capability_resolver)
    if report.get('required_suites') != list(REQUIRED_C_SUITES):
        raise CompilerMatrixError('required suite registry mismatch')
    if report.get('compilers') != _capability_rows(plan):
        raise CompilerMatrixError('compiler capability mismatch')
    required = _parse_cells(report.get('required_cells'), 'required_cells')
    if required != plan.cells:
        raise CompilerMatrixError('required matrix cells mismatch')
    executed = _parse_cells(report.get('executed_cells'), 'executed_cells')
    if report.get('execution_count') != len(executed):
        raise CompilerMatrixError('execution count mismatch')
    validate_execution_records(plan, executed)
    return plan


def matrix_output(report):
    try:
        count = report['execution_count']
        families = ','.join(row['family'] for row in report['compilers'])
    except (KeyError, TypeError) as error:
        raise CompilerMatrixError('malformed matrix report summary') from error
    machine = REPORT_PREFIX + json.dumps(
        report, sort_keys=True, separators=(',', ':'))
    human = (f'portable C suites: {count} builds/runs PASS; '
             f'compilers={families}')
    return machine + '\n' + human


def _parse_matrix_report(output):
    lines = [line[len(REPORT_PREFIX):] for line in output.splitlines()
             if line.startswith(REPORT_PREFIX)]
    if len(lines) != 1:
        raise CompilerMatrixError('expected exactly one compiler matrix report')
    try:
        return json.loads(lines[0])
    except json.JSONDecodeError as error:
        raise CompilerMatrixError('invalid compiler matrix JSON') from error


def validate_matrix_output(output, compilers,
                           capability_resolver=sanitizer_capabilities):
    report = _parse_matrix_report(output)
    plan = validate_matrix_report(report, compilers, capability_resolver)
    summaries = [line for line in output.splitlines()
                 if re.fullmatch(
                     r'portable C suites: \d+ builds/runs PASS; compilers=.*',
                     line)]
    expected = matrix_output(report).splitlines()[-1]
    if summaries != [expected]:
        raise CompilerMatrixError('human summary mismatch')
    return plan


def execute_compiler_matrix(compilers, execute,
                            capability_resolver=sanitizer_capabilities):
    plan = build_compiler_matrix_plan(compilers, capability_resolver)
    by_family = {compiler.family: compiler for compiler in plan.compilers}
    records = []
    for cell in plan.cells:
        result = execute(by_family[cell.family], cell)
        if result is False:
            raise CompilerMatrixError('matrix executor reported failure: ' +
                                      _cell_text(cell))
        records.append(cell)
    return build_matrix_report(plan, records)
