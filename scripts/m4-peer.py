#!/usr/bin/env python3
import argparse
import json
import pathlib
import socket
import struct
import sys
import time

ETHERTYPE = 0x88B5
GUEST_MAC = bytes.fromhex("525400123456")
HOST_MAC = bytes.fromhex("525400123457")
BROADCAST = b"\xff" * 6


def payload_checksum(payload: bytes) -> int:
    return sum(payload) & 0xFFFFFFFF


def decode_request(frame: bytes) -> tuple[int, bytes]:
    if len(frame) < 24:
        raise ValueError("truncated M4 frame")
    destination, source, ethertype = struct.unpack_from("!6s6sH", frame, 0)
    if destination != BROADCAST or source != GUEST_MAC or ethertype != ETHERTYPE:
        raise ValueError("unexpected M4 Ethernet header")
    sequence, length = struct.unpack_from("!IH", frame, 14)
    end = 20 + length
    if end + 4 > len(frame):
        raise ValueError("truncated M4 payload")
    payload = frame[20:end]
    checksum = struct.unpack_from("!I", frame, end)[0]
    if checksum != payload_checksum(payload):
        raise ValueError("bad M4 checksum")
    for offset, value in enumerate(payload):
        if value != ((sequence ^ offset ^ 0x5A) & 0xFF):
            raise ValueError("bad M4 deterministic payload")
    return sequence, payload


def encode_response(sequence: int, payload: bytes) -> bytes:
    body = struct.pack("!IH", sequence, len(payload)) + payload
    body += struct.pack("!I", payload_checksum(payload))
    frame = GUEST_MAC + HOST_MAC + struct.pack("!H", ETHERTYPE) + body
    return frame.ljust(60, b"\0")


def write_stats(path: str | None, frames: int, elapsed: float) -> None:
    if path:
        pathlib.Path(path).write_text(
            json.dumps({"frames": frames, "elapsed_seconds": elapsed}) + "\n",
            encoding="utf-8",
        )


def run_peer(interface: str, count: int, timeout: float,
             ready_file: str | None, stats_file: str | None) -> int:
    started = time.monotonic()
    deadline = started + timeout
    received = 0
    expected_sequence = 0

    with socket.socket(socket.AF_PACKET, socket.SOCK_RAW,
                       socket.htons(ETHERTYPE)) as peer:
        peer.bind((interface, 0))
        if ready_file:
            pathlib.Path(ready_file).write_text("ready\n", encoding="utf-8")

        while received < count:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f"timed out after {received}/{count} frames")
            peer.settimeout(remaining)
            frame, address = peer.recvfrom(ETHERNET_MAX_FRAME)
            if len(address) > 2 and address[2] == socket.PACKET_OUTGOING:
                continue
            sequence, payload = decode_request(frame)
            if sequence != expected_sequence:
                raise ValueError(
                    f"expected sequence {expected_sequence}, got {sequence}"
                )
            peer.send(encode_response(sequence, payload))
            received += 1
            expected_sequence += 1

    write_stats(stats_file, received, time.monotonic() - started)
    return received


ETHERNET_MAX_FRAME = 1514


def main() -> int:
    parser = argparse.ArgumentParser(description="M4 raw Ethernet TAP peer")
    parser.add_argument("interface")
    parser.add_argument("--count", type=int, required=True)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--ready-file")
    parser.add_argument("--stats-file")
    args = parser.parse_args()
    if args.count < 1 or args.timeout <= 0:
        parser.error("--count and --timeout must be positive")

    try:
        run_peer(args.interface, args.count, args.timeout,
                 args.ready_file, args.stats_file)
    except (OSError, TimeoutError, ValueError) as error:
        print(f"m4-peer: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
