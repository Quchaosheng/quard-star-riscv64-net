#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)

PYTHONDONTWRITEBYTECODE=1 python3 - "$root" <<'PY'
import importlib.util
import json
import pathlib
import sys
import tempfile

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
        peer.GUEST_MAC, peer.HOST_MAC, peer.GUEST_IP, peer.HOST_IP,
        peer.TEST_IDENTIFIER, peer.TEST_SEQUENCE, b"quard-star-m6c2",
        peer.ICMP_ECHO_REQUEST
    )
    guest_echo_reply = peer.encode_icmp_echo(
        peer.GUEST_MAC, peer.HOST_MAC, peer.GUEST_IP, peer.HOST_IP,
        peer.HOST_IDENTIFIER, peer.HOST_SEQUENCE, b"host-to-guest-m5",
        peer.ICMP_ECHO_REPLY
    )
    udp = peer.encode_udp(
        peer.GUEST_MAC, peer.HOST_MAC, peer.GUEST_IP, peer.HOST_IP,
        peer.GUEST_UDP_PORT, peer.HOST_UDP_PORT, b"quard-star-m6b"
    )
    return [arp_request, guest_arp_reply, guest_echo_request,
            guest_echo_reply, udp, *tcp_frames]


def active_flow():
    src_port = 12345
    guest_isn = 100
    host_isn = 9000
    payload = b"quard-star-m6c1"
    guest_data = guest_isn + 1
    guest_end = guest_data + len(payload)
    host_data = host_isn + 1
    host_end = host_data + len(payload)

    def segment(seq, ack, flags, body=b""):
        return peer.encode_tcp(
            peer.GUEST_MAC, peer.HOST_MAC, peer.GUEST_IP, peer.HOST_IP,
            src_port, peer.HOST_TCP_PORT, seq, ack, flags, body
        )

    return [
        segment(guest_isn, 0, peer.TCP_FLAG_SYN),
        segment(guest_data, host_data, peer.TCP_FLAG_ACK),
        segment(guest_data, host_data,
                peer.TCP_FLAG_PSH | peer.TCP_FLAG_ACK, payload),
        segment(guest_data, host_data,
                peer.TCP_FLAG_PSH | peer.TCP_FLAG_ACK, payload),
        segment(guest_end, host_end, peer.TCP_FLAG_ACK),
        segment(guest_end, host_end, peer.TCP_FLAG_FIN | peer.TCP_FLAG_ACK),
        segment(guest_end + 1, host_end + 1, peer.TCP_FLAG_ACK),
    ]


def server_flow(overrides=None):
    host_port = 4802
    host_isn = 12000
    guest_isn = 200
    payload = b"quard-star-m6c2"
    host_data = host_isn + 1
    host_end = host_data + len(payload)
    guest_data = guest_isn + 1
    guest_end = guest_data + len(payload)

    def segment(seq, ack, flags, body=b""):
        return peer.encode_tcp(
            peer.GUEST_MAC, peer.HOST_MAC, peer.GUEST_IP, peer.HOST_IP,
            peer.GUEST_TCP_SERVER_PORT, host_port, seq, ack, flags, body
        )

    frames = [
        segment(guest_isn, host_data, peer.TCP_FLAG_SYN | peer.TCP_FLAG_ACK),
        segment(guest_data, host_end, peer.TCP_FLAG_ACK),
        segment(guest_data, host_end,
                peer.TCP_FLAG_PSH | peer.TCP_FLAG_ACK, payload),
        segment(guest_data, host_end,
                peer.TCP_FLAG_PSH | peer.TCP_FLAG_ACK, payload),
        segment(guest_end, host_end, peer.TCP_FLAG_FIN | peer.TCP_FLAG_ACK),
        segment(guest_end + 1, host_end + 1, peer.TCP_FLAG_ACK),
    ]
    for index, value in (overrides or {}).items():
        frames[index] = value
    return frames


