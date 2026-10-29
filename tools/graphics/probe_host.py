# SPDX-License-Identifier: GPL-2.0-or-later
"""Inspect Windows Vulkan and optionally initialize diskless, paused QEMU GPUs.

Exit 0: requested probes passed; 1: inspection/control failure; 2: Venus startup
failed. A successful startup is not evidence of a working ReactOS Vulkan stack.
"""
import argparse
import ctypes as c
from datetime import datetime, timezone
import json
from pathlib import Path
import platform
import socket
import subprocess
import sys


def command(args, timeout=15):
    try:
        proc = subprocess.run(args, capture_output=True, text=True, timeout=timeout)
        return {"exit_code": proc.returncode, "stdout": proc.stdout,
                "stderr": proc.stderr}
    except (OSError, subprocess.TimeoutExpired) as error:
        return {"exit_code": None, "error": str(error)}


def version(value):
    return f"{(value >> 22) & 127}.{(value >> 12) & 1023}.{value & 4095}"


def inspect_vulkan():
    if sys.platform != "win32":
        return {"status": "not_run", "reason": "This loader probe targets Windows"}
    try:
        vk = c.WinDLL("vulkan-1.dll")
    except OSError as error:
        return {"status": "unavailable", "error": str(error)}

    class Application(c.Structure):
        _fields_ = [("sType", c.c_uint32), ("pNext", c.c_void_p),
                    ("name", c.c_char_p), ("appVersion", c.c_uint32),
                    ("engine", c.c_char_p), ("engineVersion", c.c_uint32),
                    ("apiVersion", c.c_uint32)]

    class CreateInfo(c.Structure):
        _fields_ = [("sType", c.c_uint32), ("pNext", c.c_void_p),
                    ("flags", c.c_uint32), ("app", c.POINTER(Application)),
                    ("layerCount", c.c_uint32), ("layers", c.POINTER(c.c_char_p)),
                    ("extensionCount", c.c_uint32),
                    ("extensions", c.POINTER(c.c_char_p))]

    class Extension(c.Structure):
        _fields_ = [("name", c.c_char * 256), ("specVersion", c.c_uint32)]

    vk.vkCreateInstance.argtypes = [c.POINTER(CreateInfo), c.c_void_p, c.POINTER(c.c_void_p)]
    vk.vkCreateInstance.restype = c.c_int32
    vk.vkDestroyInstance.argtypes = [c.c_void_p, c.c_void_p]
    vk.vkDestroyInstance.restype = None
    vk.vkEnumeratePhysicalDevices.argtypes = [c.c_void_p, c.POINTER(c.c_uint32),
                                             c.POINTER(c.c_void_p)]
    vk.vkEnumeratePhysicalDevices.restype = c.c_int32
    vk.vkGetPhysicalDeviceProperties.argtypes = [c.c_void_p, c.c_void_p]
    vk.vkGetPhysicalDeviceProperties.restype = None
    vk.vkEnumerateDeviceExtensionProperties.argtypes = [c.c_void_p, c.c_char_p,
                                                       c.POINTER(c.c_uint32),
                                                       c.POINTER(Extension)]
    vk.vkEnumerateDeviceExtensionProperties.restype = c.c_int32
    instance = c.c_void_p()
    result = {"status": "error", "devices": []}
    try:
        loader_version = c.c_uint32(1 << 22)
        enum_version = getattr(vk, "vkEnumerateInstanceVersion", None)
        if enum_version:
            enum_version.argtypes = [c.POINTER(c.c_uint32)]
            enum_version.restype = c.c_int32
            if enum_version(c.byref(loader_version)) != 0:
                raise RuntimeError("vkEnumerateInstanceVersion failed")
        result["loader_api"] = version(loader_version.value)
        # No layers/extensions and API 1.0: inspect without creating a device,
        # allocating GPU memory, or submitting commands.
        app = Application(0, None, b"ReactOS graphics preflight", 1, None, 0, 1 << 22)
        info = CreateInfo(1, None, 0, c.pointer(app), 0, None, 0, None)
        status = vk.vkCreateInstance(c.byref(info), None, c.byref(instance))
        if status != 0:
            raise RuntimeError(f"vkCreateInstance returned {status}")
        count = c.c_uint32()
        if vk.vkEnumeratePhysicalDevices(instance, c.byref(count), None) != 0:
            raise RuntimeError("Physical-device count query failed")
        if not 0 < count.value <= 64:
            raise RuntimeError(f"Unexpected physical-device count: {count.value}")
        devices = (c.c_void_p * count.value)()
        status = vk.vkEnumeratePhysicalDevices(instance, c.byref(count), devices)
        if status != 0:
            raise RuntimeError(f"Physical-device enumeration returned {status}")
        for device in devices[:count.value]:
            # VkPhysicalDeviceProperties has a stable scalar/name prefix. Use
            # aligned storage larger than its complete core 1.0 structure;
            # limits and sparse properties are deliberately not interpreted.
            storage = (c.c_uint64 * 512)()
            vk.vkGetPhysicalDeviceProperties(device, c.byref(storage))
            raw = bytes(storage)
            words = (c.c_uint32 * 5).from_buffer_copy(raw[:20])
            entry = {"name": raw[20:276].split(b"\0")[0].decode(errors="replace"),
                     "api": version(words[0]), "vendor_id": f"0x{words[2]:04x}",
                     "device_id": f"0x{words[3]:04x}", "type": words[4]}
            ext_count = c.c_uint32()
            status = vk.vkEnumerateDeviceExtensionProperties(device, None,
                                                              c.byref(ext_count), None)
            if status != 0 or ext_count.value > 4096:
                raise RuntimeError("Device-extension count query failed")
            extensions = (Extension * ext_count.value)()
            status = vk.vkEnumerateDeviceExtensionProperties(device, None,
                                                              c.byref(ext_count), extensions)
            if status != 0:
                raise RuntimeError(f"Device-extension enumeration returned {status}")
            names = {item.name.decode() for item in extensions[:ext_count.value]}
            entry["external_memory_extensions"] = {
                name: name in names for name in
                ("VK_KHR_external_memory_fd", "VK_KHR_external_memory_win32",
                 "VK_EXT_external_memory_dma_buf", "VK_EXT_image_drm_format_modifier")}
            result["devices"].append(entry)
        result["status"] = "passed"
    except (RuntimeError, OSError) as error:
        result["error"] = str(error)
    finally:
        if instance.value:
            vk.vkDestroyInstance(instance, None)
    return result


