# QEMU Demo

**English** | [简体中文](qemu-demo.zh-CN.md)

The README demo is a compact, post-processed replay of a real M8 QEMU/TAP
acceptance run. It does not invent successful results: the renderer first
validates the raw ordinary-kernel log, trusted UART2 log, and TAP peer
statistics, then presents their accepted evidence as a readable 42-second
video. The output itself is synthesized with FFmpeg, not a screen recording or
a live QEMU/terminal capture.

## Watch

<a href="assets/qemu-m8-demo.mp4"><img src="assets/qemu-m8-demo.gif" alt="Animated M8 QEMU/TAP evidence replay"></a>

The preview is animated from the committed MP4. Click it to open the full 42-second video.

The committed media is H.264, 1280x720, 30 fps, `yuv420p`, without audio. Its
source and output digests are recorded in
[qemu-m8-demo-evidence.json](assets/qemu-m8-demo-evidence.json).

## Generate from scratch

Use Ubuntu 24.04, directly or through WSL2. The environment check may accept
26.04, but current CI and retained acceptance evidence cover Ubuntu 24.04 only.
In addition to the normal M8 build and TAP dependencies, install `ffmpeg` and
`ffprobe`:

```sh
sudo apt-get update
sudo apt-get install -y ffmpeg
make demo
```

`make demo` performs these steps in order:

1. Build the cumulative M8 firmware.
2. Run the full eight-hart QEMU/TAP acceptance test with `sudo`.
3. Reject the run unless all required kernel, FreeRTOS, PMP, protocol, and
   1 MiB TFTP evidence is present exactly once.
4. Render and verify the video and poster.
5. Write a machine-readable evidence manifest.

Public DNS or Internet access is not part of the runtime acceptance path. The
TAP peer provides deterministic local DNS, HTTP, NTP, TFTP, ICMP, and UDP
traffic.

## Render existing evidence

If `make m8-smoke` has already passed and its artifacts remain in `out/m8`,
avoid rebuilding or rerunning QEMU:

```sh
make demo-render
```

The renderer reads:

- `out/m8/qemu.log`
- `out/m8/trusted.log`
- `out/m8/m5-peer.stats`

It writes:

- `docs/assets/qemu-m8-demo.mp4`
- `docs/assets/qemu-m8-demo.gif`
- `docs/assets/qemu-m8-demo-poster.png`
- `docs/assets/qemu-m8-demo-evidence.json`

## Validation boundary

Rendering is refused when any of the following conditions is true:

- A required marker is missing or duplicated.
- Either log contains a `QS:TEST_FAIL` marker.
- Harts 0-6 did not all publish their online marker.
- Trusted scheduling or either PMP summary marker is missing.
- DNS, HTTP, NTP, ping, or TFTP acceptance is incomplete.
- The peer did not observe exactly 1 MiB, 2049 TFTP data blocks and ACKs, or
  reports outstanding TFTP packets.
- The output is not 42-second H.264 video at 1280x720 with `yuv420p` pixels.

The video and animated preview are explanatory artifacts, not additional
acceptance authority or a recording of the run. The raw logs, peer statistics,
smoke-test exit status, and CI artifacts remain the authoritative evidence.

The scheduled, manually dispatched, and release-tag M8 workflow renders a
media set with the same renderer and format after smoke acceptance. The
regenerated MP4, poster, evidence JSON,
serial logs, and peer statistics are uploaded together in the
`m8-serial-logs` workflow artifact.
