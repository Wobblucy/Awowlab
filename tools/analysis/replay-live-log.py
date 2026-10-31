#!/usr/bin/env python3
"""Replay a saved combat log into a file the app can tail as if it were live.

Reads a real pull and writes it out again line by line with fresh timestamps,
so live mode, the overlay and the wipe-triggered comparison can be exercised
without sitting in a raid.

The output is named WoWCombatLog-<stamp>.txt because that is what the log
watcher matches on, and it lands in the folder given by --out-dir. Point that
at the WoW Logs folder to have the app find it on its own.

    python tools/analysis/replay-live-log.py testlogs/midnight-falls-kill.txt

Speed is real time by default. --speed 10 replays ten times faster, which is
usually what you want when the pull is nine minutes long.

    python tools/analysis/replay-live-log.py testlogs/midnight-falls-kill.txt --speed 20

--loop writes the same pull over and over with a fresh start time each round,
which is how to get several attempts on one boss - the app sees pull after
pull the way it would on a progression night.

    python tools/analysis/replay-live-log.py testlogs/midnight-falls-kill.txt --speed 20 --loop 3

Every damage number is scaled by a random factor, one per pull, so looped
pulls read as different attempts rather than the same numbers over and over.
--vary sets the range (10% by default), --vary 0 replays the pull verbatim,
and --seed repeats a given run.
"""

import argparse
import random
import re
import sys
import time
from datetime import datetime, timedelta
from pathlib import Path

# "7/21/2026 23:47:25.100-6  ENCOUNTER_START,..." - the offset is the local
# UTC offset WoW writes, kept as-is so the parsed times stay self-consistent.
LINE_RE = re.compile(
    r"^(?P<month>\d{1,2})/(?P<day>\d{1,2})/(?P<year>\d{4}) "
    r"(?P<time>\d{1,2}:\d{2}:\d{2}\.\d{3})(?P<offset>[-+]\d{1,2})(?P<rest>.*)$",
    re.DOTALL,
)


def parse_stamp(line):
    """Timestamp and the rest of the line, or (None, None) when it has none."""
    match = LINE_RE.match(line)
    if not match:
        return None, None
    stamp = datetime.strptime(
        f"{match['year']}-{match['month']}-{match['day']} {match['time']}",
        "%Y-%m-%d %H:%M:%S.%f",
    )
    return stamp, match


# Damage events whose amounts are worth varying. Healing is left alone: the
# comparison measures damage, and changing both would muddy which one moved.
DAMAGE_EVENTS = frozenset({
    "SPELL_DAMAGE",
    "SPELL_PERIODIC_DAMAGE",
    "SPELL_BUILDING_DAMAGE",
    "RANGE_DAMAGE",
    "SWING_DAMAGE",
    "SWING_DAMAGE_LANDED",
    "DAMAGE_SPLIT",
    "DAMAGE_SHIELD",
})

# The tail of a damage event is
#   amount, baseAmount, overkill, school, resisted, blocked, absorbed,
#   critical, glancing, crushing, isOffHand
# and the last three are always "nil"/"1". Spell events add a "ST"/"AOE" tag
# after that; swings do not. Finding the three flags is what locates the two
# amounts without hardcoding a field index per event type - those indices
# differ between spell and swing, and getting one wrong would scale a school
# mask or a resource value instead of damage.
FLAG_VALUES = frozenset({"nil", "1"})


def scale_damage(payload, factor):
    """One damage line with its amount and baseAmount scaled.

    Returns the payload unchanged when the line does not look like the layout
    above - better to leave a line alone than to corrupt a field by guessing.
    """
    fields = payload.split(",")
    if len(fields) < 12:
        return payload

    # Walk back past an optional ST/AOE tag to the three boolean flags.
    end = len(fields)
    if fields[-1] in ("ST", "AOE"):
        end -= 1

    flags = fields[end - 3:end]
    if len(flags) != 3 or not all(f in FLAG_VALUES for f in flags):
        return payload

    # critical/glancing/crushing sit at end-3; the amounts are seven and eight
    # fields before that.
    amount_at = end - 3 - 7
    if amount_at < 1:
        return payload

    changed = False
    for index in (amount_at, amount_at + 1):
        raw = fields[index]
        if not raw.isdigit():
            return payload   # Not the layout we expected; leave it be
        scaled = int(int(raw) * factor)
        fields[index] = str(max(1, scaled))
        changed = True

    return ",".join(fields) if changed else payload


