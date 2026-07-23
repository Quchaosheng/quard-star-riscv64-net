#!/usr/bin/env python3
import argparse
import json
import math
import os
import pathlib
import re
import subprocess
import sys
import tempfile


SCHEMA_VERSION = 1
INTERPRETATION = (
    "Results from different hosts or QEMU configurations must not be "
    "compared directly."
)
COUNTERS = {
    "QS:STRESS_ALLOC_OPS:": "allocation_operations",
    "QS:STRESS_MIGRATIONS:": "migrations",
    "QS:STRESS_ELAPSED_TICKS:": "elapsed_ticks",
}


class BaselineError(Exception):
    pass


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", required=True)
    parser.add_argument("--commit")
    parser.add_argument("--qemu-log", required=True, type=pathlib.Path)
    parser.add_argument("--peer-stats", required=True, type=pathlib.Path)
    parser.add_argument("--json-out", required=True, type=pathlib.Path)
    parser.add_argument("--markdown-out", required=True, type=pathlib.Path)
    return parser.parse_args()


def read_text(path, description):
    try:
        return path.read_text(encoding="utf-8", errors="strict")
    except (OSError, UnicodeError) as error:
        raise BaselineError(f"cannot read {description}: {error}") from error


def load_peer_stats(path):
    try:
        peer = json.loads(read_text(path, "peer stats"))
    except json.JSONDecodeError as error:
        raise BaselineError(f"invalid peer stats JSON: {error.msg}") from error
    if not isinstance(peer, dict):
        raise BaselineError("peer stats must be a JSON object")
    for name, value in peer.items():
        if (not isinstance(name, str) or isinstance(value, bool) or
                not isinstance(value, (int, float))):
            raise BaselineError(f"peer counter {name!r} must be numeric")
        if value < 0 or (isinstance(value, float) and not math.isfinite(value)):
            raise BaselineError(f"peer counter {name!r} must be finite and nonnegative")
    return peer


def parse_guest_log(path):
    found = {name: [] for name in COUNTERS.values()}
    pass_markers = []
    for line in read_text(path, "QEMU log").splitlines():
        for prefix, name in COUNTERS.items():
            if line.startswith(prefix):
                value = line[len(prefix):]
                if not value.isdecimal():
                    raise BaselineError(f"invalid {name} counter")
                found[name].append(int(value))
        if line.startswith("QS:TEST_PASS:"):
            pass_markers.append(line)

    guest = {}
    for name, values in found.items():
        if not values:
            raise BaselineError(f"missing {name} counter")
        if len(values) != 1:
            raise BaselineError(f"duplicate {name} counter")
        guest[name] = values[0]
    if guest["elapsed_ticks"] <= 0:
        raise BaselineError("elapsed_ticks must be positive")
    if not pass_markers:
        raise BaselineError("missing pass marker")
    if len(pass_markers) != 1:
        raise BaselineError("multiple pass markers")
    guest["pass_marker"] = pass_markers[0]
    return guest


def require_value(values, name, expected):
    actual = values.get(name)
    if actual != expected:
        raise BaselineError(f"{name} must be {expected}; found {actual!r}")


def validate(stage, guest, peer):
    elapsed = peer.get("elapsed_seconds")
    if (isinstance(elapsed, bool) or not isinstance(elapsed, (int, float)) or
            elapsed <= 0):
        raise BaselineError("elapsed_seconds must be positive")

    if stage == "m8":
        require_value(guest, "pass_marker", "QS:TEST_PASS:m8-smoke")
        require_value(peer, "tftp_bytes", 1048576)
        require_value(peer, "tftp_outstanding", 0)
    elif stage == "m6c2-stress":
        require_value(guest, "pass_marker", "QS:TEST_PASS:m6c2-stress")
        expected = {
            "tcp_server_stress_handshakes": 108,
            "tcp_server_stress_echo": 108,
            "tcp_server_stress_parallel_peak": 8,
            "tcp_server_stress_reconnects": 100,
            "tcp_server_stress_fin": 108,
            "tcp_server_stress_outstanding": 0,
            "tcp_server_stress_live": 0,
        }
        for name, value in expected.items():
            require_value(peer, name, value)


