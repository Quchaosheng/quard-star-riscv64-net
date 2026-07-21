#!/usr/bin/env python3
import argparse
import json
import pathlib
import socket
import struct
import sys
import time

ETHERNET_HEADER_SIZE = 14
ETHERNET_MIN_FRAME = 60
ETHERNET_MAX_FRAME = 1514
ETHERTYPE_IPV4 = 0x0800
ETHERTYPE_ARP = 0x0806
M4_ETHERTYPE = 0x88B5
ARP_HW_ETHER = 1
ARP_REQUEST = 1
ARP_REPLY = 2
ICMP_ECHO_REPLY = 0
ICMP_ECHO_REQUEST = 8
IPPROTO_UDP = 17
IPPROTO_TCP = 6
GUEST_UDP_PORT = 4600
HOST_UDP_PORT = 4700
HOST_TCP_PORT = 4800
GUEST_TCP_SERVER_PORT = 4801
HOST_TCP_SERVER_PORT = 4802
TCP_SERVER_PAYLOAD = b"quard-star-m6c2"
STRESS_PARALLEL = 8
STRESS_RECONNECTS = 100
TCP_FLAG_FIN = 0x01
TCP_FLAG_SYN = 0x02
TCP_FLAG_PSH = 0x08
TCP_FLAG_ACK = 0x10
GUEST_MAC = bytes.fromhex("525400123456")
HOST_MAC = bytes.fromhex("525400123457")
GUEST_IP = bytes((192, 168, 100, 2))
HOST_IP = bytes((192, 168, 100, 1))
TEST_IDENTIFIER = 0x4D35
TEST_SEQUENCE = 1
HOST_IDENTIFIER = 0x4D36
HOST_SEQUENCE = 1
M4_PAYLOAD_SIZE = 32


def checksum16(data: bytes) -> int:
    if len(data) & 1:
        data += b"\0"
    total = 0
    for offset in range(0, len(data), 2):
        total += (data[offset] << 8) | data[offset + 1]
        total = (total & 0xFFFF) + (total >> 16)
    while total >> 16:
        total = (total & 0xFFFF) + (total >> 16)
    return (~total) & 0xFFFF


def _ether(dst: bytes, src: bytes, ethertype: int, payload: bytes) -> bytes:
    frame = dst + src + struct.pack("!H", ethertype) + payload
    return frame.ljust(ETHERNET_MIN_FRAME, b"\0")


def encode_arp_request(src_mac: bytes, src_ip: bytes, target_ip: bytes,
                       target_mac: bytes) -> bytes:
    payload = struct.pack("!HHBBH", ARP_HW_ETHER, ETHERTYPE_IPV4,
                          len(src_mac), len(src_ip), ARP_REQUEST)
    payload += src_mac + src_ip + target_mac + target_ip
    return _ether(b"\xff" * 6, src_mac, ETHERTYPE_ARP, payload)


def encode_arp_reply(src_mac: bytes, dst_mac: bytes, src_ip: bytes,
                     dst_ip: bytes) -> bytes:
    payload = struct.pack("!HHBBH", ARP_HW_ETHER, ETHERTYPE_IPV4,
                          len(src_mac), len(src_ip), ARP_REPLY)
    payload += src_mac + src_ip + dst_mac + dst_ip
    return _ether(dst_mac, src_mac, ETHERTYPE_ARP, payload)


def _decode_arp(frame: bytes, opcode: int):
    if len(frame) < ETHERNET_HEADER_SIZE + 28:
        return None
    if struct.unpack_from("!H", frame, 12)[0] != ETHERTYPE_ARP:
        return None
    htype, ptype, hlen, plen, actual_opcode = struct.unpack_from(
        "!HHBBH", frame, ETHERNET_HEADER_SIZE
    )
    if (htype, ptype, hlen, plen, actual_opcode) != (
        ARP_HW_ETHER, ETHERTYPE_IPV4, 6, 4, opcode
    ):
        return None
    offset = ETHERNET_HEADER_SIZE + 8
    send_mac = frame[offset:offset + 6]
    send_ip = frame[offset + 6:offset + 10]
    target_mac = frame[offset + 10:offset + 16]
    target_ip = frame[offset + 16:offset + 20]
    return send_mac, send_ip, target_mac, target_ip


def decode_arp_request(frame: bytes):
    decoded = _decode_arp(frame, ARP_REQUEST)
    if decoded is None:
        return None
    send_mac, send_ip, _target_mac, target_ip = decoded
    return send_mac, send_ip, target_ip


def decode_arp_reply(frame: bytes):
    decoded = _decode_arp(frame, ARP_REPLY)
    if decoded is None:
        return None
    send_mac, _send_ip, target_mac, target_ip = decoded
    return send_mac, target_ip, target_mac


def encode_icmp_echo(src_mac: bytes, dst_mac: bytes, src_ip: bytes,
                     dst_ip: bytes, identifier: int, sequence: int,
                     payload: bytes, icmp_type: int) -> bytes:
    icmp = struct.pack("!BBHHH", icmp_type, 0, 0, identifier, sequence)
    icmp += payload
    icmp = icmp[:2] + struct.pack("!H", checksum16(icmp)) + icmp[4:]
    total_length = 20 + len(icmp)
    ip = struct.pack("!BBHHHBBH4s4s", 0x45, 0, total_length, 1, 0x4000,
                     64, 1, 0, src_ip, dst_ip)
    ip = ip[:10] + struct.pack("!H", checksum16(ip)) + ip[12:]
    return _ether(dst_mac, src_mac, ETHERTYPE_IPV4, ip + icmp)


