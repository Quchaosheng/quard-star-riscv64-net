#!/usr/bin/env python3
import argparse
import json
import math
import os
import pathlib
import re
import shutil
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
    parser.add_argument(
        "--stage", required=True, choices=("m8", "m6c2-stress")
    )
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
    def unique_object(pairs):
        result = {}
        for name, value in pairs:
            if name in result:
                raise BaselineError(f"duplicate peer stats key: {name}")
            result[name] = value
        return result

    try:
        peer = json.loads(
            read_text(path, "peer stats"), object_pairs_hook=unique_object
        )
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


def stage_pass_marker(stage):
    return {
        "m8": "QS:TEST_PASS:m8-smoke",
        "m6c2-stress": "QS:TEST_PASS:m6c2-stress",
    }.get(stage)


def parse_guest_log(path, stage):
    found = {name: [] for name in COUNTERS.values()}
    pass_markers = []
    for line in read_text(path, "QEMU log").splitlines():
        if line.startswith("QS:TEST_FAIL"):
            raise BaselineError(f"QEMU log contains failure marker: {line}")
        for prefix, name in COUNTERS.items():
            if line.startswith(prefix):
                value = line[len(prefix):]
                if re.fullmatch(r"[0-9]+", value) is None:
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
    expected_marker = stage_pass_marker(stage)
    if expected_marker is not None:
        pass_markers = [marker for marker in pass_markers
                        if marker == expected_marker]
    if not pass_markers:
        raise BaselineError("missing pass marker")
    if len(pass_markers) != 1:
        raise BaselineError("duplicate pass marker")
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
        require_value(guest, "allocation_operations", 14000)
        require_value(guest, "migrations", 100)
        require_value(peer, "tftp_bytes", 1048576)
        require_value(peer, "tftp_outstanding", 0)
    elif stage == "m6c2-stress":
        require_value(guest, "pass_marker", "QS:TEST_PASS:m6c2-stress")
        require_value(guest, "allocation_operations", 100000)
        require_value(guest, "migrations", 10000)
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
    guest = parse_guest_log(args.qemu_log, args.stage)
    peer = load_peer_stats(args.peer_stats)
    validate(args.stage, guest, peer)
    derived = {}
    if peer.get("tftp_bytes", 0) > 0:
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


def write_temporary(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
                mode="w", encoding="utf-8", newline="\n", delete=False,
                dir=path.parent, prefix=f".{path.name}.") as output:
            temporary = pathlib.Path(output.name)
            output.write(content)
            output.flush()
            os.fsync(output.fileno())
    except OSError:
        if temporary is not None:
            cleanup_paths((temporary,))
        raise
    return temporary


def cleanup_paths(paths):
    for path in paths:
        try:
            path.unlink(missing_ok=True)
        except OSError:
            pass


def replace_texts(outputs):
    outputs = tuple(outputs)
    temporaries = {}
    backups = {}
    replaced = []
    try:
        for path, content in outputs:
            temporaries[path] = write_temporary(path, content)
        for path, _ in outputs:
            if path.exists():
                with tempfile.NamedTemporaryFile(
                        delete=False, dir=path.parent,
                        prefix=f".{path.name}.backup.") as backup:
                    backup_path = pathlib.Path(backup.name)
                backups[path] = backup_path
                shutil.copy2(path, backup_path)
        for path, _ in outputs:
            os.replace(temporaries.pop(path), path)
            replaced.append(path)
    except OSError as error:
        rollback_errors = []
        for path in reversed(replaced):
            try:
                backup = backups.pop(path, None)
                if backup is None:
                    path.unlink(missing_ok=True)
                else:
                    os.replace(backup, path)
            except OSError as rollback_error:
                rollback_errors.append(f"{path}: {rollback_error}")
        cleanup_paths(temporaries.values())
        cleanup_paths(backups.values())
        detail = ""
        if rollback_errors:
            detail = "; rollback failed for " + ", ".join(rollback_errors)
        raise BaselineError(f"cannot replace report outputs: {error}{detail}") from error
    cleanup_paths(backups.values())


def main():
    args = parse_args()
    if args.json_out.resolve() == args.markdown_out.resolve():
        raise BaselineError("output paths must be different")
    report = build_report(args)
    json_text = json.dumps(report, indent=2, ensure_ascii=True) + "\n"
    markdown_text = render_markdown(report)
    replace_texts(((args.json_out, json_text),
                   (args.markdown_out, markdown_text)))


if __name__ == "__main__":
    try:
        main()
    except BaselineError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