def last_stamp(lines):
    """Timestamp of the final timestamped line, i.e. when the pull ended."""
    for line in reversed(lines):
        stamp, _ = parse_stamp(line)
        if stamp is not None:
            return stamp
    return None


def format_line(when, offset, rest):
    """Rebuild a log line with a new time, keeping WoW's exact spacing."""
    # WoW writes no leading zero on month/day and pads the line with two
    # spaces after the offset. Both matter: the tokenizer splits on them.
    return (
        f"{when.month}/{when.day}/{when.year} "
        f"{when:%H:%M:%S}.{when.microsecond // 1000:03d}{offset}{rest}"
    )


def load_pull(path):
    """The header, the pull's own lines, and the time the pull starts.

    A saved pull keeps the COMBAT_LOG_VERSION line from the top of the log it
    came from, and that line is stamped whenever the original log was opened -
    often hours before the fight. It is split out here for two reasons: the
    app needs it once at the top of the file to know the format, and leaving
    it inline would send time backwards at the start of every looped pull.
    """
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    if not lines:
        sys.exit(f"{path} is empty")

    header = []
    body = []
    for line in lines:
        if not body and "COMBAT_LOG_VERSION" in line:
            header.append(line)
        else:
            body.append(line)

    origin = None
    for line in body:
        stamp, _ = parse_stamp(line)
        if stamp is not None:
            origin = stamp
            break

    if origin is None:
        sys.exit(f"{path} has no timestamped lines - is it a combat log?")

    return header, body, origin


def rewrite(rest, factor):
    """The part of a line after its timestamp, with damage scaled."""
    if factor == 1.0:
        return rest

    # `rest` still carries the two spaces that separate stamp from payload.
    stripped = rest.lstrip()
    if not stripped:
        return rest
    lead = rest[:len(rest) - len(stripped)]

    event = stripped.split(",", 1)[0]
    if event not in DAMAGE_EVENTS:
        return rest

    return lead + scale_damage(stripped, factor)


def dump(lines, origin, out_file, shift, factor=1.0):
    """Write the whole pull at once, the way the game flushes its log.

    WoW does not stream the file out event by event - it writes when the
    encounter ends, so the app sees a block of new bytes appear in one go.
    Pacing the lines out was modelling something the app never actually meets.
    """
    written = 0
    for line in lines:
        stamp, match = parse_stamp(line)
        if stamp is None:
            out_file.write(line + "\n")
        else:
            out_file.write(
                format_line(stamp + shift, match["offset"],
                            rewrite(match["rest"], factor)) + "\n")
            written += 1
    out_file.flush()
    return written


def replay(lines, origin, out_file, speed, shift, quiet, factor=1.0):
    """Write every line with its time shifted, pacing to match the original."""
    started = time.monotonic()
    written = 0

    for line in lines:
        stamp, match = parse_stamp(line)

        if stamp is None:
            # A line with no timestamp (rare, but they exist) goes out as-is.
            out_file.write(line + "\n")
            continue

        # Where this line sits inside the pull, which is what decides both
        # its new timestamp and when it should be written.
        offset_into_pull = (stamp - origin).total_seconds()
        out_file.write(
            format_line(stamp + shift, match["offset"],
                        rewrite(match["rest"], factor)) + "\n"
        )
        written += 1

        # Flushing every line is the point: a buffered writer would hand the
        # app one huge block at the end, which is not what it will see live.
        out_file.flush()

        # Catch up to where this line belongs in wall-clock time. Falling
        # behind is fine and just means the replay runs as fast as it can.
        target = offset_into_pull / speed
        elapsed = time.monotonic() - started
        if target > elapsed:
            time.sleep(target - elapsed)

        if not quiet and written % 5000 == 0:
            print(f"  {written} lines ({offset_into_pull:.0f}s into the pull)",
                  flush=True)

    return written