def decode_icmp_echo(frame: bytes):
    if len(frame) < ETHERNET_HEADER_SIZE + 20 + 8:
        return None
    dst_mac, src_mac = frame[:6], frame[6:12]
    if struct.unpack_from("!H", frame, 12)[0] != ETHERTYPE_IPV4:
        return None
    ip_offset = ETHERNET_HEADER_SIZE
    version_ihl = frame[ip_offset]
    if version_ihl >> 4 != 4:
        return None
    ihl = (version_ihl & 0x0F) * 4
    if ihl < 20 or len(frame) < ip_offset + ihl + 8:
        return None
    total_length = struct.unpack_from("!H", frame, ip_offset + 2)[0]
    if total_length < ihl + 8 or len(frame) < ip_offset + total_length:
        return None
    ip_header = frame[ip_offset:ip_offset + ihl]
    if checksum16(ip_header) != 0 or ip_header[9] != 1:
        return None
    src_ip = ip_header[12:16]
    dst_ip = ip_header[16:20]
    icmp_offset = ip_offset + ihl
    icmp = frame[icmp_offset:ip_offset + total_length]
    if checksum16(icmp) != 0:
        return None
    icmp_type, code, _checksum, identifier, sequence = struct.unpack_from(
        "!BBHHH", icmp
    )
    if code != 0:
        return None
    return {
        "src_mac": src_mac,
        "dst_mac": dst_mac,
        "src_ip": src_ip,
        "dst_ip": dst_ip,
        "type": icmp_type,
        "identifier": identifier,
        "sequence": sequence,
        "payload": icmp[8:],
    }


def decode_udp(frame: bytes):
    if len(frame) < ETHERNET_HEADER_SIZE + 28:
        return None
    if struct.unpack_from("!H", frame, 12)[0] != ETHERTYPE_IPV4:
        return None
    ip_offset = ETHERNET_HEADER_SIZE
    ihl = (frame[ip_offset] & 0x0F) * 4
    total_length = struct.unpack_from("!H", frame, ip_offset + 2)[0]
    if frame[ip_offset] >> 4 != 4 or ihl < 20 or \
            total_length < ihl + 8 or len(frame) < ip_offset + total_length:
        return None
    ip_header = frame[ip_offset:ip_offset + ihl]
    if checksum16(ip_header) != 0 or ip_header[9] != IPPROTO_UDP:
        return None
    src_ip, dst_ip = ip_header[12:16], ip_header[16:20]
    udp_offset = ip_offset + ihl
    src_port, dst_port, udp_length, udp_checksum = struct.unpack_from(
        "!HHHH", frame, udp_offset)
    if udp_length < 8 or udp_offset + udp_length > ip_offset + total_length:
        return None
    udp = frame[udp_offset:udp_offset + udp_length]
    pseudo = src_ip + dst_ip + bytes((0, IPPROTO_UDP)) + \
        struct.pack("!H", udp_length)
    if udp_checksum != 0 and checksum16(pseudo + udp) != 0:
        return None
    return src_ip, dst_ip, src_port, dst_port, udp[8:]


def encode_udp(src_mac: bytes, dst_mac: bytes, src_ip: bytes,
               dst_ip: bytes, src_port: int, dst_port: int,
               payload: bytes) -> bytes:
    udp_length = 8 + len(payload)
    udp = struct.pack("!HHHH", src_port, dst_port, udp_length, 0) + payload
    pseudo = src_ip + dst_ip + bytes((0, IPPROTO_UDP)) + \
        struct.pack("!H", udp_length)
    udp_checksum = checksum16(pseudo + udp) or 0xFFFF
    udp = udp[:6] + struct.pack("!H", udp_checksum) + udp[8:]
    total_length = 20 + udp_length
    ip = struct.pack("!BBHHHBBH4s4s", 0x45, 0, total_length, 2, 0x4000,
                     64, IPPROTO_UDP, 0, src_ip, dst_ip)
    ip = ip[:10] + struct.pack("!H", checksum16(ip)) + ip[12:]
    return _ether(dst_mac, src_mac, ETHERTYPE_IPV4, ip + udp)


def encode_tcp(src_mac: bytes, dst_mac: bytes, src_ip: bytes,
               dst_ip: bytes, src_port: int, dst_port: int,
               sequence: int, acknowledgement: int, flags: int,
               payload: bytes = b"", window: int = 4096) -> bytes:
    tcp = struct.pack("!HHIIBBHHH", src_port, dst_port, sequence,
                      acknowledgement, 5 << 4, flags, window, 0, 0)
    pseudo = src_ip + dst_ip + bytes((0, IPPROTO_TCP)) + \
        struct.pack("!H", len(tcp) + len(payload))
    tcp += payload
    tcp = tcp[:16] + struct.pack("!H", checksum16(pseudo + tcp)) + tcp[18:]
    total_length = 20 + len(tcp)
    ip = struct.pack("!BBHHHBBH4s4s", 0x45, 0, total_length, 3, 0x4000,
                     64, IPPROTO_TCP, 0, src_ip, dst_ip)
    ip = ip[:10] + struct.pack("!H", checksum16(ip)) + ip[12:]
    return _ether(dst_mac, src_mac, ETHERTYPE_IPV4, ip + tcp)


