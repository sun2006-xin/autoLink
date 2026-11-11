"""Portable host-C compiler discovery used by conformance and release gates."""
from dataclasses import dataclass
from pathlib import Path
import hashlib
import os
import re
import shutil
import subprocess


@dataclass(frozen=True)
class CompilerIdentity:
    """Positive self-identification evidence returned by one compiler."""

    family: str
    probe: str
    evidence_sha256: str


@dataclass(frozen=True)
class Compiler:
    family: str
    executable: str
    environment: dict
    identity_probe: str = ''
    identity_sha256: str = ''


@dataclass(frozen=True)
class CompilerCandidate:
    """A command-discovery result which does not yet imply a family."""

    command: str
    executable: str
    environment: dict


class CompilerIdentityError(RuntimeError):
    """A visible compiler candidate could not be positively identified."""


HOST_COMPILER_FAMILIES = ('gcc', 'clang', 'msvc')
_SHA256 = re.compile(r'^[0-9a-f]{64}$')


def canonical_compiler_executable(executable):
    """Return a host-native identity for compiler alias comparison.

    Use the path module selected when Python started, rather than ``Path``'s
    runtime ``os.name`` dispatch.  This keeps discovery deterministic under
    platform-simulation tests while still resolving aliases and symlinks on
    the actual host.
    """
    return os.path.normcase(os.path.realpath(os.path.abspath(
        os.fspath(executable))))


def _msvc_environment():
    candidates = []
    found = shutil.which('vswhere')
    if found:
        candidates.append(Path(found))
    program_files_x86 = os.environ.get('ProgramFiles(x86)')
    if program_files_x86:
        candidates.append(Path(program_files_x86) /
                          'Microsoft Visual Studio/Installer/vswhere.exe')
    for vswhere in candidates:
        if not vswhere.is_file():
            continue
        install = subprocess.check_output(
            [str(vswhere), '-latest', '-products', '*', '-requires',
             'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
             '-property', 'installationPath'], text=True,
            encoding='utf-8', errors='replace').strip()
        if not install:
            continue
        vcvars = Path(install) / 'VC/Auxiliary/Build/vcvars64.bat'
        if not vcvars.is_file():
            continue
        raw = subprocess.check_output(
            ['cmd.exe', '/d', '/c', 'call vcvars64.bat >nul && set'],
            cwd=vcvars.parent).decode('cp936', errors='replace')
        environment = os.environ.copy()
        environment.update((line.split('=', 1)[0].upper(), line.split('=', 1)[1])
                           for line in raw.splitlines() if '=' in line)
        cl = shutil.which('cl', path=environment.get('PATH'))
        if cl:
            return cl, environment
    return None


def _run_identity_probe(executable, arguments, environment):
    try:
        result = subprocess.run(
            [executable, *arguments], input='', env=environment,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False,
            text=True, encoding='utf-8', errors='replace', timeout=10)
        return result.returncode, result.stdout
    except (OSError, subprocess.SubprocessError) as error:
        return -1, f'{type(error).__name__}: {error}'


def _identity(family, probe, output):
    normalized = output.replace('\r\n', '\n').replace('\r', '\n')
    evidence = hashlib.sha256(
        f'{family}\0{probe}\0{normalized}'.encode('utf-8')).hexdigest()
    return CompilerIdentity(family, probe, evidence)


