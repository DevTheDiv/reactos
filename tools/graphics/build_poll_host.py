"""Build a private polling-only adapter around the pinned WINQ Alpha 10 renderer."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--compiler', default='gcc')
    parser.add_argument('--objdump', default='objdump')
    args = parser.parse_args()
    package, output = args.package.resolve(), args.output.resolve()
    source = Path(__file__).with_name('virgl_poll.c').resolve()
    renderer = package / 'bin/libvirglrenderer-1.dll'
    expected = 'e84b6a093badf92c61330e297900b17987349208b93237acfef2d81c366b9729'
    if digest(renderer) != expected:
        raise SystemExit('Unsupported renderer hash: this adapter is pinned to WINQ Alpha 10')
    if output.exists():
        raise SystemExit('Output already exists; choose a new private directory')
    if package in output.parents or output in package.parents:
        raise SystemExit('Package and output must be separate directory trees')
    machine = subprocess.check_output([args.compiler, '-dumpmachine'], text=True).strip()
    if machine != 'x86_64-w64-mingw32':
        raise SystemExit('An x86_64 MinGW Windows compiler is required')
    dump = subprocess.check_output([args.objdump, '-p', str(renderer)], text=True)
    exports = re.findall(r'^\s*\[\s*\d+\]\s+\+base\[\s*(\d+)\]\s+[0-9a-fA-F]+\s+([A-Za-z_][\w@?$]*)\s*$', dump, re.M)
    wrappers = {'virgl_renderer_init', 'virgl_renderer_context_create_with_flags'}
    if not wrappers.issubset({name for _, name in exports}) or len(exports) < 30:
        raise SystemExit('Unexpected renderer export table')
    # Copy only; the original package is never renamed or overwritten.
    shutil.copytree(package, output)
    original = output / 'bin/libvirglrenderer-original.dll'
    shutil.copyfile(renderer, original)
    definition = output / 'poll-adapter.def'
    lines = ['LIBRARY libvirglrenderer-1', 'EXPORTS']
    for ordinal, name in exports:
        target = name if name in wrappers else name + '=libvirglrenderer-original.' + name
        lines.append('    ' + target + ' @' + ordinal)
    definition.write_text('\n'.join(lines) + '\n')
    target = output / 'bin/libvirglrenderer-1.dll'
    command = [args.compiler, '-shared', '-O2', '-Wall', '-Wextra', '-Werror',
               '-Wno-cast-function-type', '-static-libgcc', str(source), str(definition), '-o', str(target)]
    subprocess.run(command, check=True)
    manifest = {'original_renderer_sha256': expected, 'adapter_sha256': digest(target),
                'adapter_source_sha256': digest(source), 'compiler': machine,
                'forwarded_exports': len(exports) - len(wrappers),
                'command': command, 'renderer_runtime_validated': False,
                'guest_vulkan_validated': False}
    (output / 'poll-adapter.json').write_text(json.dumps(manifest, indent=2) + '\n')
    package_manifest = output / 'host-package.json'
    if package_manifest.exists():
        package_info = json.loads(package_manifest.read_text(encoding='utf-8-sig'))
        package_info.update({'parent_package_renderer_sha256': expected,
                             'renderer_sha256': digest(target),
                             'experimental_poll_adapter': 'poll-adapter.json',
                             'renderer_runtime_validated': False, 'guest_vulkan_validated': False})
        package_info.pop('active_guest_validation', None)
        package_manifest.write_text(json.dumps(package_info, indent=2) + '\n')
    print('Prepared experimental polling host:', output)


if __name__ == '__main__':
    main()
