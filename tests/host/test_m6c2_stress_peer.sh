#!/usr/bin/env bash
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)

PYTHONDONTWRITEBYTECODE=1 python3 - "$root" <<'PY'
import importlib.util
import inspect
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

assert peer.STRESS_PARALLEL == 8
assert peer.STRESS_RECONNECTS == 100
assert "require_tcp_server_stress" in inspect.signature(peer.run_peer).parameters


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
        frame = self.frames.pop(0)
        if frame is None:
            raise peer.socket.timeout("injected timeout")
        return frame, ("tap-test", 0, 0, 0)

    def send(self, frame):
        self.sent.append(frame)
        return len(frame)


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
        segment(guest_isn, 0, peer.TCP_FLAG_SYN),
        segment(guest_data, host_data, peer.TCP_FLAG_ACK),
        segment(guest_data, host_data,
                peer.TCP_FLAG_PSH | peer.TCP_FLAG_ACK, payload),
        segment(guest_data, host_data,
                peer.TCP_FLAG_PSH | peer.TCP_FLAG_ACK, payload),
        segment(guest_data, host_data,
                peer.TCP_FLAG_PSH | peer.TCP_FLAG_ACK, payload),
        segment(guest_end, host_end, peer.TCP_FLAG_ACK),
        segment(guest_end, host_end, peer.TCP_FLAG_FIN | peer.TCP_FLAG_ACK),
        segment(guest_end, host_end, peer.TCP_FLAG_FIN | peer.TCP_FLAG_ACK),
        segment(guest_end + 1, host_end + 1, peer.TCP_FLAG_ACK),
    ]


def common_frames(tcp_frames):
    return [
        peer.encode_arp_request(peer.GUEST_MAC, peer.GUEST_IP,
                                peer.HOST_IP, b"\0" * 6),
        peer.encode_arp_reply(peer.GUEST_MAC, peer.HOST_MAC,
                              peer.GUEST_IP, peer.HOST_IP),
        peer.encode_icmp_echo(
            peer.GUEST_MAC, peer.HOST_MAC, peer.GUEST_IP, peer.HOST_IP,
            peer.TEST_IDENTIFIER, peer.TEST_SEQUENCE, b"quard-star-m6c2",
            peer.ICMP_ECHO_REQUEST),
        peer.encode_icmp_echo(
            peer.GUEST_MAC, peer.HOST_MAC, peer.GUEST_IP, peer.HOST_IP,
            peer.HOST_IDENTIFIER, peer.HOST_SEQUENCE, b"host-to-guest-m5",
            peer.ICMP_ECHO_REPLY),
        peer.encode_udp(
            peer.GUEST_MAC, peer.HOST_MAC, peer.GUEST_IP, peer.HOST_IP,
            peer.GUEST_UDP_PORT, peer.HOST_UDP_PORT, b"quard-star-m6b"),
        *tcp_frames,
    ]


def stress_parts(index):
    host_port = peer.HOST_TCP_SERVER_PORT + index
    host_isn = 12000 + index * 32
    guest_isn = 200 + index * 32
    payload = f"m6c2-{index:03d}".encode()
    host_data = host_isn + 1
    host_end = host_data + len(payload)
    guest_data = guest_isn + 1
    guest_end = guest_data + len(payload)

    def segment(seq, ack, flags, body=b""):
        return peer.encode_tcp(
            peer.GUEST_MAC, peer.HOST_MAC, peer.GUEST_IP, peer.HOST_IP,
            peer.GUEST_TCP_SERVER_PORT, host_port, seq, ack, flags, body)

    exchange = [
        segment(guest_isn, host_data, peer.TCP_FLAG_SYN | peer.TCP_FLAG_ACK),
        segment(guest_data, host_end, peer.TCP_FLAG_ACK),
        segment(guest_data, host_end,
                peer.TCP_FLAG_PSH | peer.TCP_FLAG_ACK, payload),
    ]
    close = [
        segment(guest_end, host_end + 1, peer.TCP_FLAG_ACK),
        segment(guest_end, host_end + 1,
                peer.TCP_FLAG_FIN | peer.TCP_FLAG_ACK),
        segment(guest_end + 1, host_end + 1, peer.TCP_FLAG_ACK),
    ]
    return exchange, close


