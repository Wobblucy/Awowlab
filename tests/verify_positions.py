#!/usr/bin/env python3
"""
Verify position parsing token indices by parsing raw combat log data.
This tests that our token indices (28, 29 for X, Y in SPELL events) are correct.
"""

import re
import sys

def tokenize_line(line):
    """Tokenize a WoW combat log line similar to the C++ tokenizer."""
    tokens = []

    # First split by double-space to separate timestamp from event
    parts = line.split('  ', 1)
    if len(parts) != 2:
        return tokens

    # Date and time
    date_time = parts[0].split(' ')
    if len(date_time) == 2:
        tokens.append(date_time[0])  # Date
        tokens.append(date_time[1])  # Time

    # Rest of the line - split by comma, handling quotes
    rest = parts[1]
    current_token = ""
    in_quote = False

    for char in rest:
        if char == '"':
            in_quote = not in_quote
        elif char == ',' and not in_quote:
            if current_token:
                tokens.append(current_token)
            current_token = ""
        else:
            current_token += char

    if current_token:
        tokens.append(current_token)

    return tokens

def verify_spell_cast_success(filepath, max_events=20):
    """Verify SPELL_CAST_SUCCESS position parsing."""
    print("=== SPELL_CAST_SUCCESS Position Verification ===\n")

    count = 0
    valid = 0
    invalid = 0

    with open(filepath, 'r', encoding='utf-8') as f:
        for line in f:
            if 'SPELL_CAST_SUCCESS' not in line:
                continue

            tokens = tokenize_line(line.strip())
            if len(tokens) < 33:
                continue

            # According to our fix:
            # Position indices for SPELL events: 28(x), 29(y), 30(map), 31(facing), 32(level)
            # InfoGUID is at token 14

            try:
                info_guid = tokens[14] if len(tokens) > 14 else "N/A"

                # OLD indices (wrong): 26, 27
                old_x = tokens[26] if len(tokens) > 26 else "N/A"
                old_y = tokens[27] if len(tokens) > 27 else "N/A"

                # NEW indices (correct): 28, 29
                new_x = tokens[28] if len(tokens) > 28 else "N/A"
                new_y = tokens[29] if len(tokens) > 29 else "N/A"
                new_map = tokens[30] if len(tokens) > 30 else "N/A"

                # Parse float values
                try:
                    new_x_f = float(new_x)
                    new_y_f = float(new_y)
                    new_map_i = int(new_map)

                    # Valid WoW coordinates are typically 0-5000
                    is_valid = (0 < abs(new_x_f) < 10000 and
                               0 < abs(new_y_f) < 10000 and
                               new_map_i > 0)

                    if is_valid:
                        valid += 1
                    else:
                        invalid += 1

                    if count < max_events:
                        spell_name = tokens[12] if len(tokens) > 12 else "?"
                        source = tokens[4] if len(tokens) > 4 else "?"
                        print(f"Event {count + 1}: {spell_name} by {source}")
                        print(f"  InfoGUID: {info_guid}")
                        print(f"  OLD indices[26,27]: ({old_x}, {old_y})")
                        print(f"  NEW indices[28,29]: ({new_x}, {new_y}) map={new_map}")
                        print(f"  Status: {'VALID' if is_valid else 'INVALID'}")
                        print()

                except ValueError:
                    invalid += 1
                    if count < max_events:
                        print(f"Event {count + 1}: Parse error - new_x={new_x}, new_y={new_y}")
                        print()

                count += 1

            except Exception as e:
                print(f"Error processing line: {e}")
                continue

            if count >= 100:  # Check first 100 events
                break

    print(f"\n=== Summary ===")
    print(f"Total SPELL_CAST_SUCCESS events checked: {count}")
    print(f"Valid positions: {valid}")
    print(f"Invalid positions: {invalid}")

    return valid, invalid

def verify_spell_damage(filepath, max_events=20):
    """Verify SPELL_DAMAGE position parsing."""
    print("\n=== SPELL_DAMAGE Position Verification ===\n")

    count = 0
    valid = 0
    invalid = 0

    with open(filepath, 'r', encoding='utf-8') as f:
        for line in f:
            if 'SPELL_DAMAGE,' not in line:  # Exact match to avoid SPELL_DAMAGE_SUPPORT
                continue
            if 'SPELL_DAMAGE_SUPPORT' in line:
                continue

            tokens = tokenize_line(line.strip())
            if len(tokens) < 40:  # Minimum tokens for SPELL_DAMAGE with position
                continue

            try:
                info_guid = tokens[14] if len(tokens) > 14 else "N/A"

                # NEW indices (correct): 28, 29, 30, 31
                new_x = tokens[28] if len(tokens) > 28 else "N/A"
                new_y = tokens[29] if len(tokens) > 29 else "N/A"
                new_map = tokens[30] if len(tokens) > 30 else "N/A"

                # Damage suffix should now be at 33+
                damage_amount = tokens[33] if len(tokens) > 33 else "N/A"

                try:
                    new_x_f = float(new_x)
                    new_y_f = float(new_y)
                    new_map_i = int(new_map)
                    damage_i = int(damage_amount)

                    is_valid = (0 < abs(new_x_f) < 10000 and
                               0 < abs(new_y_f) < 10000 and
                               new_map_i > 0 and
                               damage_i > 0)

                    if is_valid:
                        valid += 1
                    else:
                        invalid += 1

                    if count < max_events:
                        spell_name = tokens[12] if len(tokens) > 12 else "?"
                        source = tokens[4] if len(tokens) > 4 else "?"
                        dest = tokens[8] if len(tokens) > 8 else "?"
                        print(f"Event {count + 1}: {spell_name} by {source} -> {dest}")
                        print(f"  InfoGUID: {info_guid}")
                        print(f"  Position[28,29,30]: ({new_x}, {new_y}) map={new_map}")
                        print(f"  Damage[33]: {damage_amount}")
                        print(f"  Status: {'VALID' if is_valid else 'INVALID'}")
                        print()

                except ValueError as e:
                    invalid += 1
                    if count < max_events:
                        print(f"Event {count + 1}: Parse error - {e}")
                        print()

                count += 1

            except Exception as e:
                print(f"Error: {e}")
                continue

            if count >= 100:
                break

    print(f"\n=== Summary ===")
    print(f"Total SPELL_DAMAGE events checked: {count}")
    print(f"Valid positions: {valid}")
    print(f"Invalid positions: {invalid}")

    return valid, invalid

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <WoWCombatLog.txt>")
        sys.exit(2)
    filepath = sys.argv[1]

    print(f"Testing file: {filepath}\n")

    cast_valid, cast_invalid = verify_spell_cast_success(filepath)
    damage_valid, damage_invalid = verify_spell_damage(filepath)

    print("\n" + "="*50)
    print("FINAL RESULTS")
    print("="*50)

    total_valid = cast_valid + damage_valid
    total_invalid = cast_invalid + damage_invalid

    if total_invalid == 0 and total_valid > 0:
        print("\n*** SUCCESS: All positions parsed correctly! ***")
        sys.exit(0)
    elif total_invalid > total_valid:
        print("\n*** FAILURE: Position parsing has issues! ***")
        sys.exit(1)
    else:
        print(f"\n*** PARTIAL: {total_valid} valid, {total_invalid} invalid ***")
        sys.exit(0)