def decode_tcp(frame: bytes):
    if len(frame) < ETHERNET_HEADER_SIZE + 20 + 20:
        return None
    if struct.unpack_from("!H", frame, 12)[0] != ETHERTYPE_IPV4:
        return None
    ip_offset = ETHERNET_HEADER_SIZE
    version_ihl = frame[ip_offset]
    ihl = (version_ihl & 0x0F) * 4
    total_length = struct.unpack_from("!H", frame, ip_offset + 2)[0]
    if version_ihl >> 4 != 4 or ihl < 20 or \
            total_length < ihl + 20 or len(frame) < ip_offset + total_length:
        return None
    ip_header = frame[ip_offset:ip_offset + ihl]
    if checksum16(ip_header) != 0 or ip_header[9] != IPPROTO_TCP:
        return None
    src_ip, dst_ip = ip_header[12:16], ip_header[16:20]
    tcp_offset = ip_offset + ihl
    tcp = frame[tcp_offset:ip_offset + total_length]
    data_offset = (tcp[12] >> 4) * 4
    if data_offset < 20 or data_offset > len(tcp):
        return None
    pseudo = src_ip + dst_ip + bytes((0, IPPROTO_TCP)) + \
        struct.pack("!H", len(tcp))
    if checksum16(pseudo + tcp) != 0:
        return None
    src_port, dst_port, sequence, acknowledgement = struct.unpack_from(
        "!HHII", tcp, 0)
    return {
        "src_mac": frame[6:12],
        "dst_mac": frame[:6],
        "src_ip": src_ip,
        "dst_ip": dst_ip,
        "src_port": src_port,
        "dst_port": dst_port,
        "seq": sequence,
        "ack": acknowledgement,
        "flags": tcp[13],
        "window": struct.unpack_from("!H", tcp, 14)[0],
        "payload": tcp[data_offset:],
    }


def payload_checksum(payload: bytes) -> int:
    return sum(payload) & 0xFFFFFFFF


def decode_raw(frame: bytes):
    if len(frame) < 24:
        raise ValueError("truncated M4 frame")
    destination, source, ethertype = struct.unpack_from("!6s6sH", frame, 0)
    if destination != b"\xff" * 6 or source != GUEST_MAC or ethertype != M4_ETHERTYPE:
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


def encode_raw_response(sequence: int, payload: bytes) -> bytes:
    body = struct.pack("!IH", sequence, len(payload)) + payload
    body += struct.pack("!I", payload_checksum(payload))
    return _ether(GUEST_MAC, HOST_MAC, M4_ETHERTYPE, body)


def write_stats(path: str | None, stats: dict, elapsed: float) -> None:
    if path:
        output = dict(stats)
        output["elapsed_seconds"] = elapsed
        pathlib.Path(path).write_text(json.dumps(output) + "\n", encoding="utf-8")