def stress_flow(connection_count=108, duplicate_first_fin=False,
                duplicate_first_syn_ack=False,
                duplicate_first_echo=False,
                duplicate_first_data_ack=False,
                duplicate_first_echo_during_close=False,
                duplicate_first_final_ack=False):
    exchanges = []
    closes = []
    parallel = min(connection_count, peer.STRESS_PARALLEL)
    for index in range(parallel):
        exchange, close = stress_parts(index)
        exchanges.append(exchange[0])
        if index == 0 and duplicate_first_syn_ack:
            exchanges.append(exchange[0])
        exchanges.append(exchange[1])
        if index == 0 and duplicate_first_data_ack:
            exchanges.append(exchange[1])
        exchanges.append(exchange[2])
        if index == 0 and duplicate_first_echo:
            exchanges.append(exchange[2])
        if index == 0 and duplicate_first_echo_during_close:
            closes.append(exchange[2])
        closes.extend(close[:2])
        if index == 0 and duplicate_first_fin:
            closes.append(close[1])
        if index == 0 and duplicate_first_final_ack:
            closes.append(close[2])
    frames = exchanges + closes
    for index in range(peer.STRESS_PARALLEL, connection_count):
        exchange, close = stress_parts(index)
        frames.extend(exchange + close[:2])
    return frames


def run(stress_frames, drop_first_syn=False):
    frames = active_flow()
    if drop_first_syn:
        frames.append(None)
    fake = FakeSocket(common_frames(frames + stress_frames))
    original_socket = peer.socket.socket
    peer.socket.socket = lambda *args, **kwargs: fake
    try:
        with tempfile.TemporaryDirectory() as tmp:
            stats_path = pathlib.Path(tmp) / "stats.json"
            result = peer.run_peer(
                "tap-test", 0, 2.0, None, str(stats_path), True, True,
                False, True)
            return result, fake.sent, json.loads(stats_path.read_text())
    finally:
        peer.socket.socket = original_socket


result, sent, stats = run(stress_flow())
assert result == 0
assert stats["tcp_server_stress_handshakes"] == 108
assert stats["tcp_server_stress_echo"] == 108
assert stats["tcp_server_stress_parallel_peak"] == 8
assert stats["tcp_server_stress_reconnects"] == 100
assert stats["tcp_server_stress_fin"] == 108
assert stats["tcp_server_stress_outstanding"] == 0

server_syns = [
    segment for frame in sent
    if (segment := peer.decode_tcp(frame)) is not None and
    segment["dst_port"] == peer.GUEST_TCP_SERVER_PORT and
    segment["flags"] == peer.TCP_FLAG_SYN
]
assert len(server_syns) == 108
assert len({segment["src_port"] for segment in server_syns}) == 108

result, sent, stats = run(stress_flow(), drop_first_syn=True)
assert result == 0
server_syns = [
    segment for frame in sent
    if (segment := peer.decode_tcp(frame)) is not None and
    segment["dst_port"] == peer.GUEST_TCP_SERVER_PORT and
    segment["flags"] == peer.TCP_FLAG_SYN
]
assert len(server_syns) == 109
assert server_syns[0] == server_syns[1]

result, _, stats = run(stress_flow(duplicate_first_fin=True))
assert result == 0
assert stats["tcp_server_stress_fin"] == 108

result, _, stats = run(stress_flow(duplicate_first_syn_ack=True))
assert result == 0
assert stats["tcp_server_stress_handshakes"] == 108

result, _, stats = run(stress_flow(duplicate_first_echo=True))
assert result == 0
assert stats["tcp_server_stress_echo"] == 108