def run(frames, require_udp=True, require_tcp=True,
        require_tcp_server=True, stats=False):
    fake = FakeSocket(common_frames(frames))
    original_socket = peer.socket.socket
    peer.socket.socket = lambda *args, **kwargs: fake
    try:
        with tempfile.TemporaryDirectory() as tmp:
            stats_path = str(pathlib.Path(tmp) / "stats.json") if stats else None
            result = peer.run_peer(
                "tap-test", 0, 1.0, None, stats_path, require_udp,
                require_tcp, require_tcp_server
            )
            data = json.loads(pathlib.Path(stats_path).read_text()) \
                if stats_path else None
            return result, fake.sent, data
    finally:
        peer.socket.socket = original_socket


valid = active_flow() + server_flow()
result, sent, stats = run(valid, stats=True)
assert result == 0
assert stats is not None
for key in ("tcp_server_syn", "tcp_server_handshakes", "tcp_server_data",
            "tcp_server_echo", "tcp_server_retransmissions",
            "tcp_server_fin"):
    assert stats[key] == 1, (key, stats)
assert stats["tcp_server_outstanding"] == 0

server_sent = []
for frame in sent:
    segment = peer.decode_tcp(frame)
    if segment is not None and (
            segment["src_port"] == 4802 or
            segment["dst_port"] == peer.GUEST_TCP_SERVER_PORT):
        server_sent.append(segment)
assert len(server_sent) == 5
expected = [
    (4802, peer.GUEST_TCP_SERVER_PORT, 12000, 0, peer.TCP_FLAG_SYN, b""),
    (4802, peer.GUEST_TCP_SERVER_PORT, 12001, 201, peer.TCP_FLAG_ACK, b""),
    (4802, peer.GUEST_TCP_SERVER_PORT, 12001, 201,
     peer.TCP_FLAG_PSH | peer.TCP_FLAG_ACK, b"quard-star-m6c2"),
    (4802, peer.GUEST_TCP_SERVER_PORT, 12016, 216, peer.TCP_FLAG_ACK, b""),
    (4802, peer.GUEST_TCP_SERVER_PORT, 12016, 217,
     peer.TCP_FLAG_FIN | peer.TCP_FLAG_ACK, b""),
]
for actual, wanted in zip(server_sent, expected):
    observed = (actual["src_port"], actual["dst_port"], actual["seq"],
                actual["ack"], actual["flags"], actual["payload"])
    assert observed == wanted, (observed, wanted)


def bad_server(index, seq, ack, flags, payload=b""):
    frame = peer.encode_tcp(
        peer.GUEST_MAC, peer.HOST_MAC, peer.GUEST_IP, peer.HOST_IP,
        peer.GUEST_TCP_SERVER_PORT, 4802, seq, ack, flags, payload
    )
    try:
        run(active_flow() + server_flow({index: frame}))
    except (ValueError, TimeoutError):
        return
    raise AssertionError(f"peer accepted malformed server frame {index}")


bad_server(0, 200, 12000, peer.TCP_FLAG_SYN | peer.TCP_FLAG_ACK)
bad_server(2, 202, 12016, peer.TCP_FLAG_PSH | peer.TCP_FLAG_ACK,
           b"quard-star-m6c2")
bad_server(3, 201, 12016, peer.TCP_FLAG_PSH | peer.TCP_FLAG_ACK,
           b"quard-star-M6C2")
bad_server(3, 216, 12016, peer.TCP_FLAG_FIN | peer.TCP_FLAG_ACK)
bad_server(5, 217, 12016, peer.TCP_FLAG_ACK)

# Earlier modes complete without initiating the passive server flow.
for require_udp, require_tcp in ((False, False), (True, False), (True, True)):
    result, sent, _ = run(active_flow(), require_udp, require_tcp, False)
    assert result == 0
    assert not any(
        (segment := peer.decode_tcp(frame)) is not None and
        segment["dst_port"] == peer.GUEST_TCP_SERVER_PORT
        for frame in sent
    )
PY

echo 'PASS: M6C2 peer validates cumulative active and passive TCP exchange'