def classify_compiler_executable(executable, environment=None,
                                 probe_runner=None):
    """Classify a compiler from its own macros/version, never its command name."""
    if not executable:
        raise CompilerIdentityError('compiler executable is empty')
    environment = environment or os.environ.copy()
    runner = probe_runner or _run_identity_probe
    outputs = []

    macro_arguments = ('-dM', '-E', '-x', 'c', '-')
    returncode, output = runner(executable, macro_arguments, environment)
    output = output.decode('utf-8', errors='replace') \
        if isinstance(output, bytes) else str(output)
    outputs.append(('predefined-macros', returncode, output))
    if returncode == 0:
        macros = set(re.findall(
            r'^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)\b',
            output, flags=re.MULTILINE))
        if '__clang__' in macros:
            return _identity('clang', 'predefined-macros', output)
        if '_MSC_VER' in macros:
            return _identity('msvc', 'predefined-macros', output)
        if '__GNUC__' in macros:
            return _identity('gcc', 'predefined-macros', output)

    for arguments in (('--version',), ('/Bv',), ('/?',)):
        returncode, output = runner(executable, arguments, environment)
        output = output.decode('utf-8', errors='replace') \
            if isinstance(output, bytes) else str(output)
        outputs.append(('version', returncode, output))
        lower = output.lower()
        if re.search(r'\b(?:apple\s+)?clang\s+version\b', lower):
            return _identity('clang', 'version', output)
        if ('microsoft' in lower and
                re.search(r'c/c\+\+.*compiler', lower)):
            return _identity('msvc', 'version', output)
        if ('free software foundation' in lower or
                re.search(r'(?m)^.*\bgcc(?:\.exe)?(?:\s|\()', lower)):
            return _identity('gcc', 'version', output)

    evidence_text = '\n'.join(
        f'{probe}:{returncode}:{output}'
        for probe, returncode, output in outputs)
    return _identity('unresolved', 'unresolved', evidence_text)


def discover_compiler_candidates(candidate_resolver=None, host_os=None):
    """Discover executable candidates without inferring compiler family."""
    resolver = candidate_resolver or shutil.which
    host_os = host_os or os.name
    environment = os.environ.copy()
    candidates = []
    for command in ('gcc', 'clang', 'cc'):
        executable = resolver(command)
        if executable:
            candidates.append(CompilerCandidate(
                command, executable, environment.copy()))
    if host_os == 'nt':
        cl = resolver('cl')
        if cl:
            candidates.append(CompilerCandidate(
                'cl', cl, environment.copy()))
        else:
            msvc = _msvc_environment()
            if msvc:
                executable, msvc_environment = msvc
                candidates.append(CompilerCandidate(
                    'vswhere:cl', executable, msvc_environment))

    # Canonical paths own alias de-duplication only.  They never prove family.
    by_executable = {}
    for candidate in candidates:
        canonical = canonical_compiler_executable(candidate.executable)
        current = by_executable.get(canonical)
        if current is None or candidate.command < current.command:
            by_executable[canonical] = candidate
    return tuple(by_executable[key] for key in sorted(by_executable))


def discover_compilers(candidate_resolver=None, identity_classifier=None,
                       host_os=None):
    """Return one positively self-identified compiler for each host family."""
    classifier = identity_classifier or classify_compiler_executable
    by_family = {}
    unresolved = []
    for candidate in discover_compiler_candidates(candidate_resolver, host_os):
        identity = classifier(candidate.executable, candidate.environment)
        if identity.family not in HOST_COMPILER_FAMILIES or \
                not _SHA256.fullmatch(identity.evidence_sha256):
            unresolved.append(candidate.executable)
            continue
        compiler = Compiler(
            identity.family, candidate.executable, candidate.environment,
            identity_probe=identity.probe,
            identity_sha256=identity.evidence_sha256)
        current = by_family.get(identity.family)
        if current is None or canonical_compiler_executable(compiler.executable) < \
                canonical_compiler_executable(current.executable):
            by_family[identity.family] = compiler
    if unresolved:
        raise CompilerIdentityError(
            'unresolved compiler candidate identity: ' +
            ', '.join(sorted(canonical_compiler_executable(path)
                             for path in unresolved)))
    return [by_family[family] for family in HOST_COMPILER_FAMILIES
            if family in by_family]


def _arduino_data_roots():
    roots = []
    for name in ('ARDUINO_DATA_DIR', 'ARDUINO15_DIR'):
        value = os.environ.get(name)
        if value:
            roots.append(Path(value))
    local_app_data = os.environ.get('LOCALAPPDATA')
    if local_app_data:
        roots.append(Path(local_app_data) / 'Arduino15')
    roots.extend((Path.home() / '.arduino15',
                  Path.home() / 'Library/Arduino15'))
    return roots