class PassiveTcpStress:
    def __init__(self, peer, stats: dict):
        self.peer = peer
        self.stats = stats
        self.connections = {}

    @staticmethod
    def _payload(index: int) -> bytes:
        return f"m6c2-{index:03d}".encode()

    def _send(self, connection: dict, flags: int,
              payload: bytes = b"") -> None:
        self.peer.send(encode_tcp(
            HOST_MAC, GUEST_MAC, HOST_IP, GUEST_IP,
            connection["host_port"], GUEST_TCP_SERVER_PORT,
            connection["host_next"], connection["guest_next"],
            flags, payload))

    def _open(self, index: int) -> None:
        host_port = HOST_TCP_SERVER_PORT + index
        connection = {
            "index": index,
            "host_port": host_port,
            "payload": self._payload(index),
            "host_next": 12000 + index * 32,
            "guest_next": 0,
            "state": "syn-ack",
            "fin_acked": False,
        }
        self.connections[host_port] = connection
        self._send(connection, TCP_FLAG_SYN)

    def start(self) -> None:
        self._open(0)

    def retry_pending_syns(self) -> None:
        for connection in self.connections.values():
            if connection["state"] == "syn-ack":
                self._send(connection, TCP_FLAG_SYN)

    def _send_fin(self, connection: dict) -> None:
        self._send(connection, TCP_FLAG_FIN | TCP_FLAG_ACK)
        connection["host_next"] += 1
        connection["state"] = "closing"
        self.stats["tcp_server_stress_outstanding"] += 1

    def _finish(self, connection: dict) -> None:
        if not connection["fin_acked"]:
            self.stats["tcp_server_stress_outstanding"] -= 1
        connection["guest_next"] += 1
        self._send(connection, TCP_FLAG_ACK)
        connection["state"] = "complete"
        self.stats["tcp_server_stress_fin"] += 1
        self.stats["tcp_server_stress_live"] -= 1
        index = connection["index"]
        if index >= STRESS_PARALLEL:
            self.stats["tcp_server_stress_reconnects"] += 1
            if index + 1 < STRESS_PARALLEL + STRESS_RECONNECTS:
                self._open(index + 1)
        elif all(self.connections[HOST_TCP_SERVER_PORT + current]["state"] ==
                 "complete" for current in range(STRESS_PARALLEL)):
            self._open(STRESS_PARALLEL)

    def handle(self, segment: dict) -> None:
        connection = self.connections.get(segment["dst_port"])
        if connection is None:
            raise ValueError("unexpected TCP stress tuple")
        flags = segment["flags"]
        state = connection["state"]

        if state == "syn-ack":
            if flags != (TCP_FLAG_SYN | TCP_FLAG_ACK) or \
                    segment["ack"] != connection["host_next"] + 1 or \
                    segment["payload"]:
                raise ValueError("unexpected TCP stress SYN-ACK")
            connection["guest_next"] = segment["seq"] + 1
            connection["host_next"] += 1
            self._send(connection, TCP_FLAG_ACK)
            self._send(connection, TCP_FLAG_PSH | TCP_FLAG_ACK,
                       connection["payload"])
            connection["host_next"] += len(connection["payload"])
            connection["state"] = "data-ack"
            self.stats["tcp_server_stress_handshakes"] += 1
            self.stats["tcp_server_stress_outstanding"] += 1
            return

        if state == "data-ack":
            data_start = connection["host_next"] - len(connection["payload"])
            if flags == (TCP_FLAG_SYN | TCP_FLAG_ACK) and \
                    segment["seq"] + 1 == connection["guest_next"] and \
                    segment["ack"] == data_start and not segment["payload"]:
                self.peer.send(encode_tcp(
                    HOST_MAC, GUEST_MAC, HOST_IP, GUEST_IP,
                    connection["host_port"], GUEST_TCP_SERVER_PORT,
                    data_start, connection["guest_next"], TCP_FLAG_ACK))
                self.peer.send(encode_tcp(
                    HOST_MAC, GUEST_MAC, HOST_IP, GUEST_IP,
                    connection["host_port"], GUEST_TCP_SERVER_PORT,
                    data_start, connection["guest_next"],
                    TCP_FLAG_PSH | TCP_FLAG_ACK, connection["payload"]))
                return
            if flags != TCP_FLAG_ACK or \
                    segment["seq"] != connection["guest_next"] or \
                    segment["ack"] != connection["host_next"] or \
                    segment["payload"]:
                raise ValueError(
                    "unexpected TCP stress data ACK "
                    f"flags={flags:#x} seq={segment['seq']} "
                    f"ack={segment['ack']} expected_seq="
                    f"{connection['guest_next']} expected_ack="
                    f"{connection['host_next']} "
                    f"payload={len(segment['payload'])}"
                )
            connection["state"] = "echo"
            self.stats["tcp_server_stress_outstanding"] -= 1
            return

        if state == "echo":
            if flags != (TCP_FLAG_PSH | TCP_FLAG_ACK) or \
                    segment["seq"] != connection["guest_next"] or \
                    segment["ack"] != connection["host_next"] or \
                    segment["payload"] != connection["payload"]:
                raise ValueError("unexpected TCP stress Echo")
            connection["guest_next"] += len(connection["payload"])
            self._send(connection, TCP_FLAG_ACK)
            connection["state"] = "held"
            self.stats["tcp_server_stress_echo"] += 1
            self.stats["tcp_server_stress_live"] += 1
            self.stats["tcp_server_stress_parallel_peak"] = max(
                self.stats["tcp_server_stress_parallel_peak"],
                self.stats["tcp_server_stress_live"])
            index = connection["index"]
            if index + 1 < STRESS_PARALLEL:
                self._open(index + 1)
            elif index + 1 == STRESS_PARALLEL:
                for current in range(STRESS_PARALLEL):
                    self._send_fin(
                        self.connections[HOST_TCP_SERVER_PORT + current])
            else:
                self._send_fin(connection)
            return

        if state == "closing":
            expected = (segment["seq"] == connection["guest_next"] and
                        segment["ack"] == connection["host_next"] and
                        not segment["payload"])
            if not expected:
                raise ValueError("unexpected TCP stress close sequence")
            if flags == TCP_FLAG_ACK:
                if not connection["fin_acked"]:
                    connection["fin_acked"] = True
                    self.stats["tcp_server_stress_outstanding"] -= 1
                return
            if flags == (TCP_FLAG_FIN | TCP_FLAG_ACK):
                self._finish(connection)
                return
            raise ValueError("unexpected TCP stress close flags")

        if state == "complete" and \
                flags == (TCP_FLAG_FIN | TCP_FLAG_ACK) and \
                segment["seq"] + 1 == connection["guest_next"] and \
                segment["ack"] == connection["host_next"] and \
                not segment["payload"]:
            self._send(connection, TCP_FLAG_ACK)
            return

        raise ValueError(
            "unexpected completed TCP stress connection "
            f"flags={flags:#x} seq={segment['seq']} ack={segment['ack']} "
            f"expected_seq={connection['guest_next']} "
            f"expected_ack={connection['host_next']} "
            f"payload={len(segment['payload'])}"
        )

    def complete(self) -> bool:
        return (
            self.stats["tcp_server_stress_handshakes"] ==
            STRESS_PARALLEL + STRESS_RECONNECTS and
            self.stats["tcp_server_stress_echo"] ==
            STRESS_PARALLEL + STRESS_RECONNECTS and
            self.stats["tcp_server_stress_parallel_peak"] ==
            STRESS_PARALLEL and
            self.stats["tcp_server_stress_reconnects"] ==
            STRESS_RECONNECTS and
            self.stats["tcp_server_stress_fin"] ==
            STRESS_PARALLEL + STRESS_RECONNECTS and
            self.stats["tcp_server_stress_outstanding"] == 0
        )