result, _, stats = run(stress_flow(duplicate_first_data_ack=True))
assert result == 0
assert stats["tcp_server_stress_echo"] == 108

result, sent, stats = run(
    stress_flow(duplicate_first_echo_during_close=True))
assert result == 0
assert stats["tcp_server_stress_fin"] == 108
first_fins = [
    segment for frame in sent
    if (segment := peer.decode_tcp(frame)) is not None and
    segment["src_port"] == peer.HOST_TCP_SERVER_PORT and
    segment["flags"] == (peer.TCP_FLAG_FIN | peer.TCP_FLAG_ACK)
]
assert len(first_fins) >= 2
assert first_fins[0] == first_fins[1]

result, _, stats = run(stress_flow(duplicate_first_final_ack=True))
assert result == 0
assert stats["tcp_server_stress_fin"] == 108


def rejected(frames):
    try:
        run(frames)
    except (TimeoutError, ValueError):
        return
    raise AssertionError("peer accepted malformed stress flow")


wrong_sequence = stress_flow()
segment = peer.decode_tcp(wrong_sequence[1])
wrong_sequence[1] = peer.encode_tcp(
    peer.GUEST_MAC, peer.HOST_MAC, peer.GUEST_IP, peer.HOST_IP,
    segment["src_port"], segment["dst_port"], segment["seq"] + 1,
    segment["ack"], segment["flags"], segment["payload"])
rejected(wrong_sequence)

wrong_payload = stress_flow()
segment = peer.decode_tcp(wrong_payload[2])
wrong_payload[2] = peer.encode_tcp(
    peer.GUEST_MAC, peer.HOST_MAC, peer.GUEST_IP, peer.HOST_IP,
    segment["src_port"], segment["dst_port"], segment["seq"],
    segment["ack"], segment["flags"], b"m6c2-999")
rejected(wrong_payload)

wrong_echo_ack = stress_flow()
segment = peer.decode_tcp(wrong_echo_ack[2])
wrong_echo_ack[2] = peer.encode_tcp(
    peer.GUEST_MAC, peer.HOST_MAC, peer.GUEST_IP, peer.HOST_IP,
    segment["src_port"], segment["dst_port"], segment["seq"] + 1,
    segment["ack"], peer.TCP_FLAG_ACK)
rejected(wrong_echo_ack)

wrong_closing_echo = stress_flow(duplicate_first_echo_during_close=True)
segment = peer.decode_tcp(wrong_closing_echo[peer.STRESS_PARALLEL * 3])
wrong_closing_echo[peer.STRESS_PARALLEL * 3] = peer.encode_tcp(
    peer.GUEST_MAC, peer.HOST_MAC, peer.GUEST_IP, peer.HOST_IP,
    segment["src_port"], segment["dst_port"], segment["seq"],
    segment["ack"] + 1, segment["flags"], segment["payload"])
rejected(wrong_closing_echo)

wrong_complete_ack = stress_flow(duplicate_first_final_ack=True)
complete_index = peer.STRESS_PARALLEL * 3 + 2
segment = peer.decode_tcp(wrong_complete_ack[complete_index])
wrong_complete_ack[complete_index] = peer.encode_tcp(
    peer.GUEST_MAC, peer.HOST_MAC, peer.GUEST_IP, peer.HOST_IP,
    segment["src_port"], segment["dst_port"], segment["seq"] + 1,
    segment["ack"], segment["flags"])
rejected(wrong_complete_ack)

cross_tuple = stress_flow()
segment = peer.decode_tcp(cross_tuple[4])
cross_tuple[4] = peer.encode_tcp(
    peer.GUEST_MAC, peer.HOST_MAC, peer.GUEST_IP, peer.HOST_IP,
    segment["src_port"], peer.HOST_TCP_SERVER_PORT,
    segment["seq"], segment["ack"], segment["flags"], segment["payload"])
rejected(cross_tuple)

rejected(stress_flow(107))
PY

echo 'PASS: M6C2 peer validates eight live connections and 100 reconnects'
