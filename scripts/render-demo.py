#!/usr/bin/env python3
"""Render a compact M8 acceptance replay from validated QEMU artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import subprocess
import tempfile


WIDTH = 1280
HEIGHT = 720
DURATION = 42.0

QEMU_MARKERS = (
    "QS:BOOT_OK",
    "QS:KERNEL_READY",
    *(f"QS:HART_ONLINE:{hart}" for hart in range(7)),
    "QS:STRESS_ALLOC_OPS:14000",
    "QS:STRESS_MIGRATIONS:100",
    "QS:PMP_UNTRUSTED_DENY_OK",
    "QS:M5_PING_OK",
    "QS:M7A_DNS_RESOLVE_OK",
    "QS:M7B_HTTP_RESPONSE_OK",
    "QS:M7C_NTP_RESPONSE_OK",
    "QS:M7E_TFTP_1M_OK",
    "QS:M7E_TFTP_SHA256_OK",
    "QS:TEST_PASS:m8-smoke",
)

TRUSTED_MARKERS = (
    "QS:TRUSTED_READY",
    "QS:TRUSTED_SCHED_OK",
    "QS:PMP_TRUSTED_DENY_OK",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate M8 artifacts and render the README demo video."
    )
    parser.add_argument("--qemu-log", type=pathlib.Path, default=pathlib.Path("out/m8/qemu.log"))
    parser.add_argument(
        "--trusted-log", type=pathlib.Path, default=pathlib.Path("out/m8/trusted.log")
    )
    parser.add_argument(
        "--peer-stats", type=pathlib.Path, default=pathlib.Path("out/m8/m5-peer.stats")
    )
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        default=pathlib.Path("docs/assets/qemu-m8-demo.mp4"),
    )
    parser.add_argument(
        "--poster",
        type=pathlib.Path,
        default=pathlib.Path("docs/assets/qemu-m8-demo-poster.png"),
    )
    parser.add_argument(
        "--evidence",
        type=pathlib.Path,
        default=pathlib.Path("docs/assets/qemu-m8-demo-evidence.json"),
    )
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--ffprobe", default="ffprobe")
    parser.add_argument("--commit")
    parser.add_argument(
        "--validate-only",
        action="store_true",
        help="validate inputs and print the evidence summary without invoking FFmpeg",
    )
    return parser.parse_args()


def read_clean_text(path: pathlib.Path) -> str:
    try:
        return path.read_bytes().replace(b"\x00", b"").decode("utf-8", errors="replace").replace("\r", "")
    except OSError as error:
        raise SystemExit(f"error: unable to read {path}: {error}") from error


def load_stats(path: pathlib.Path) -> dict[str, object]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise SystemExit(f"error: invalid peer statistics {path}: {error}") from error
    if not isinstance(data, dict):
        raise SystemExit(f"error: peer statistics {path} must contain a JSON object")
    return data


def require_markers(text: str, markers: tuple[str, ...], source: pathlib.Path) -> None:
    lines = text.splitlines()
    for marker in markers:
        count = lines.count(marker)
        if count != 1:
            raise SystemExit(
                f"error: expected exactly one {marker} in {source}, found {count}"
            )
    failures = [line for line in lines if line.startswith("QS:TEST_FAIL")]
    if failures:
        raise SystemExit(f"error: failure marker in {source}: {failures[0]}")


def require_stat(stats: dict[str, object], name: str, expected: object) -> None:
    if stats.get(name) != expected:
        raise SystemExit(
            f"error: expected peer statistic {name}={expected!r}, found {stats.get(name)!r}"
        )


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as error:
        raise SystemExit(f"error: unable to hash {path}: {error}") from error
    return digest.hexdigest()


def source_commit(explicit: str | None) -> str:
    if explicit:
        return explicit
    try:
        return subprocess.run(
            ["git", "rev-parse", "--short=12", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def validate(args: argparse.Namespace) -> dict[str, object]:
    qemu_text = read_clean_text(args.qemu_log)
    trusted_text = read_clean_text(args.trusted_log)
    stats = load_stats(args.peer_stats)

    require_markers(qemu_text, QEMU_MARKERS, args.qemu_log)
    require_markers(trusted_text, TRUSTED_MARKERS, args.trusted_log)

    for name, expected in (
        ("raw_frames", 32),
        ("http_outstanding", 0),
        ("tftp_bytes", 1048576),
        ("tftp_data", 2049),
        ("tftp_acks", 2049),
        ("tftp_outstanding", 0),
    ):
        require_stat(stats, name, expected)

    elapsed = stats.get("elapsed_seconds")
    if not isinstance(elapsed, (int, float)) or elapsed <= 0:
        raise SystemExit("error: peer statistic elapsed_seconds must be positive")

    return {
        "schema_version": 1,
        "source_commit": source_commit(args.commit),
        "source_sha256": {
            "qemu_log": sha256(args.qemu_log),
            "trusted_log": sha256(args.trusted_log),
            "peer_stats": sha256(args.peer_stats),
        },
        "acceptance": {
            "ordinary_harts": 7,
            "trusted_harts": 1,
            "allocation_operations": 14000,
            "scheduler_migrations": 100,
            "raw_frames": stats["raw_frames"],
            "dns_replies": stats.get("dns_replies"),
            "http_responses": stats.get("http_responses"),
            "ntp_replies": stats.get("ntp_replies"),
            "tftp_bytes": stats["tftp_bytes"],
            "tftp_blocks": stats["tftp_data"],
            "peer_elapsed_seconds": elapsed,
            "pass_marker": "QS:TEST_PASS:m8-smoke",
        },
    }


def ass_time(seconds: float) -> str:
    centiseconds = round(seconds * 100)
    hours, remainder = divmod(centiseconds, 360000)
    minutes, remainder = divmod(remainder, 6000)
    secs, fraction = divmod(remainder, 100)
    return f"{hours}:{minutes:02d}:{secs:02d}.{fraction:02d}"


def ass_escape(text: str) -> str:
    return text.replace("\\", r"\\").replace("{", r"\{").replace("}", r"\}")


def dialogue(start: float, end: float, style: str, text: str, x: int, y: int) -> str:
    return (
        f"Dialogue: 0,{ass_time(start)},{ass_time(end)},{style},,0,0,0,,"
        f"{{\\pos({x},{y})\\fad(180,180)}}{ass_escape(text)}"
    )


def make_ass(summary: dict[str, object]) -> str:
    acceptance = summary["acceptance"]
    assert isinstance(acceptance, dict)
    commit = str(summary["source_commit"])
    elapsed = float(acceptance["peer_elapsed_seconds"])

    header = """[Script Info]
