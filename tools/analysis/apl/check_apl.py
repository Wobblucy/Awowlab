"""Check an analysis package against a SimulationCraft action priority list.

For every cast of an action the list names, evaluates that action's whole
condition at the instant of the cast. A condition that evaluates false means the
cast happened while the list said not to; a condition that cannot be fully
evaluated is counted separately and never reported as a violation.

    python check_apl.py <package.zip> <spec.simc> [--hero "Aldrachi Reaver"]

Why the whole condition: pulling one clause out of a conjunction measures a rule
that was never in force. `annihilation` is gated on three clauses at once, and
testing the middle one alone reported a 23% violation rate for a line that never
fires at all.
"""

from __future__ import annotations
import argparse
import collections
import csv
import io
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import condition as C
import logstate


def read_apl(path):
    """The action lines, in priority order, as (name, condition) pairs."""
    out = []
    for line in open(path, encoding='utf-8'):
        line = line.strip()
        if not line.startswith('actions'):
            continue
        if '/' not in line:
            continue
        body = line.split('/', 1)[1]
        name = body.split(',')[0]
        cond = body.split(',if=', 1)[1] if ',if=' in body else ''
        out.append((name, cond))
    return out


def spell_ids_from_package(zf, manifest):
    """Map an apl action name to the spell ids that represent a *press*.

    One ability often arrives under several ids - a press, a proc that casts it,
    a per-target trigger - and they are not interchangeable. Throw Glaive shows
    up as 1615 presses alongside 4882 and 5210 trigger rows; testing all three
    against the press condition reported every one of 11707 as a violation.

    A press occupies the global cooldown, so its casts are spaced. A trigger
    fires beside its parent, so its median gap collapses toward zero. Keeping
    the ids whose median gap is at least half a GCD separates them without a
    hardcoded table, and an ability with a single id is unaffected.
    """
    times = collections.defaultdict(lambda: collections.defaultdict(list))
    for fight in manifest['fights']:
        raw = zf.read('fights/%s/casts.csv' % fight['fight_key']).decode('utf-8')
        for r in csv.DictReader(io.StringIO(raw)):
            if r['actor_guid'] != fight['player_guid']:
                continue
            name = logstate._snake(r['spell_name'])
            times[name][r['spell_id']].append(int(r['end_ms']))

    MIN_GAP_MS = 700          # half a GCD at raiding haste
    ids = collections.defaultdict(set)
    for name, by_id in times.items():
        spaced = {}
        for spell_id, stamps in by_id.items():
            stamps.sort()
            gaps = sorted(stamps[i] - stamps[i - 1] for i in range(1, len(stamps)))
            median = gaps[len(gaps) // 2] if gaps else MIN_GAP_MS
            spaced[spell_id] = median
        keep = {i for i, gap in spaced.items() if gap >= MIN_GAP_MS}
        # If nothing qualifies, the ability is rare enough that gaps say
        # nothing; keep the least frequent id rather than dropping it entirely.
        ids[name] = keep or {min(by_id, key=lambda i: len(by_id[i]))}
    return ids


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('package')
    ap.add_argument('apl')
    ap.add_argument('--hero', default=None,
                    help='only fights of this hero talent tree')
    ap.add_argument('--verbose', action='store_true')
    args = ap.parse_args()

    zf, manifest = logstate.load_package(args.package)
    fights = manifest['fights']
    if args.hero:
        fights = [f for f in fights if (f.get('hero_tree') or '') == args.hero]
    if not fights:
        sys.exit('no fights match')

    by_name = spell_ids_from_package(zf, manifest)
    actions = read_apl(args.apl)

    # Parse every condition once. A condition that will not parse is reported
    # rather than skipped silently.
    #
    # An action often appears on several lines: a gated one early and a bare one
    # later. The list tries them in order, so a cast is prescribed if ANY line
    # matches - and an action with a bare line can never be a violation, because
    # the list always permits it eventually. Dropping bare lines here counted
    # every Annihilation that fell past the Fel-Scarred line as a violation,
    # which is 79% of them.
    parsed, unparsed, unconditional = {}, {}, set()
    for name, cond in actions:
        if not cond:
            unconditional.add(name)
            continue
        try:
            parsed.setdefault(name, []).append((cond, C.parse(cond)))
        except ValueError as exc:
            unparsed[name] = str(exc)

    stats = collections.defaultdict(lambda: collections.Counter())
    unresolved = collections.defaultdict(collections.Counter)

    for fight in fights:
        state = logstate.FightState(zf, fight, spell_ids=by_name)
        raw = zf.read('fights/%s/casts.csv' % fight['fight_key']).decode('utf-8')
        for r in csv.DictReader(io.StringIO(raw)):
            if r['actor_guid'] != fight['player_guid']:
                continue
            action = logstate._snake(r['spell_name'])
            if action not in parsed and action not in unconditional:
                continue
            # Only a press is judged against the condition. The proc and
            # trigger ids sharing this name were never a decision.
            if r['spell_id'] not in by_name.get(action, ()):
                continue
            t = int(r['end_ms'])

            # A cast satisfies the list if ANY line for that action is true:
            # the list tries them in order and the first match fires.
            verdicts = []
            for cond, tree in parsed.get(action, ()):
                seen = set()

                def resolve(term, _t=t, _s=state, _seen=seen):
                    value = _s.resolve(term, _t)
                    if value is C.UNKNOWN:
                        _seen.add(term)
                    return value

                verdicts.append(C.truth(C.evaluate(tree, resolve)))
                if verdicts[-1] is C.UNKNOWN:
                    for term in seen:
                        unresolved[action][term] += 1

            if any(v is True for v in verdicts) or action in unconditional:
                stats[action]['satisfied'] += 1
            elif all(v is False for v in verdicts):
                stats[action]['violated'] += 1
            else:
                stats[action]['undecidable'] += 1

    print('package : %s' % os.path.basename(args.package))
    print('apl     : %s' % os.path.basename(args.apl))
    print('fights  : %d%s' % (len(fights), ' (%s)' % args.hero if args.hero else ''))
    print()
    print('%-22s %9s %9s %12s  %s' %
          ('action', 'satisfied', 'violated', 'undecidable', 'rate'))
    for action in sorted(stats, key=lambda a: -sum(stats[a].values())):
        s = stats[action]
        decided = s['satisfied'] + s['violated']
        rate = ('%5.1f%%' % (100.0 * s['violated'] / decided)) if decided else '    --'
        print('%-22s %9d %9d %12d  %s' %
              (action[:22], s['satisfied'], s['violated'], s['undecidable'], rate))

    if unresolved:
        print()
        print('terms the log cannot answer (these make a cast undecidable):')
        for action in sorted(unresolved):
            worst = unresolved[action].most_common(3)
            print('  %-20s %s' % (action[:20],
                                  ', '.join('%s (%d)' % (t, n) for t, n in worst)))

    if unparsed:
        print()
        print('conditions that would not parse:')
        for name, why in unparsed.items():
            print('  %-20s %s' % (name[:20], why))


if __name__ == '__main__':
    main()
