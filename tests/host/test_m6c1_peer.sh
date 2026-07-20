#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)

PYTHONDONTWRITEBYTECODE=1 python3 - "$root" <<'PY'
import importlib.util
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
spec = importlib.util.spec_from_file_location("m5_peer", root / "scripts/m5-peer.py")
if spec is None or spec.loader is None:
    raise SystemExit("cannot load m5-peer.py")
peer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(peer)
peer.socket.AF_PACKET = getattr(peer.socket, "AF_PACKET", 17)
peer.socket.SOCK_RAW = getattr(peer.socket, "SOCK_RAW", 3)
peer.socket.PACKET_OUTGOING = getattr(peer.socket, "PACKET_OUTGOING", 4)


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
            raise TimeoutError("fake frame stream exhausted")
        return self.frames.pop(0), ("tap-test", 0, 0, 0)

    def send(self, frame):
        self.sent.append(frame)
        return len(frame)


def common_frames(tcp_frames):
    arp_request = peer.encode_arp_request(
        peer.GUEST_MAC, peer.GUEST_IP, peer.HOST_IP, b"\0" * 6
    )
    guest_arp_reply = peer.encode_arp_reply(
        peer.GUEST_MAC, peer.HOST_MAC, peer.GUEST_IP, peer.HOST_IP
    )
    guest_echo_request = peer.encode_icmp_echo(
        peer.GUEST_MAC, b"\x02" * 6, peer.GUEST_IP, peer.HOST_IP,
        peer.TEST_IDENTIFIER, peer.TEST_SEQUENCE, b"quard-star-m6c1",
        peer.ICMP_ECHO_REQUEST
    )
    guest_echo_reply = peer.encode_icmp_echo(
        peer.GUEST_MAC, b"\x02" * 6, peer.GUEST_IP, peer.HOST_IP,
        peer.HOST_IDENTIFIER, peer.HOST_SEQUENCE, b"host-to-guest-m5",
        peer.ICMP_ECHO_REPLY
    )
    return [arp_request, guest_arp_reply, guest_echo_request,
            guest_echo_reply, *tcp_frames]


def tcp_flow(overrides=None):
    src_port = 12345
    guest_isn = 100
    host_isn = 9000
    payload = b"quard-star-m6c1"
    guest_data = guest_isn + 1
    guest_data_end = guest_data + len(payload)
    host_data = host_isn + 1
    host_data_end = host_data + len(payload)

    def segment(seq, ack, flags, body=b""):
        return peer.encode_tcp(
            peer.GUEST_MAC, peer.HOST_MAC, peer.GUEST_IP, peer.HOST_IP,
            src_port, peer.HOST_TCP_PORT, seq, ack, flags, body
        )

    frames = [
        segment(guest_isn, 0, peer.TCP_FLAG_SYN),
        segment(guest_data, host_data, peer.TCP_FLAG_ACK),
        segment(guest_data, host_data,
                peer.TCP_FLAG_PSH | peer.TCP_FLAG_ACK, payload),
        segment(guest_data, host_data,
                peer.TCP_FLAG_PSH | peer.TCP_FLAG_ACK, payload),
        segment(guest_data_end, host_data_end, peer.TCP_FLAG_ACK),
        segment(guest_data_end, host_data_end,
                peer.TCP_FLAG_FIN | peer.TCP_FLAG_ACK),
        segment(guest_data_end + 1, host_data_end + 1, peer.TCP_FLAG_ACK),
    ]
    for index, value in (overrides or {}).items():
        frames[index] = value
    return frames


def run(frames):
    fake = FakeSocket(common_frames(frames))
    original_socket = peer.socket.socket
    peer.socket.socket = lambda *args, **kwargs: fake
    try:
        return peer.run_peer("tap-test", 0, 1.0, None, None,
                             require_tcp=True)
    finally:
        peer.socket.socket = original_socket


assert run(tcp_flow()) == 0

# Keep the malformed frames in the same state slot as the valid flow so each
# case proves that the peer validates the guest sequence and acknowledgement.
bad_cases = {
    "handshake ACK": tcp_flow({1: peer.encode_tcp(
        peer.GUEST_MAC, peer.HOST_MAC, peer.GUEST_IP, peer.HOST_IP,
        12345, peer.HOST_TCP_PORT, 101, 9000, peer.TCP_FLAG_ACK
    )}),
    "first data ACK": tcp_flow({2: peer.encode_tcp(
        peer.GUEST_MAC, peer.HOST_MAC, peer.GUEST_IP, peer.HOST_IP,
        12345, peer.HOST_TCP_PORT, 101, 9000,
        peer.TCP_FLAG_PSH | peer.TCP_FLAG_ACK, b"quard-star-m6c1"
    )}),
    "retransmission ACK": tcp_flow({3: peer.encode_tcp(
        peer.GUEST_MAC, peer.HOST_MAC, peer.GUEST_IP, peer.HOST_IP,
        12345, peer.HOST_TCP_PORT, 101, 9000,
        peer.TCP_FLAG_PSH | peer.TCP_FLAG_ACK, b"quard-star-m6c1"
    )}),
    "FIN ACK": tcp_flow({5: peer.encode_tcp(
        peer.GUEST_MAC, peer.HOST_MAC, peer.GUEST_IP, peer.HOST_IP,
        12345, peer.HOST_TCP_PORT, 116, 9001,
        peer.TCP_FLAG_FIN | peer.TCP_FLAG_ACK
    )}),
    "final ACK": tcp_flow({6: peer.encode_tcp(
        peer.GUEST_MAC, peer.HOST_MAC, peer.GUEST_IP, peer.HOST_IP,
        12345, peer.HOST_TCP_PORT, 117, 9016, peer.TCP_FLAG_ACK
    )}),
}
for name, frames in bad_cases.items():
    try:
        run(frames)
    except (ValueError, TimeoutError):
        continue
    raise AssertionError(f"peer accepted malformed {name}")
PY

echo 'PASS: M6C1 peer TCP sequence and ACK validation'
