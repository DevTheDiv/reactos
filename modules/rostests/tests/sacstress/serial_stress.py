"""Byte-for-byte serial test for sacstress --echo-server; Python stdlib only."""
import argparse
import hashlib
import json
import random
import socket
import time


def drain(sock, quiet=0.3):
    data = bytearray()
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        sock.settimeout(min(quiet, max(0.001, deadline - time.monotonic())))
        try:
            chunk = sock.recv(4096)
            if not chunk:
                raise RuntimeError("Serial connection closed")
            data.extend(chunk)
        except socket.timeout:
            return bytes(data)
    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5560)
    parser.add_argument("--bytes", type=int, default=32768)
    parser.add_argument("--output", help="Write a JSON result file")
    args = parser.parse_args()
    if not 1 <= args.bytes <= 1048576:
        parser.error("--bytes must be between 1 and 1048576")
    rng = random.Random(0x5AC)
    payload = bytes(rng.choice(b"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz")
                    for _ in range(args.bytes))
    started = time.monotonic()
    result = {"bytes_requested": args.bytes, "passed": False}
    try:
        with socket.create_connection((args.host, args.port), 5) as sock:
            drain(sock)
            for _ in range(10):
                sock.sendall(b"\x1b\t")
                banner = drain(sock)
                if b"SACSTRESS" in banner:
                    break
            else:
                raise RuntimeError("SACSTRESS channel not found; start the echo server first")
            sock.sendall(b" ")  # Acknowledge the channel-switch banner.
            drain(sock)
            reply = bytearray()
            sizes = (1, 7, 31, 127, 255, 511)
            offset = block = 0
            while offset < len(payload):
                chunk = payload[offset:offset + sizes[block % len(sizes)]]
                sock.sendall(chunk)  # One burst, no per-character throttling.
                deadline = time.monotonic() + 10
                echoed = bytearray()
                while len(echoed) < len(chunk):
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        raise TimeoutError(f"Echo stalled at byte {offset + len(echoed)}")
                    sock.settimeout(remaining)
                    data = sock.recv(len(chunk) - len(echoed))
                    if not data:
                        raise RuntimeError("Serial connection closed")
                    echoed.extend(data)
                if echoed != chunk:
                    raise AssertionError(f"Corrupt or reordered echo in block {block} at byte {offset}")
                reply.extend(echoed)
                offset += len(chunk)
                block += 1
            result.update(passed=True, bytes_verified=len(reply), bursts=block,
                          sha256=hashlib.sha256(reply).hexdigest())
    except Exception as error:
        result["error"] = str(error)
    result["elapsed_seconds"] = round(time.monotonic() - started, 3)
    text = json.dumps(result, indent=2)
    print(text)
    if args.output:
        with open(args.output, "w", encoding="utf-8") as output:
            output.write(text + "\n")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
