@echo off
rem Boot the overlay containing the tested ReactOS Chrome compatibility fixes.
start "" "C:\Program Files\qemu\qemu-system-x86_64.exe" -accel whpx -m 2048 -smp 1 -drive "file=C:\ReactOS\vm\chrome-render.qcow2,format=qcow2" -usb -device usb-tablet -netdev user,id=n1,hostfwd=tcp::17890-:17890 -device e1000,netdev=n1 -serial file:C:\ReactOS\vm\debug.log -serial tcp:127.0.0.1:5555,server,nowait -monitor telnet:127.0.0.1:4444,server,nowait
