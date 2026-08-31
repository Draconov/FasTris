#!/usr/bin/env python3
"""Decode FasTris .ftr replay files into human-readable text.

This parser mirrors the strict current binary replay layout documented in
``docs/REPLAY_FORMAT.md``. It has no third-party dependencies and intentionally
does not attempt deterministic game-state verification; use ``fastris_verify``
for that. The final SHA-256 stored in the replay is still displayed.
"""

from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
import sys

MAGIC = b"FASTRIS\x01"
MAX_REPLAY_BYTES = 16 * 1024 * 1024
MAX_REPLAY_EVENTS = 1_000_000
MAX_REPLAY_DURATION_US = 12 * 60 * 60 * 1_000_000

MODE_NAMES = (
    "Sprint 40L",
    "Ultra 2:00",
    "Marathon",
    "Zen",
    "Cheese 40",
    "Finesse",
    "Seed Race",
    "Sandbox / Custom",
)

ACTION_NAMES = (
    "Move left",
    "Move right",
    "Soft drop",
    "Hard drop",
    "Rotate clockwise",
    "Rotate counter-clockwise",
    "Rotate 180 degrees",
    "Hold piece",
)


class ReplayDecodeError(ValueError):
    pass


@dataclass(frozen=True)
class ReplayEvent:
    time_us: int
    delta_us: int
    action: int
    pressed: bool


@dataclass(frozen=True)
class ReplayRules:
    das_ms: int
    arr_ms: int
    sdf: int
    dcd_ms: int
    lock_delay_ms: int
    max_lock_resets: int
    allow_180: bool
    irs: bool
    ihs: bool
    ghost: bool
    tournament: bool
    next_count: int
    garbage_cap: int
    garbage_delay_ms: int
    garbage_messiness_pct: int
    custom_gravity_ms: int
    custom_line_goal: int
    custom_time_limit_s: int
    custom_start_garbage: int


@dataclass(frozen=True)
class Replay:
    seed: int
    mode: int
    rules: ReplayRules
    duration_us: int
    events: tuple[ReplayEvent, ...]
    final_hash: bytes


class Reader:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.pos = 0

    def read_byte(self, what: str) -> int:
        if self.pos >= len(self.data):
            raise ReplayDecodeError(f"truncated {what}")
        value = self.data[self.pos]
        self.pos += 1
        return value

    def read_raw(self, size: int, what: str) -> bytes:
        end = self.pos + size
        if end > len(self.data):
            raise ReplayDecodeError(f"truncated {what}")
        value = self.data[self.pos:end]
        self.pos = end
        return value

    def read_varuint(self, what: str) -> int:
        value = 0
        for shift in range(0, 64, 7):
            byte = self.read_byte(what)
            if shift == 63 and byte & 0xFE:
                raise ReplayDecodeError(f"invalid/overflowing {what}")
            value |= (byte & 0x7F) << shift
            if not (byte & 0x80):
                return value
        raise ReplayDecodeError(f"invalid/overflowing {what}")

    def read_bounded_varuint(self, maximum: int, what: str, *, minimum: int = 0) -> int:
        value = self.read_varuint(what)
        if not minimum <= value <= maximum:
            raise ReplayDecodeError(f"{what} is out of range: {value}")
        return value