def smoke_qemu(qemu, venus):
    # No disk, NIC or serial port. -S prevents guest code execution. QEMU
    # connects to our temporary loopback QMP listener; requests are sequential.
    # Always tear down the child, including on monitor/startup failures.
    device = ("virtio-gpu-gl-pci,hostmem=256M,blob=true,venus=true" if venus
              else "virtio-gpu-pci")
    result = {"status": "failed", "guest_vulkan_validated": False,
              "guest_cpu_executed": False, "renderer_runtime_validated": False}
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(20)
        args = [str(qemu), "-nodefaults", "-machine", "q35", "-accel", "tcg",
                "-m", "512", "-S", "-vga", "none", "-device", device,
                "-display", "egl-headless" if venus else "none", "-qmp",
                f"tcp:127.0.0.1:{listener.getsockname()[1]}"]
        result["argv"] = args
        try:
            proc = subprocess.Popen(args, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, text=True,
                                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        except OSError as error:
            result["error"] = str(error)
            return result
        messages = []
        try:
            connection, _ = listener.accept()
            with connection:
                connection.settimeout(10)
                reader = connection.makefile("rb")
                with reader:
                    def receive():
                        line = reader.readline(1048576)
                        if not line:
                            raise RuntimeError("QMP connection closed")
                        message = json.loads(line)
                        messages.append(message)
                        return message
                    if "QMP" not in receive():
                        raise RuntimeError("Missing QMP greeting")
                    for name in ("qmp_capabilities", "query-status", "query-pci", "quit"):
                        connection.sendall((json.dumps({"execute": name, "id": name}) + "\n").encode())
                        for _ in range(100):
                            message = receive()
                            if message.get("id") == name:
                                if "error" in message:
                                    raise RuntimeError(str(message["error"]))
                                break
                        else:
                            raise RuntimeError("Too many unsolicited QMP messages")
            proc.wait(timeout=10)
        except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
            result["error"] = str(error)
        finally:
            if proc.poll() is None:
                proc.kill()
            try:
                out, err = proc.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                out, err = proc.communicate()
            result.update(exit_code=proc.returncode, stdout=out, stderr=err, qmp=messages)
    replies = {m.get("id"): m for m in messages if "id" in m}
    state = replies.get("query-status", {}).get("return", {})
    pci = replies.get("query-pci", {}).get("return", [])
    gpu_found = any(d.get("id", {}).get("vendor") == 0x1af4 and
                    d.get("id", {}).get("device") == 0x1050
                    for bus in pci for d in bus.get("devices", []))
    result["virtio_gpu_enumerated"] = gpu_found
    if ("error" not in result and proc.returncode == 0 and
            state.get("running") is False and gpu_found):
        result["status"] = "passed"
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", type=Path,
                        default=Path(r"C:\Program Files\qemu\qemu-system-x86_64.exe"))
    parser.add_argument("--smoke", action="store_true", help="Initialize diskless paused GPU devices")
    parser.add_argument("--output", type=Path,
                        default=Path(__file__).resolve().parent / "artifacts" / "host-report.json")
    args = parser.parse_args()
    report = {"schema_version": 1, "timestamp_utc": datetime.now(timezone.utc).isoformat(),
              "host": platform.system(), "host_vulkan": inspect_vulkan(),
              "guest_vulkan_validated": False}
    for key, suffix in (("qemu_version", ["--version"]),
                        ("qemu_devices", ["-device", "help"]),
                        ("venus_properties", ["-device", "virtio-gpu-gl-pci,help"])):
        report[key] = command([str(args.qemu)] + suffix)
    if args.smoke:
        report["control_2d"] = smoke_qemu(args.qemu, False)
        report["venus_initialization"] = smoke_qemu(args.qemu, True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"report": str(args.output), "host_vulkan": report["host_vulkan"],
                      "control_2d": report.get("control_2d", {}).get("status", "not_run"),
                      "venus_initialization": report.get("venus_initialization", {}).get("status", "not_run"),
                      "guest_vulkan_validated": False}, indent=2))
    if any(report[key].get("exit_code") != 0
           for key in ("qemu_version", "qemu_devices", "venus_properties")):
        return 1
    if args.smoke and report["control_2d"]["status"] != "passed":
        return 1
    if args.smoke and report["venus_initialization"]["status"] != "passed":
        return 2
    return 0 if report["host_vulkan"]["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