def discover_esp32_riscv_compilers(data_roots=None):
    """Find Arduino ESP32 RISC-V GCC without treating it as a host compiler.

    The returned compiler is suitable for strict syntax compilation only; its
    output cannot be executed by the host conformance runner.
    """
    result = []
    path_compiler = shutil.which('riscv32-esp-elf-gcc')
    candidates = ([Path(path_compiler)]
                  if path_compiler and data_roots is None else [])
    roots = list(data_roots) if data_roots is not None else _arduino_data_roots()
    for root in roots:
        tool_root = Path(root) / 'packages/esp32/tools/esp-rv32'
        candidates.extend(tool_root.glob(
            '*/bin/riscv32-esp-elf-gcc.exe' if os.name == 'nt' else
            '*/bin/riscv32-esp-elf-gcc'))
    seen = set()
    for candidate in sorted(candidates, key=lambda item: str(item), reverse=True):
        if not candidate.is_file():
            continue
        resolved = str(candidate.resolve())
        key = os.path.normcase(resolved)
        if key in seen:
            continue
        seen.add(key)
        result.append(Compiler('esp32-riscv-gcc', resolved, os.environ.copy()))
    return result


def sanitizer_capabilities(compiler):
    if compiler.family in ('gcc', 'clang'):
        return ('ASan', 'UBSan')
    if compiler.family != 'msvc':
        return ()
    result = subprocess.run(
        [compiler.executable, '/?'], env=compiler.environment,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
    output = result.stdout if isinstance(result.stdout, bytes) else \
        result.stdout.encode('utf-8', errors='replace')
    return ('ASan',) if b'/fsanitize=' in output.lower() else ()


def compile_executable(compiler, output, sources, includes=(), sanitize=False):
    output = Path(output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    sources = [str(Path(source).resolve()) for source in sources]
    includes = [str(Path(include).resolve()) for include in includes]
    if compiler.family == 'msvc':
        object_dir = output.parent / (output.stem + '-objects')
        object_dir.mkdir(parents=True, exist_ok=True)
        command = [compiler.executable, '/nologo', '/std:c11', '/utf-8',
                   '/W4', '/WX', *('/I' + include for include in includes),
                   '/Fo:' + str(object_dir) + os.sep,
                   '/Fd:' + str(output.with_suffix('.compile.pdb'))]
        if sanitize:
            command += ['/fsanitize=address', '/Zi']
        command += [*sources, '/Fe:' + str(output), '/link', '/WX',
                    '/PDB:' + str(output.with_suffix('.pdb'))]
        if sanitize:
            command.append('/INCREMENTAL:NO')
    else:
        command = [compiler.executable, '-std=c11', '-Wall', '-Wextra',
                   '-Werror', '-pedantic', *('-I' + include for include in includes)]
        if sanitize:
            command += ['-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer']
        command += [*sources, '-o', str(output)]
    subprocess.run(command, cwd=output.parent, env=compiler.environment,
                   check=True)
    return output


def compile_syntax_only(compiler, sources, includes=(), cwd=None):
    """Strictly compile GCC-family sources without linking or host execution."""
    if compiler.family not in ('gcc', 'clang', 'esp32-riscv-gcc'):
        raise ValueError('syntax-only gate requires a GCC-compatible compiler')
    sources = [str(Path(source).resolve()) for source in sources]
    includes = [str(Path(include).resolve()) for include in includes]
    command = [compiler.executable, '-std=c11', '-Wall', '-Wextra',
               '-Werror', '-pedantic', '-fsyntax-only',
               *('-I' + include for include in includes), *sources]
    subprocess.run(command, cwd=Path(cwd).resolve() if cwd else None,
                   env=compiler.environment, check=True)