def resolve_commit(explicit):
    if explicit is not None:
        if re.fullmatch(r"[0-9a-fA-F]{7,40}", explicit) is None:
            raise BaselineError("commit must contain 7 to 40 hexadecimal characters")
        return explicit.lower()
    try:
        result = subprocess.run(
            ["git", "rev-parse", "HEAD"], text=True, capture_output=True,
            check=False,
        )
    except OSError:
        return None
    value = result.stdout.strip()
    if result.returncode == 0 and re.fullmatch(r"[0-9a-fA-F]{40}", value):
        return value.lower()
    return None


def build_report(args):
    guest = parse_guest_log(args.qemu_log)
    peer = load_peer_stats(args.peer_stats)
    validate(args.stage, guest, peer)
    derived = {}
    if "tftp_bytes" in peer:
        derived["tftp_bytes_per_second"] = (
            peer["tftp_bytes"] / peer["elapsed_seconds"]
        )
    return {
        "schema_version": SCHEMA_VERSION,
        "stage": args.stage,
        "commit": resolve_commit(args.commit),
        "guest": guest,
        "peer": peer,
        "derived": derived,
        "interpretation": INTERPRETATION,
    }


def metric_rows(report):
    guest = report["guest"]
    peer = report["peer"]
    rows = [
        ("Allocation operations", guest["allocation_operations"]),
        ("Migrations", guest["migrations"]),
        ("Guest elapsed ticks", guest["elapsed_ticks"]),
        ("Host elapsed seconds", peer["elapsed_seconds"]),
    ]
    if report["stage"] == "m8":
        rows.extend([
            ("TFTP bytes", peer["tftp_bytes"]),
            ("TFTP outstanding", peer["tftp_outstanding"]),
            ("TFTP bytes per second", report["derived"]["tftp_bytes_per_second"]),
        ])
    elif report["stage"] == "m6c2-stress":
        rows.extend([
            ("Handshakes", peer["tcp_server_stress_handshakes"]),
            ("Echoes", peer["tcp_server_stress_echo"]),
            ("Peak parallel connections", peer["tcp_server_stress_parallel_peak"]),
            ("Reconnects", peer["tcp_server_stress_reconnects"]),
            ("FINs", peer["tcp_server_stress_fin"]),
            ("Outstanding connections", peer["tcp_server_stress_outstanding"]),
            ("Live connections", peer["tcp_server_stress_live"]),
        ])
    else:
        rows.extend((name, value) for name, value in sorted(peer.items())
                    if name != "elapsed_seconds")
    return rows


def render_markdown(report):
    lines = [
        f"# Performance Baseline: {report['stage']}",
        "",
        f"- Commit: `{report['commit'] or 'unknown'}`",
        f"- Pass marker: `{report['guest']['pass_marker']}`",
        "",
        "| Metric | Value |",
        "|---|---:|",
    ]
    lines.extend(f"| {name} | {value} |" for name, value in metric_rows(report))
    lines.extend(["", f"> {report['interpretation']}", ""])
    return "\n".join(lines)


def replace_text(path, content):
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(
                mode="w", encoding="utf-8", newline="\n", delete=False,
                dir=path.parent, prefix=f".{path.name}.") as output:
            temporary = pathlib.Path(output.name)
            output.write(content)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
    except OSError as error:
        if "temporary" in locals():
            temporary.unlink(missing_ok=True)
        raise BaselineError(f"cannot write {path}: {error}") from error


def main():
    args = parse_args()
    report = build_report(args)
    json_text = json.dumps(report, indent=2, ensure_ascii=True) + "\n"
    markdown_text = render_markdown(report)
    replace_text(args.json_out, json_text)
    replace_text(args.markdown_out, markdown_text)


if __name__ == "__main__":
    try:
        main()
    except BaselineError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