def main():
    parser = argparse.ArgumentParser(
        description="Replay a saved pull into a tailable combat log.")
    parser.add_argument("source", type=Path,
                        help="Combat log holding the pull to replay")
    parser.add_argument("--out-dir", type=Path, default=Path("testlogs/live"),
                        help="Where to write the fake log "
                             "(default: testlogs/live)")
    parser.add_argument("--speed", type=float, default=1.0,
                        help="Playback multiplier; 20 replays 20x faster "
                             "(default: real time). Ignored with --dump.")
    parser.add_argument("--dump", action="store_true",
                        help="Write the whole pull at once instead of pacing "
                             "it out, which is what the game does - the log is "
                             "flushed when the encounter ends, not streamed")
    parser.add_argument("--loop", type=int, default=1,
                        help="Replay this many times, each as a fresh pull "
                             "(default: 1)")
    parser.add_argument("--gap", type=float, default=15.0,
                        help="Seconds of quiet between looped pulls, in "
                             "replay time (default: 15)")
    parser.add_argument("--vary", type=float, default=10.0, metavar="PERCENT",
                        help="Scale every damage number by a random amount up "
                             "to this percent, one factor per pull, so looped "
                             "pulls differ instead of being identical "
                             "(default: 10; 0 replays the pull verbatim)")
    parser.add_argument("--seed", type=int,
                        help="Seed for --vary, to repeat a given run")
    parser.add_argument("--quiet", action="store_true",
                        help="Only print the summary")
    args = parser.parse_args()

    if args.seed is not None:
        random.seed(args.seed)

    if args.speed <= 0:
        sys.exit("--speed must be positive")
    if not args.source.is_file():
        sys.exit(f"No such file: {args.source}")

    header, body, origin = load_pull(args.source)

    args.out_dir.mkdir(parents=True, exist_ok=True)
    # The watcher matches on this name and takes the most recent, so a new
    # stamp each run means the newest replay always wins.
    stamp = datetime.now().strftime("%m%d%y_%H%M%S")
    out_path = args.out_dir / f"WoWCombatLog-{stamp}.txt"

    if args.dump:
        print(f"Dumping {args.source.name} ({len(body)} lines) in one write")
    else:
        print(f"Replaying {args.source.name} ({len(body)} lines) "
              f"at {args.speed}x")
    print(f"  -> {out_path}")
    if args.loop > 1:
        print(f"  {args.loop} pulls, {args.gap}s apart")
    print("  Ctrl-C to stop\n", flush=True)

    total = 0
    try:
        # Line buffered and never truncated between pulls: the app tails by
        # byte offset, so rewriting the file would look like it shrank.
        with out_path.open("w", encoding="utf-8", buffering=1) as out_file:
            # Once, at the top, stamped now. The app reads this to learn the
            # log format, and a looped replay must not repeat it - a second
            # copy carries the original stamp and sends time backwards.
            for line in header:
                stamp, match = parse_stamp(line)
                if stamp is None:
                    out_file.write(line + "\n")
                else:
                    out_file.write(
                        format_line(datetime.now(), match["offset"],
                                    match["rest"]) + "\n")
            out_file.flush()

            # How far into the future each pull's stamps are pushed. Tracked
            # across loops rather than recomputed from the clock, because the
            # replay runs faster than real time: a second pull anchored to
            # "now" would start before the first one finished, and the app
            # tails a log where time only moves forward.
            span = last_stamp(body) - origin
            shift = datetime.now() - origin

            for pull in range(args.loop):
                # One factor for the whole pull, not per line: the point is
                # that this attempt did more or less damage than the last, and
                # jittering each hit separately would average back out to the
                # same total.
                factor = 1.0
                if args.vary > 0:
                    factor = 1.0 + random.uniform(-args.vary, args.vary) / 100.0

                if not args.quiet:
                    label = f"Pull {pull + 1}/{args.loop}"
                    if args.vary > 0:
                        label += f" (damage x{factor:.3f})"
                    print(label, flush=True)

                if args.dump:
                    total += dump(body, origin, out_file, shift, factor)
                else:
                    total += replay(body, origin, out_file, args.speed, shift,
                                    args.quiet, factor)

                if pull + 1 < args.loop:
                    # Next pull picks up after this one's last line plus the
                    # quiet gap, so the sequence reads as one raid night.
                    shift += span + timedelta(seconds=args.gap)
                    # Real seconds between dumps, so the app's 2s poll has time
                    # to notice each pull separately rather than seeing them
                    # all land as one block of bytes.
                    time.sleep(args.gap if args.dump
                               else args.gap / args.speed)
    except KeyboardInterrupt:
        print(f"\nStopped after {total} lines. {out_path} kept.")
        return

    print(f"\nDone: {total} lines written to {out_path}")


if __name__ == "__main__":
    main()