def decode_replay(data: bytes) -> Replay:
    if len(data) > MAX_REPLAY_BYTES:
        raise ReplayDecodeError("replay file is too large")

    reader = Reader(data)
    if reader.read_raw(len(MAGIC), "replay header") != MAGIC:
        raise ReplayDecodeError("unsupported replay format (expected FASTRIS binary v1)")

    seed = reader.read_varuint("replay seed")
    mode = reader.read_byte("replay mode")
    if mode >= len(MODE_NAMES):
        raise ReplayDecodeError(f"invalid replay mode: {mode}")

    das_ms = reader.read_bounded_varuint(5000, "DAS")
    arr_ms = reader.read_bounded_varuint(5000, "ARR")
    sdf = reader.read_bounded_varuint(1000, "SDF")
    dcd_ms = reader.read_bounded_varuint(5000, "DCD")
    lock_delay_ms = reader.read_bounded_varuint(10000, "lock delay")
    max_lock_resets = reader.read_bounded_varuint(1000, "max lock resets")

    flags = reader.read_byte("rule flags")
    if flags & 0xE0:
        raise ReplayDecodeError(f"invalid rule flags: 0x{flags:02x}")

    rules = ReplayRules(
        das_ms=das_ms,
        arr_ms=arr_ms,
        sdf=sdf,
        dcd_ms=dcd_ms,
        lock_delay_ms=lock_delay_ms,
        max_lock_resets=max_lock_resets,
        allow_180=bool(flags & 0x01),
        irs=bool(flags & 0x02),
        ihs=bool(flags & 0x04),
        ghost=bool(flags & 0x08),
        tournament=bool(flags & 0x10),
        next_count=reader.read_bounded_varuint(8, "next queue count", minimum=1),
        garbage_cap=reader.read_bounded_varuint(100, "garbage cap"),
        garbage_delay_ms=reader.read_bounded_varuint(60000, "garbage delay"),
        garbage_messiness_pct=reader.read_bounded_varuint(100, "garbage messiness"),
        custom_gravity_ms=reader.read_bounded_varuint(60000, "custom gravity"),
        custom_line_goal=reader.read_bounded_varuint(1_000_000, "custom line goal"),
        custom_time_limit_s=reader.read_bounded_varuint(43_200, "custom time limit"),
        custom_start_garbage=reader.read_bounded_varuint(12, "custom starting garbage"),
    )

    duration_us = reader.read_bounded_varuint(MAX_REPLAY_DURATION_US, "replay duration")
    event_count = reader.read_bounded_varuint(MAX_REPLAY_EVENTS, "replay event count")

    events: list[ReplayEvent] = []
    timestamp_us = 0
    for index in range(event_count):
        delta_us = reader.read_varuint(f"event {index + 1} timestamp delta")
        packed = reader.read_byte(f"event {index + 1}")
        if packed & 0x70:
            raise ReplayDecodeError(f"event {index + 1} has invalid flags: 0x{packed:02x}")
        action = packed & 0x0F
        if action >= len(ACTION_NAMES):
            raise ReplayDecodeError(f"event {index + 1} has invalid action: {action}")
        timestamp_us += delta_us
        if timestamp_us > duration_us:
            raise ReplayDecodeError(
                f"event {index + 1} is outside replay duration "
                f"({timestamp_us} us > {duration_us} us)"
            )
        events.append(ReplayEvent(timestamp_us, delta_us, action, bool(packed & 0x80)))

    final_hash = reader.read_raw(32, "replay hash")
    if reader.pos != len(data):
        raise ReplayDecodeError(f"unexpected trailing replay data: {len(data) - reader.pos} byte(s)")

    return Replay(seed, mode, rules, duration_us, tuple(events), final_hash)


def yes_no(value: bool) -> str:
    return "Yes" if value else "No"


def format_time_us(value: int) -> str:
    hours, rem = divmod(value, 3_600_000_000)
    minutes, rem = divmod(rem, 60_000_000)
    seconds, micros = divmod(rem, 1_000_000)
    if hours:
        return f"{hours:02d}:{minutes:02d}:{seconds:02d}.{micros:06d}"
    return f"{minutes:02d}:{seconds:02d}.{micros:06d}"


def format_delta_us(value: int) -> str:
    if value < 1000:
        return f"+{value} us"
    if value < 1_000_000:
        return f"+{value / 1000:.3f} ms"
    return f"+{value / 1_000_000:.6f} s"


