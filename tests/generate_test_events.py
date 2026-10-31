"""Sample up to ten lines of each event type from a combat log into testevents.txt.

usage: generate_test_events.py <WoWCombatLog.txt> [output]
The output defaults to testevents.txt beside this script.
"""
import os
import random
import sys

if len(sys.argv) not in (2, 3):
    print(__doc__)
    sys.exit(2)

log_path = sys.argv[1]
output_path = sys.argv[2] if len(sys.argv) == 3 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "testevents.txt")

events_by_type = {}

with open(log_path, 'r', encoding='utf-8') as f:
    for line_num, line in enumerate(f):
        line = line.rstrip('\r\n')  # Remove line endings but keep content
        if not line:
            continue
        # Strip surrounding quotes if present (some WoW logs have these)
        if line.startswith('"') and line.endswith('"'):
            line = line[1:-1]
        # Find the event type (after the double-space, before the first comma)
        parts = line.split('  ', 1)  # Split on double-space (timestamp separator)
        if len(parts) < 2:
            continue
        after_timestamp = parts[1]
        comma_pos = after_timestamp.find(',')
        if comma_pos == -1:
            event_type = after_timestamp
        else:
            event_type = after_timestamp[:comma_pos]

        if event_type not in events_by_type:
            events_by_type[event_type] = []
        events_by_type[event_type].append(line)

# Now select up to 10 random samples per event type, spread throughout the file
output_lines = []

for event_type in sorted(events_by_type.keys()):
    events = events_by_type[event_type]
    if len(events) <= 10:
        # Take all if we have 10 or fewer
        output_lines.extend(events)
    else:
        # Stratified sampling: divide into 10 chunks and pick 1 from each
        chunk_size = len(events) // 10
        for i in range(10):
            start = i * chunk_size
            end = start + chunk_size if i < 9 else len(events)
            idx = random.randint(start, end - 1)
            output_lines.append(events[idx])

# Write to file - raw lines with Unix line endings
with open(output_path, 'w', encoding='utf-8', newline='\n') as f:
    for line in output_lines:
        f.write(line + '\n')

print(f'Wrote {len(output_lines)} test events to {output_path}')
print(f'Event types found: {len(events_by_type)}')
for et in sorted(events_by_type.keys()):
    print(f'  {et}: {len(events_by_type[et])} events -> {min(10, len(events_by_type[et]))} samples')
