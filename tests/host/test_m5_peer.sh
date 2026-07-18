#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)

PYTHONDONTWRITEBYTECODE=1 python3 - "$root" <<'PY'
import importlib.util
import pathlib
import struct
import sys

root = pathlib.Path(sys.argv[1])
spec = importlib.util.spec_from_file_location("m5_peer", root / "scripts/m5-peer.py")
if spec is None or spec.loader is None:
    raise SystemExit("cannot load m5-peer.py")
peer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(peer)

arp_request = peer.encode_arp_request(
    peer.GUEST_MAC, peer.GUEST_IP, peer.HOST_IP, peer.GUEST_MAC
)
assert len(arp_request) == peer.ETHERNET_MIN_FRAME
assert peer.decode_arp_request(arp_request) == (
    peer.GUEST_MAC, peer.GUEST_IP, peer.HOST_IP
)

arp_reply = peer.encode_arp_reply(
    peer.HOST_MAC, peer.GUEST_MAC, peer.HOST_IP, peer.GUEST_IP
)
assert peer.decode_arp_reply(arp_reply) == (
    peer.HOST_MAC, peer.GUEST_IP, peer.GUEST_MAC
)

payload = b"quard-star-m5"
request = peer.encode_icmp_echo(
    peer.GUEST_MAC, peer.HOST_MAC, peer.GUEST_IP, peer.HOST_IP,
    peer.TEST_IDENTIFIER, peer.TEST_SEQUENCE, payload, peer.ICMP_ECHO_REQUEST
)
decoded = peer.decode_icmp_echo(request)
assert decoded is not None
assert decoded["src_mac"] == peer.GUEST_MAC
assert decoded["dst_mac"] == peer.HOST_MAC
assert decoded["src_ip"] == peer.GUEST_IP
assert decoded["dst_ip"] == peer.HOST_IP
assert decoded["type"] == peer.ICMP_ECHO_REQUEST
assert decoded["identifier"] == peer.TEST_IDENTIFIER
assert decoded["sequence"] == peer.TEST_SEQUENCE
assert decoded["payload"] == payload

reply = peer.encode_icmp_echo(
    peer.HOST_MAC, peer.GUEST_MAC, peer.HOST_IP, peer.GUEST_IP,
    peer.TEST_IDENTIFIER, peer.TEST_SEQUENCE, payload, peer.ICMP_ECHO_REPLY
)
assert peer.decode_icmp_echo(reply)["type"] == peer.ICMP_ECHO_REPLY

bad = bytearray(request)
bad[peer.ETHERNET_HEADER_SIZE + 20 + 8] ^= 1
assert peer.decode_icmp_echo(bytes(bad)) is None


class FakeSocket:
    def __init__(self, frames):
        self.frames = list(frames)
        self.sent = []

    def __enter__(self):
        return self

    def __exit__(self, *args):
        return False

    def bind(self, address):
        assert address == ("tap-test", 0)

    def settimeout(self, timeout):
        assert timeout > 0

    def recvfrom(self, capacity):
        assert capacity == peer.ETHERNET_MAX_FRAME
        if not self.frames:
            raise AssertionError("peer consumed the fake frame stream too early")
        return self.frames.pop(0), ("tap-test", 0, 0, 0)

    def send(self, frame):
        self.sent.append(frame)
        return len(frame)


raw_payload = bytes((0 ^ offset ^ 0x5A) & 0xFF
                    for offset in range(peer.M4_PAYLOAD_SIZE))
raw_body = struct.pack("!IH", 0, len(raw_payload)) + raw_payload
raw_body += struct.pack("!I", peer.payload_checksum(raw_payload))
raw_request = peer._ether(b"\xff" * 6, peer.GUEST_MAC,
                          peer.M4_ETHERTYPE, raw_body)
arp_request = peer.encode_arp_request(
    peer.GUEST_MAC, peer.GUEST_IP, peer.HOST_IP, b"\0" * 6
)
guest_arp_reply = peer.encode_arp_reply(
    peer.GUEST_MAC, peer.HOST_MAC, peer.GUEST_IP, peer.HOST_IP
)
guest_echo_request = peer.encode_icmp_echo(
    peer.GUEST_MAC, b"\x02" * 6, peer.GUEST_IP, peer.HOST_IP,
    peer.TEST_IDENTIFIER, peer.TEST_SEQUENCE, b"quard-star-m5",
    peer.ICMP_ECHO_REQUEST
)
guest_echo_reply = peer.encode_icmp_echo(
    peer.GUEST_MAC, b"\x02" * 6, peer.GUEST_IP, peer.HOST_IP,
    peer.HOST_IDENTIFIER, peer.HOST_SEQUENCE, b"host-to-guest-m5",
    peer.ICMP_ECHO_REPLY
)
fake = FakeSocket([raw_request, arp_request, guest_arp_reply,
                   guest_echo_request, guest_echo_reply])
original_socket = peer.socket.socket
peer.socket.socket = lambda *args, **kwargs: fake
try:
    assert peer.run_peer("tap-test", 1, 1.0, None, None) == 0
finally:
    peer.socket.socket = original_socket
assert len(fake.sent) == 5
PY

echo 'PASS: M5 TAP peer helpers'