def render_text(replay: Replay, source: Path, include_events: bool = True) -> str:
    rules = replay.rules
    presses = Counter(event.action for event in replay.events if event.pressed)
    releases = Counter(event.action for event in replay.events if not event.pressed)

    lines = [
        "FasTris replay decoded",
        "======================",
        f"File: {source}",
        "Format: FASTRIS binary v1 (.ftr)",
        f"Game mode: {MODE_NAMES[replay.mode]}",
        f"Seed: {replay.seed}",
        f"Duration: {format_time_us(replay.duration_us)} ({replay.duration_us} us)",
        f"Input transitions: {len(replay.events)}",
        f"Stored final state SHA-256: {replay.final_hash.hex()}",
        "",
        "Rules / handling",
        "----------------",
        f"DAS: {rules.das_ms} ms (delay before held left/right starts auto-shifting)",
        f"ARR: {rules.arr_ms} ms (delay between automatic horizontal repeats; 0 = instant shift)",
        f"SDF: {rules.sdf} (soft-drop factor; 0 = sonic soft drop)",
        f"DCD: {rules.dcd_ms} ms (direction-change delay)",
        f"Lock delay: {rules.lock_delay_ms} ms",
        f"Maximum lock resets: {rules.max_lock_resets}",
        f"180-degree rotation enabled: {yes_no(rules.allow_180)}",
        f"IRS / initial rotation: {yes_no(rules.irs)}",
        f"IHS / initial hold: {yes_no(rules.ihs)}",
        f"Ghost piece: {yes_no(rules.ghost)}",
        f"Tournament rules: {yes_no(rules.tournament)}",
        f"Next queue pieces shown: {rules.next_count}",
        f"Garbage cap: {rules.garbage_cap}",
        f"Garbage delay: {rules.garbage_delay_ms} ms",
        f"Garbage messiness: {rules.garbage_messiness_pct}%",
        f"Custom gravity: {rules.custom_gravity_ms} ms per cell",
        f"Custom line goal: {rules.custom_line_goal or 'Disabled / endless'}",
        f"Custom time limit: {str(rules.custom_time_limit_s) + ' s' if rules.custom_time_limit_s else 'Disabled / endless'}",
        f"Custom starting garbage: {rules.custom_start_garbage} line(s)",
        "",
        "Input summary",
        "-------------",
    ]

    if not replay.events:
        lines.append("No gameplay input transitions were recorded.")
    else:
        for action, name in enumerate(ACTION_NAMES):
            count = presses[action] + releases[action]
            if count:
                lines.append(f"{name}: {presses[action]} press(es), {releases[action]} release(s)")

    lines.extend([
        "",
        "Note: this script validates the binary structure and displays the stored final hash.",
        "Use the native 'fastris_verify' tool when you need deterministic re-simulation/hash verification.",
    ])

    if include_events:
        lines.extend(["", "Input timeline", "--------------"])
        if not replay.events:
            lines.append("(empty)")
        else:
            width = max(6, len(str(len(replay.events))))
            for index, event in enumerate(replay.events, 1):
                state = "PRESS  " if event.pressed else "RELEASE"
                lines.append(
                    f"#{index:0{width}d}  {format_time_us(event.time_us)}  "
                    f"({format_delta_us(event.delta_us):>13})  {state}  {ACTION_NAMES[event.action]}"
                )

    return "\n".join(lines) + "\n"


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Decode a FasTris .ftr replay into human-readable text."
    )
    parser.add_argument("replay", type=Path, help="path to the .ftr replay file")
    parser.add_argument(
        "-o", "--output", type=Path,
        help="write decoded text to this file instead of standard output",
    )
    parser.add_argument(
        "--summary-only", action="store_true",
        help="show metadata/rules/input counts but omit the full event timeline",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    try:
        data = args.replay.read_bytes()
        replay = decode_replay(data)
        text = render_text(replay, args.replay, include_events=not args.summary_only)
        if args.output:
            args.output.write_text(text, encoding="utf-8")
            print(f"Decoded replay written to: {args.output}")
        else:
            sys.stdout.write(text)
        return 0
    except (OSError, ReplayDecodeError) as exc:
        print(f"Replay decode failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
