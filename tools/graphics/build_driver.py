# SPDX-License-Identifier: GPL-2.0-or-later
"""Targeted i386 RosBE build using an existing MinGW Makefiles build's headers.

All driver and virtio C files come from this checkout. Generated headers and
import libraries come from --build-dir. This is not a clean full OS build.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--rosbe", type=Path, default=root.parent / "tools" / "RosBE")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent / "artifacts" / "driver")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    cache = (build / "CMakeCache.txt").read_text()
    cache_source = re.search(r"^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$", cache, re.M).group(1)
    env = dict(os.environ)
    env["PATH"] = str(args.rosbe / "bin") + os.pathsep + str(args.rosbe / "i386/bin") + os.pathsep + env["PATH"]
    compiler = args.rosbe / "i386/bin/gcc.exe"
    sources = []
    with (out / "build.log").open("w") as log:
        def run(argv, cwd):
            print(" ".join(map(str, argv)), file=log, flush=True)
            subprocess.run(list(map(str, argv)), cwd=cwd, env=env,
                           stdout=log, stderr=subprocess.STDOUT, check=True)

        def compile_group(directory, target, files):
            flags = (directory / f"CMakeFiles/{target}.dir/flags.make").read_text()
            values = dict(re.findall(r"^(C_\w+) = (.*)$", flags, re.M))
            defines = re.sub(r" -D__RELFILE__=.*?(?= -D__i386__)", "", values["C_DEFINES"])
            includes = (directory / f"CMakeFiles/{target}.dir/includes_C.rsp").read_text()
            includes = includes.replace(cache_source, root.as_posix()).replace(
                cache_source.replace("/", "\\"), str(root))
            include_file = out / (target + "-includes.rsp")
            include_file.write_text(includes)
            objects = []
            for source in files:
                sources.append(source)
                obj = out / (source.name + ".obj")
                argv = ([compiler] + shlex.split(defines) + ["@" + str(include_file),
                        "-I" + str(root / "sdk/lib/drivers/virtio")] + shlex.split(values["C_FLAGS"]) +
                        ["-Wno-unused-function", "-Wno-attributes",
                         '-D__RELFILE__="' + source.relative_to(root).as_posix() + '"',
                         "-c", source, "-o", obj])
                run(argv, directory)
                objects.append(obj)
            return objects

        library_dir = build / "sdk/lib/drivers/virtio"
        library_sources = [root / "sdk/lib/drivers/virtio" / name for name in
                           ("VirtIOPCICommon.c", "VirtIOPCILegacy.c", "VirtIOPCIModern.c",
                            "VirtIORing.c", "VirtIORing-Packed.c")]
        objects = compile_group(library_dir, "virtio", library_sources)
        archive = out / "libvirtio-local.a"
        run([args.rosbe / "i386/bin/ar.exe", "rcs", archive] + objects, out)
        link_dir = build / "drivers/sac/driver"
        driver = root / "drivers/virtio/vgpudiag"
        driver_objects = compile_group(link_dir, "sacdrv", [driver / "vgpudiag.c"])
        resource = out / "vgpudiag.rc.obj"
        run([args.rosbe / "i386/bin/windres.exe", "-I" + str(root / "sdk/include"),
             "-I" + str(root / "sdk/include/psdk"), "-I" + str(root / "sdk/include/reactos"),
             "-I" + str(build / "sdk/include"), "-I" + str(build / "sdk/include/reactos"),
             "-D", "__REACTOS__", "-i", driver / "vgpudiag.rc", "-O", "coff", "-o", resource], out)
        rsp = out / "driver-objects.rsp"
        rsp.write_text(" ".join(p.as_posix() for p in driver_objects + [resource, archive]))
        link = shlex.split((link_dir / "CMakeFiles/sacdrv.dir/link.txt").read_text().splitlines()[0].replace("\\", "/"))
        link[link.index("-o") + 1] = (out / "vgpudiag.sys").as_posix()
        link = ["@" + rsp.as_posix() if item == "@CMakeFiles/sacdrv.dir/objects1.rsp" else item for item in link]
        run(link, link_dir)
        run([build / "host-tools/bin/rsym.exe", "-s", root,
             out / "vgpudiag.sys", out / "vgpudiag.sys"], out)
        test = root / "modules/rostests/tests/vgputest/vgputest.c"
        run([compiler, "-O2", "-Wall", "-Wextra", "-Werror", "-I" + str(root / "sdk/include/reactos"),
             test, "-o", out / "vgputest.exe"], out)
        shutil.copyfile(driver / "vgpudiag.inf", out / "vgpudiag.inf")
        ring_test = root / "modules/rostests/tests/virtioringtest/virtioringtest.c"
        ring_objects = compile_group(library_dir, "virtio", [ring_test])
        run([compiler] + ring_objects + ["-o", out / "virtioringtest.exe", "-lntdll"], out)
    sources += [test, driver / "vgpudiag.rc", driver / "vgpudiag.inf",
                root / "sdk/include/reactos/drivers/vgpu_diag.h"]
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    manifest = {"source_root": str(root), "generated_build": str(build),
                "generated_build_source": cache_source,
                "head": subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip(),
                "sources": {p.relative_to(root).as_posix(): digest(p) for p in sources},
                "driver_sha256": digest(out / "vgpudiag.sys"),
                "full_os_build": False}
    (out / "build-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({"driver": str(out / "vgpudiag.sys"),
                      "manifest": str(out / "build-manifest.json"), "source_root": str(root)}, indent=2))


if __name__ == "__main__":
    main()