ScriptType: v4.00+
PlayResX: 1280
PlayResY: 720
WrapStyle: 2
ScaledBorderAndShadow: yes

[V4+ Styles]
Format: Name,Fontname,Fontsize,PrimaryColour,SecondaryColour,OutlineColour,BackColour,Bold,Italic,Underline,StrikeOut,ScaleX,ScaleY,Spacing,Angle,BorderStyle,Outline,Shadow,Alignment,MarginL,MarginR,MarginV,Encoding
Style: Brand,DejaVu Sans,22,&H00A7F3D0,&H000000FF,&H00080D18,&H00080D18,1,0,0,0,100,100,0,0,1,0,0,7,0,0,0,1
Style: Title,DejaVu Sans,42,&H00F8FAFC,&H000000FF,&H00080D18,&H00080D18,1,0,0,0,100,100,0,0,1,0,0,7,0,0,0,1
Style: Section,DejaVu Sans,27,&H00F8FAFC,&H000000FF,&H00080D18,&H00080D18,1,0,0,0,100,100,0,0,1,0,0,7,0,0,0,1
Style: Mono,DejaVu Sans Mono,23,&H00CBD5E1,&H000000FF,&H00080D18,&H00080D18,0,0,0,0,100,100,0,0,1,0,0,7,0,0,0,1
Style: Accent,DejaVu Sans Mono,23,&H00F4BF75,&H000000FF,&H00080D18,&H00080D18,0,0,0,0,100,100,0,0,1,0,0,7,0,0,0,1
Style: Success,DejaVu Sans Mono,23,&H0072E6A3,&H000000FF,&H00080D18,&H00080D18,1,0,0,0,100,100,0,0,1,0,0,7,0,0,0,1
Style: Muted,DejaVu Sans,17,&H0094A3B8,&H000000FF,&H00080D18,&H00080D18,0,0,0,0,100,100,0,0,1,0,0,7,0,0,0,1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
"""

    events = [
        dialogue(0, DURATION, "Brand", "quard-star-riscv64-net / M8 evidence replay", 82, 78),
        dialogue(0, DURATION, "Muted", f"commit {commit}", 1000, 80),
        dialogue(
            0,
            DURATION,
            "Muted",
            "validated qemu.log + trusted.log + m5-peer.stats",
            82,
            676,
        ),
    ]

    scenes: list[tuple[float, float, str, list[tuple[str, str]]]] = [
        (
            0.2,
            5.5,
            "Eight-hart RISC-V64 system acceptance",
            [
                ("$ make m8-smoke", "Accent"),
                ("QEMU quard-star + OpenSBI domains + Linux TAP", "Mono"),
                ("Deterministic local services; no public Internet", "Mono"),
            ],
        ),
        (
            5.5,
            11.0,
            "OpenSBI establishes the domain boundary",
            [
                ("OpenSBI v1.2", "Accent"),
                ("Platform HART Count       : 8", "Mono"),
                ("Domain1 untrusted-domain  : harts 0-6", "Mono"),
                ("Domain2 trusted-domain    : hart 7", "Mono"),
            ],
        ),
        (
            11.0,
            17.0,
            "Seven ordinary harts run the SMP kernel",
            [
                ("QS:HART_ONLINE:0 ... QS:HART_ONLINE:6", "Success"),
                ("QS:STRESS_ALLOC_OPS:14000", "Mono"),
                ("QS:STRESS_MIGRATIONS:100", "Mono"),
                ("VirtIO block + FatFs + VirtIO net", "Mono"),
            ],
        ),
        (
            17.0,
            23.5,
            "Hart 7 runs isolated FreeRTOS",
            [
                ("QS:TRUSTED_READY", "Success"),
                ("QS:TRUSTED_SCHED_OK", "Success"),
                ("QS:PMP_UNTRUSTED_DENY_OK", "Success"),
                ("QS:PMP_TRUSTED_DENY_OK", "Success"),
            ],
        ),
        (
            23.5,
            30.0,
            "The first-party IPv4 stack reaches local services",
            [
                ("QS:M5_PING_OK", "Success"),
                (f"DNS replies              : {acceptance.get('dns_replies')}", "Mono"),
                (f"HTTP responses           : {acceptance.get('http_responses')}", "Mono"),
                (f"NTP replies              : {acceptance.get('ntp_replies')}", "Mono"),
            ],
        ),
        (
            30.0,
            36.5,
            "A 1 MiB TFTP transfer crosses the full stack",
            [
                ("QS:M7E_TFTP_1M_OK", "Success"),
                ("QS:M7E_TFTP_SHA256_OK", "Success"),
                (f"Data blocks / ACKs       : {acceptance['tftp_blocks']} / {acceptance['tftp_blocks']}", "Mono"),
                (f"Peer elapsed             : {elapsed:.3f} s", "Mono"),
            ],
        ),
        (
            36.5,
            42.0,
            "Full M8 acceptance completed",
            [
                ("QS:TEST_PASS:m8-smoke", "Success"),
                ("7 ordinary harts + 1 trusted hart", "Mono"),
                ("SMP / storage / network / FreeRTOS / PMP", "Mono"),
            ],
        ),
    ]

    for start, end, title, lines in scenes:
        events.append(dialogue(start, end, "Section", title, 82, 136))
        for index, (text, style) in enumerate(lines):
            events.append(
                dialogue(start + 0.65 + index * 0.55, end, style, text, 98, 214 + index * 62)
            )

    return header + "\n".join(events) + "\n"


def run(command: list[str], *, cwd: pathlib.Path | None = None) -> None:
    try:
        subprocess.run(command, check=True, cwd=cwd)
    except FileNotFoundError as error:
        raise SystemExit(f"error: required command not found: {command[0]}") from error
    except subprocess.CalledProcessError as error:
        raise SystemExit(f"error: command failed with status {error.returncode}: {command[0]}") from error


def render(args: argparse.Namespace, summary: dict[str, object]) -> None:
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.poster.parent.mkdir(parents=True, exist_ok=True)
    args.evidence.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="qs-demo-") as directory:
        temporary = pathlib.Path(directory)
        (temporary / "demo.ass").write_text(make_ass(summary), encoding="utf-8", newline="\n")
        video = args.output.resolve()
        filter_graph = (
            "drawbox=x=48:y=52:w=1184:h=620:color=0x111827:t=fill,"
            "drawbox=x=48:y=52:w=1184:h=54:color=0x1f2937:t=fill,"
            "drawbox=x=48:y=52:w=1184:h=620:color=0x334155:t=2,"
            "subtitles=demo.ass"
        )
        run(
            [
                args.ffmpeg,
                "-y",
                "-hide_banner",
                "-loglevel",
                "warning",
                "-f",
                "lavfi",
                "-i",
                f"color=c=0x080d18:s={WIDTH}x{HEIGHT}:r=30:d={DURATION}",
                "-vf",
                filter_graph,
                "-an",
                "-c:v",
                "libx264",
                "-preset",
                "slow",
                "-crf",
                "20",
                "-pix_fmt",
                "yuv420p",
                "-movflags",
                "+faststart",
                "-map_metadata",
                "-1",
                str(video),
            ],
            cwd=temporary,
        )

    run(
        [
            args.ffmpeg,
            "-y",
            "-hide_banner",
            "-loglevel",
            "warning",
            "-ss",
            "39.6",
            "-i",
            str(args.output.resolve()),
            "-frames:v",
            "1",
            "-update",
            "1",
            "-compression_level",
            "9",
            str(args.poster.resolve()),
        ]
    )

    try:
        probe = subprocess.run(
            [
                args.ffprobe,
                "-v",
                "error",
                "-select_streams",
                "v:0",
                "-show_entries",
                "stream=codec_name,width,height,pix_fmt:format=duration",
                "-of",
                "json",
                str(args.output.resolve()),
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        media = json.loads(probe.stdout)
    except (OSError, subprocess.CalledProcessError, ValueError, KeyError) as error:
        raise SystemExit(f"error: unable to verify rendered video: {error}") from error

    stream = media.get("streams", [{}])[0]
    duration = float(media.get("format", {}).get("duration", 0))
    if (
        stream.get("codec_name") != "h264"
        or stream.get("width") != WIDTH
        or stream.get("height") != HEIGHT
        or stream.get("pix_fmt") != "yuv420p"
        or not (DURATION - 0.1 <= duration <= DURATION + 0.1)
    ):
        raise SystemExit(f"error: unexpected rendered video properties: {media}")

    summary["media"] = {
        "video": args.output.as_posix(),
        "video_sha256": sha256(args.output),
        "poster": args.poster.as_posix(),
        "poster_sha256": sha256(args.poster),
        "codec": stream["codec_name"],
        "pixel_format": stream["pix_fmt"],
        "width": stream["width"],
        "height": stream["height"],
        "duration_seconds": duration,
    }
    args.evidence.write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n"
    )


def main() -> int:
    args = parse_args()
    summary = validate(args)
    if args.validate_only:
        print(json.dumps(summary, indent=2, sort_keys=True))
        return 0
    render(args, summary)
    print(f"PASS: rendered {args.output} and {args.poster}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