def run_peer(interface: str, raw_count: int, timeout: float,
             ready_file: str | None, stats_file: str | None,
             require_udp: bool = False, require_tcp: bool = False,
             require_tcp_server: bool = False,
             require_tcp_server_stress: bool = False) -> int:
    started = time.monotonic()
    deadline = started + timeout
    stats = {
        "raw_frames": 0,
        "arp_requests": 0,
        "arp_replies": 0,
        "guest_echo_requests": 0,
        "guest_echo_replies": 0,
        "host_echo_requests": 0,
        "host_echo_replies": 0,
        "udp_requests": 0,
        "udp_replies": 0,
        "tcp_syn": 0,
        "tcp_data": 0,
        "tcp_retransmissions": 0,
        "tcp_fin": 0,
        "tcp_outstanding": 0,
        "tcp_server_syn": 0,
        "tcp_server_handshakes": 0,
        "tcp_server_data": 0,
        "tcp_server_echo": 0,
        "tcp_server_retransmissions": 0,
        "tcp_server_fin": 0,
        "tcp_server_outstanding": 0,
        "tcp_server_stress_handshakes": 0,
        "tcp_server_stress_echo": 0,
        "tcp_server_stress_parallel_peak": 0,
        "tcp_server_stress_reconnects": 0,
        "tcp_server_stress_fin": 0,
        "tcp_server_stress_outstanding": 0,
        "tcp_server_stress_live": 0,
    }
    expected_raw = 0
    host_arp_request_sent = False
    host_echo_request_sent = False
    tcp_tuple = None
    tcp_guest_isn = 0
    tcp_guest_data_end = 0
    tcp_host_next = 0
    tcp_payload = b""
    tcp_handshake_ack_seen = False
    tcp_data_seen = False
    tcp_echo_sent = False
    tcp_fin_sent = False
    tcp_server_started = False
    tcp_server_guest_isn = 0
    tcp_server_guest_next = 0
    tcp_server_host_next = 12000
    tcp_server_state = "syn-ack"
    tcp_server_stress = None

    def complete() -> bool:
        return (
            stats["raw_frames"] >= raw_count and
            stats["arp_requests"] >= 1 and
            stats["arp_replies"] >= 1 and
            stats["guest_echo_requests"] >= 1 and
            stats["guest_echo_replies"] >= 1 and
            stats["host_echo_requests"] >= 1 and
            stats["host_echo_replies"] >= 1 and
            (not require_udp or (
                stats["udp_requests"] >= 1 and
                stats["udp_replies"] >= 1
            )) and
            (not require_tcp or (
                stats["tcp_syn"] >= 1 and
                stats["tcp_data"] >= 1 and
                stats["tcp_retransmissions"] >= 1 and
                stats["tcp_fin"] >= 1 and
                stats["tcp_outstanding"] == 0
            )) and
            (not require_tcp_server or (
                stats["tcp_server_syn"] >= 1 and
                stats["tcp_server_handshakes"] >= 1 and
                stats["tcp_server_data"] >= 1 and
                stats["tcp_server_echo"] >= 1 and
                stats["tcp_server_retransmissions"] >= 1 and
                stats["tcp_server_fin"] >= 1 and
                stats["tcp_server_outstanding"] == 0
            )) and
            (not require_tcp_server_stress or (
                tcp_server_stress is not None and
                tcp_server_stress.complete()
            ))
        )

    with socket.socket(socket.AF_PACKET, socket.SOCK_RAW,
                       socket.htons(0x0003)) as peer:
        peer.bind((interface, 0))
        if ready_file:
            pathlib.Path(ready_file).write_text("ready\n", encoding="utf-8")

        while not complete():
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f"timed out with stats {stats}")
            peer.settimeout(min(remaining, 0.5)
                            if tcp_server_stress is not None else remaining)
            try:
                frame, address = peer.recvfrom(ETHERNET_MAX_FRAME)
            except socket.timeout:
                if tcp_server_stress is None:
                    raise
                tcp_server_stress.retry_pending_syns()
                continue
            if len(address) > 2 and address[2] == socket.PACKET_OUTGOING:
                continue
            ethertype = struct.unpack_from("!H", frame, 12)[0] \
                if len(frame) >= ETHERNET_HEADER_SIZE else 0

            if ethertype == M4_ETHERTYPE and stats["raw_frames"] < raw_count:
                sequence, payload = decode_raw(frame)
                if sequence != expected_raw:
                    raise ValueError(
                        f"expected raw sequence {expected_raw}, got {sequence}"
                    )
                peer.send(encode_raw_response(sequence, payload))
                stats["raw_frames"] += 1
                expected_raw += 1
                continue

            if ethertype == ETHERTYPE_ARP:
                request = decode_arp_request(frame)
                if request is not None and request[0] == GUEST_MAC and \
                        request[1] == GUEST_IP and request[2] == HOST_IP:
                    stats["arp_requests"] += 1
                    peer.send(encode_arp_reply(HOST_MAC, GUEST_MAC,
                                               HOST_IP, GUEST_IP))
                    stats["arp_replies"] += 1
                    if not host_arp_request_sent:
                        peer.send(encode_arp_request(HOST_MAC, HOST_IP,
                                                     GUEST_IP, GUEST_MAC))
                        host_arp_request_sent = True
                    continue
                reply = decode_arp_reply(frame)
                if reply is not None and reply[0] == GUEST_MAC and \
                        reply[1] == HOST_IP and reply[2] == HOST_MAC:
                    # This is the guest's answer to the host ARP request.
                    stats["arp_replies"] += 1
                continue

            if ethertype != ETHERTYPE_IPV4:
                continue
            event = decode_icmp_echo(frame)
            udp = decode_udp(frame)
            if udp is not None and udp[0] == GUEST_IP and \
                    udp[1] == HOST_IP and udp[2] == GUEST_UDP_PORT and \
                    udp[3] == HOST_UDP_PORT:
                stats["udp_requests"] += 1
                peer.send(encode_udp(HOST_MAC, GUEST_MAC, HOST_IP, GUEST_IP,
                                     HOST_UDP_PORT, GUEST_UDP_PORT, udp[4]))
                stats["udp_replies"] += 1
                continue
            tcp = decode_tcp(frame)
            if tcp is not None and tcp["src_ip"] == GUEST_IP and \
                    tcp["dst_ip"] == HOST_IP and \
                    tcp["dst_port"] == HOST_TCP_PORT:
                flags = tcp["flags"]
                current_tuple = (tcp["src_port"], tcp["dst_port"])
                if flags == TCP_FLAG_SYN:
                    duplicate_syn = (
                        require_tcp_server_stress and tcp_tuple is not None and
                        not tcp_handshake_ack_seen and
                        current_tuple == tcp_tuple and
                        tcp["seq"] == tcp_guest_isn and tcp["ack"] == 0 and
                        not tcp["payload"]
                    )
                    if duplicate_syn:
                        peer.send(encode_tcp(
                            HOST_MAC, GUEST_MAC, HOST_IP, GUEST_IP,
                            HOST_TCP_PORT, tcp["src_port"], 9000,
                            tcp_guest_data_end,
                            TCP_FLAG_SYN | TCP_FLAG_ACK))
                        continue
                    if tcp_tuple is not None or tcp["ack"] != 0 or \
                            tcp["payload"]:
                        raise ValueError("unexpected second TCP SYN")
                    tcp_tuple = current_tuple
                    tcp_guest_isn = tcp["seq"]
                    tcp_guest_data_end = tcp_guest_isn + 1
                    tcp_host_next = 9000 + 1
                    stats["tcp_syn"] += 1
                    peer.send(encode_tcp(
                        HOST_MAC, GUEST_MAC, HOST_IP, GUEST_IP,
                        HOST_TCP_PORT, tcp["src_port"], 9000,
                        tcp_guest_data_end, TCP_FLAG_SYN | TCP_FLAG_ACK))
                    continue
                if tcp_tuple != current_tuple:
                    raise ValueError("unexpected TCP tuple")
                if not tcp_handshake_ack_seen:
                    if flags != TCP_FLAG_ACK or \
                            tcp["seq"] != tcp_guest_data_end or \
                            tcp["ack"] != tcp_host_next or tcp["payload"]:
                        raise ValueError("unexpected TCP handshake ACK")
                    tcp_handshake_ack_seen = True
                    continue
                if not tcp_data_seen:
                    if flags != (TCP_FLAG_PSH | TCP_FLAG_ACK) or \
                            tcp["seq"] != tcp_guest_data_end or \
                            tcp["ack"] != tcp_host_next or \
                            not tcp["payload"]:
                        raise ValueError("unexpected TCP data segment")
                    tcp_data_seen = True
                    tcp_payload = tcp["payload"]
                    tcp_guest_data_end = tcp["seq"] + len(tcp_payload)
                    stats["tcp_data"] += 1
                    stats["tcp_outstanding"] = 1
                    continue
                if not tcp_echo_sent:
                    if flags != (TCP_FLAG_PSH | TCP_FLAG_ACK) or \
                            tcp["seq"] != tcp_guest_isn + 1 or \
                            tcp["ack"] != tcp_host_next or \
                            tcp["payload"] != tcp_payload:
                        raise ValueError("unexpected TCP retransmission")
                    stats["tcp_retransmissions"] += 1
                    stats["tcp_outstanding"] = 1
                    peer.send(encode_tcp(
                        HOST_MAC, GUEST_MAC, HOST_IP, GUEST_IP,
                        HOST_TCP_PORT, tcp["src_port"], tcp_host_next,
                        tcp_guest_data_end,
                        TCP_FLAG_PSH | TCP_FLAG_ACK, tcp_payload))
                    tcp_host_next += len(tcp_payload)
                    tcp_echo_sent = True
                    continue
                if require_tcp_server_stress and \
                        flags == (TCP_FLAG_PSH | TCP_FLAG_ACK) and \
                        tcp["seq"] == tcp_guest_isn + 1 and \
                        tcp["ack"] == tcp_host_next - len(tcp_payload) and \
                        tcp["payload"] == tcp_payload:
                    stats["tcp_retransmissions"] += 1
                    peer.send(encode_tcp(
                        HOST_MAC, GUEST_MAC, HOST_IP, GUEST_IP,
                        HOST_TCP_PORT, tcp["src_port"],
                        tcp_host_next - len(tcp_payload),
                        tcp_guest_data_end, TCP_FLAG_PSH | TCP_FLAG_ACK,
                        tcp_payload))
                    continue
                if not tcp_fin_sent:
                    if flags == TCP_FLAG_ACK and \
                            tcp["seq"] == tcp_guest_data_end and \
                            tcp["ack"] == tcp_host_next and not tcp["payload"]:
                        stats["tcp_outstanding"] = 0
                        continue
                    if flags != (TCP_FLAG_FIN | TCP_FLAG_ACK) or \
                            tcp["seq"] != tcp_guest_data_end or \
                            tcp["ack"] != tcp_host_next or tcp["payload"]:
                        raise ValueError(
                            "unexpected TCP FIN "
                            f"flags={flags:#x} seq={tcp['seq']} "
                            f"ack={tcp['ack']} expected_seq="
                            f"{tcp_guest_data_end} expected_ack="
                            f"{tcp_host_next} payload={len(tcp['payload'])}"
                        )
                    stats["tcp_fin"] += 1
                    peer.send(encode_tcp(
                        HOST_MAC, GUEST_MAC, HOST_IP, GUEST_IP,
                        HOST_TCP_PORT, tcp["src_port"], tcp_host_next,
                        tcp["seq"] + 1, TCP_FLAG_FIN | TCP_FLAG_ACK))
                    tcp_host_next += 1
                    tcp_fin_sent = True
                    stats["tcp_outstanding"] = 1
                    continue
                if require_tcp_server_stress and \
                        flags == (TCP_FLAG_FIN | TCP_FLAG_ACK) and \
                        tcp["seq"] == tcp_guest_data_end and \
                        tcp["ack"] == tcp_host_next - 1 and \
                        not tcp["payload"]:
                    peer.send(encode_tcp(
                        HOST_MAC, GUEST_MAC, HOST_IP, GUEST_IP,
                        HOST_TCP_PORT, tcp["src_port"], tcp_host_next - 1,
                        tcp_guest_data_end + 1,
                        TCP_FLAG_FIN | TCP_FLAG_ACK))
                    continue
                if flags != TCP_FLAG_ACK or \
                        tcp["seq"] != tcp_guest_data_end + 1 or \
                        tcp["ack"] != tcp_host_next or tcp["payload"]:
                    raise ValueError("unexpected TCP final ACK")
                stats["tcp_outstanding"] = 0
                if require_tcp_server_stress and tcp_server_stress is None:
                    tcp_server_stress = PassiveTcpStress(peer, stats)
                    tcp_server_stress.start()
                elif require_tcp_server and not tcp_server_started:
                    peer.send(encode_tcp(
                        HOST_MAC, GUEST_MAC, HOST_IP, GUEST_IP,
                        HOST_TCP_SERVER_PORT, GUEST_TCP_SERVER_PORT,
                        tcp_server_host_next, 0, TCP_FLAG_SYN))
                    tcp_server_started = True
                    stats["tcp_server_syn"] += 1
                continue
            if tcp is not None and tcp_server_stress is not None and \
                    tcp["src_mac"] == GUEST_MAC and \
                    tcp["dst_mac"] == HOST_MAC and \
                    tcp["src_ip"] == GUEST_IP and \
                    tcp["dst_ip"] == HOST_IP and \
                    tcp["src_port"] == GUEST_TCP_SERVER_PORT:
                tcp_server_stress.handle(tcp)
                continue
            if tcp is not None and tcp_server_started and \
                    tcp["src_mac"] == GUEST_MAC and \
                    tcp["dst_mac"] == HOST_MAC and \
                    tcp["src_ip"] == GUEST_IP and \
                    tcp["dst_ip"] == HOST_IP and \
                    tcp["src_port"] == GUEST_TCP_SERVER_PORT and \
                    tcp["dst_port"] == HOST_TCP_SERVER_PORT:
                flags = tcp["flags"]
                if tcp_server_state == "syn-ack":
                    if flags != (TCP_FLAG_SYN | TCP_FLAG_ACK) or \
                            tcp["ack"] != tcp_server_host_next + 1 or \
                            tcp["payload"]:
                        raise ValueError("unexpected TCP server SYN-ACK")
                    tcp_server_guest_isn = tcp["seq"]
                    tcp_server_guest_next = tcp_server_guest_isn + 1
                    tcp_server_host_next += 1
                    peer.send(encode_tcp(
                        HOST_MAC, GUEST_MAC, HOST_IP, GUEST_IP,
                        HOST_TCP_SERVER_PORT, GUEST_TCP_SERVER_PORT,
                        tcp_server_host_next, tcp_server_guest_next,
                        TCP_FLAG_ACK))
                    peer.send(encode_tcp(
                        HOST_MAC, GUEST_MAC, HOST_IP, GUEST_IP,
                        HOST_TCP_SERVER_PORT, GUEST_TCP_SERVER_PORT,
                        tcp_server_host_next, tcp_server_guest_next,
                        TCP_FLAG_PSH | TCP_FLAG_ACK, TCP_SERVER_PAYLOAD))
                    tcp_server_host_next += len(TCP_SERVER_PAYLOAD)
                    stats["tcp_server_handshakes"] += 1
                    stats["tcp_server_outstanding"] = 1
                    tcp_server_state = "data-ack"
                    continue
                if tcp_server_state == "data-ack":
                    if flags != TCP_FLAG_ACK or \
                            tcp["seq"] != tcp_server_guest_next or \
                            tcp["ack"] != tcp_server_host_next or \
                            tcp["payload"]:
                        raise ValueError("unexpected TCP server data ACK")
                    stats["tcp_server_data"] += 1
                    stats["tcp_server_outstanding"] = 0
                    tcp_server_state = "echo"
                    continue
                if tcp_server_state == "echo":
                    if flags & TCP_FLAG_FIN:
                        raise ValueError("premature TCP server FIN")
                    if flags != (TCP_FLAG_PSH | TCP_FLAG_ACK) or \
                            tcp["seq"] != tcp_server_guest_next or \
                            tcp["ack"] != tcp_server_host_next or \
                            tcp["payload"] != TCP_SERVER_PAYLOAD:
                        raise ValueError("unexpected TCP server Echo")
                    tcp_server_guest_next += len(TCP_SERVER_PAYLOAD)
                    stats["tcp_server_echo"] += 1
                    stats["tcp_server_outstanding"] = 1
                    tcp_server_state = "echo-retransmission"
                    continue
                if tcp_server_state == "echo-retransmission":
                    if flags & TCP_FLAG_FIN:
                        raise ValueError("premature TCP server FIN")
                    if flags != (TCP_FLAG_PSH | TCP_FLAG_ACK) or \
                            tcp["seq"] != tcp_server_guest_isn + 1 or \
                            tcp["ack"] != tcp_server_host_next or \
                            tcp["payload"] != TCP_SERVER_PAYLOAD:
                        raise ValueError("unexpected TCP server retransmission")
                    stats["tcp_server_retransmissions"] += 1
                    peer.send(encode_tcp(
                        HOST_MAC, GUEST_MAC, HOST_IP, GUEST_IP,
                        HOST_TCP_SERVER_PORT, GUEST_TCP_SERVER_PORT,
                        tcp_server_host_next, tcp_server_guest_next,
                        TCP_FLAG_ACK))
                    stats["tcp_server_outstanding"] = 0
                    tcp_server_state = "fin"
                    continue
                if tcp_server_state == "fin":
                    if flags != (TCP_FLAG_FIN | TCP_FLAG_ACK) or \
                            tcp["seq"] != tcp_server_guest_next or \
                            tcp["ack"] != tcp_server_host_next or \
                            tcp["payload"]:
                        raise ValueError("unexpected TCP server FIN")
                    tcp_server_guest_next += 1
                    stats["tcp_server_fin"] += 1
                    peer.send(encode_tcp(
                        HOST_MAC, GUEST_MAC, HOST_IP, GUEST_IP,
                        HOST_TCP_SERVER_PORT, GUEST_TCP_SERVER_PORT,
                        tcp_server_host_next, tcp_server_guest_next,
                        TCP_FLAG_FIN | TCP_FLAG_ACK))
                    tcp_server_host_next += 1
                    stats["tcp_server_outstanding"] = 1
                    tcp_server_state = "final-ack"
                    continue
                if flags != TCP_FLAG_ACK or \
                        tcp["seq"] != tcp_server_guest_next or \
                        tcp["ack"] != tcp_server_host_next or tcp["payload"]:
                    raise ValueError("unexpected TCP server final ACK")
                stats["tcp_server_outstanding"] = 0
                tcp_server_state = "complete"
                continue
            if event is None:
                continue
            if event["src_mac"] == GUEST_MAC and \
                    event["src_ip"] == GUEST_IP and \
                    event["dst_ip"] == HOST_IP:
                if event["type"] == ICMP_ECHO_REQUEST and \
                        event["dst_ip"] == HOST_IP:
                    if event["identifier"] != TEST_IDENTIFIER or \
                            event["sequence"] != TEST_SEQUENCE:
                        raise ValueError("unexpected guest ICMP echo request")
                    stats["guest_echo_requests"] += 1
                    peer.send(encode_icmp_echo(
                        HOST_MAC, GUEST_MAC, HOST_IP, GUEST_IP,
                        TEST_IDENTIFIER, TEST_SEQUENCE, event["payload"],
                        ICMP_ECHO_REPLY))
                    stats["guest_echo_replies"] += 1
                    if not host_echo_request_sent:
                        peer.send(encode_icmp_echo(
                            HOST_MAC, GUEST_MAC, HOST_IP, GUEST_IP,
                            HOST_IDENTIFIER, HOST_SEQUENCE,
                            b"host-to-guest-m5", ICMP_ECHO_REQUEST))
                        stats["host_echo_requests"] += 1
                        host_echo_request_sent = True
                elif event["type"] == ICMP_ECHO_REPLY and \
                        event["identifier"] == HOST_IDENTIFIER and \
                        event["sequence"] == HOST_SEQUENCE:
                    stats["host_echo_replies"] += 1

    write_stats(stats_file, stats, time.monotonic() - started)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="M5 ARP/ICMP TAP peer")
    parser.add_argument("interface")
    parser.add_argument("--raw-count", type=int, default=32)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--ready-file")
    parser.add_argument("--stats-file")
    parser.add_argument("--require-udp", action="store_true")
    parser.add_argument("--require-tcp", action="store_true")
    parser.add_argument("--require-tcp-server", action="store_true")
    parser.add_argument("--require-tcp-server-stress", action="store_true")
    args = parser.parse_args()
    if args.raw_count < 0 or args.timeout <= 0:
        parser.error("--raw-count must be non-negative and --timeout positive")

    try:
        return run_peer(args.interface, args.raw_count, args.timeout,
                        args.ready_file, args.stats_file, args.require_udp,
                        args.require_tcp, args.require_tcp_server,
                        args.require_tcp_server_stress)
    except (OSError, TimeoutError, ValueError) as error:
        print(f"m5-peer: {error}", file=sys.stderr)
        return 1

if __name__ == "__main__":
    raise SystemExit(main())
