"""Answer action-list terms from an analysis package.

Builds a per-fight timeline from auras.csv, casts.csv and combatant.json, then
answers a condition's terms at the instant of a given cast. Anything the tables
do not carry returns UNKNOWN rather than a guess - `demonsurge_available` and
`variable.*` have no equivalent in a log, and a guard resting on them must be
declined, not approximated.
"""

from __future__ import annotations
import bisect
import collections
import csv
import io
import json
import re
import zipfile

from condition import UNKNOWN


def _rows(zf, fight_key, table):
    raw = zf.read('fights/%s/%s.csv' % (fight_key, table)).decode('utf-8')
    return list(csv.DictReader(io.StringIO(raw)))


class FightState:
    """One fight's timeline, queried at an instant."""

    def __init__(self, zf, fight, spell_ids=None, talent_names=None):
        self.guid = fight['player_guid']
        self.spell_ids = spell_ids or {}          # apl name -> {spell id, ...}
        self.talents = set(talent_names or ())
        # The list writes a tree with nothing between the words - "felscarred",
        # not "fel_scarred" - so the manifest's "Fel-Scarred" has to lose its
        # separators entirely rather than turn them into underscores.
        self.hero_tree = re.sub(r'[^a-z0-9]', '',
                                (fight.get('hero_tree') or '').lower())

        # Buff and debuff windows, by lowercase snake_case name.
        self.buff = collections.defaultdict(list)
        self.debuff = collections.defaultdict(list)
        self.stacks = collections.defaultdict(list)
        open_at = {}
        for r in _rows(zf, fight['fight_key'], 'auras'):
            name = _snake(r['spell_name'])
            if not name:
                continue
            mine = r['target_guid'] == self.guid
            book = self.buff if mine else self.debuff
            t, event = int(r['timestamp_ms']), r['event_type']
            key = (id(book), name)
            if event == 'applied':
                open_at[key] = t
            elif event == 'removed' and key in open_at:
                book[name].append((open_at.pop(key), t))
            if r['stacks']:
                try:
                    self.stacks[name].append((t, 0 if event == 'removed'
                                              else int(r['stacks'])))
                except ValueError:
                    pass
        for book in (self.buff, self.debuff):
            for windows in book.values():
                windows.sort()
        for series in self.stacks.values():
            series.sort()

        # Cast times per apl action name, for cooldown.<x>.remains.
        self.casts = collections.defaultdict(list)
        for r in _rows(zf, fight['fight_key'], 'casts'):
            if r['actor_guid'] != self.guid:
                continue
            for action, ids in self.spell_ids.items():
                if r['spell_id'] in ids:
                    self.casts[action].append(int(r['end_ms']))
        for times in self.casts.values():
            times.sort()

    # -- queries --------------------------------------------------------

    def _up(self, book, name, t):
        windows = book.get(name)
        if windows is None:
            return UNKNOWN            # never seen: cannot say it was down
        starts = [s for s, _ in windows]
        i = bisect.bisect_right(starts, t) - 1
        return i >= 0 and windows[i][0] <= t < windows[i][1]

    def _stack(self, name, t):
        series = self.stacks.get(name)
        if series is None:
            return UNKNOWN
        times = [x for x, _ in series]
        i = bisect.bisect_right(times, t) - 1
        return series[i][1] if i >= 0 else 0

    def resolve(self, term, t):
        parts = term.split('.')
        head = parts[0]

        if head in ('buff', 'debuff') and len(parts) >= 3:
            book = self.buff if head == 'buff' else self.debuff
            name, field = parts[1], parts[2]
            if field == 'up':
                return self._up(book, name, t)
            if field == 'down':
                up = self._up(book, name, t)
                return UNKNOWN if up is UNKNOWN else (not up)
            if field == 'stack':
                return self._stack(name, t)
            return UNKNOWN            # remains, duration: not tracked

        if head == 'talent' and len(parts) >= 2:
            # Only answerable when a name table was supplied. The package
            # carries trait node entry ids (112823 and such), not spell ids, so
            # lookups.json cannot name them and hero_talents covers only the
            # hero trees. Without names, absence is unknown rather than false -
            # answering false would mark every talent-gated line violated.
            if not self.talents:
                return UNKNOWN
            return parts[1] in self.talents

        if head == 'hero_tree' and len(parts) >= 2:
            return parts[1] == self.hero_tree

        if head == 'cooldown' and len(parts) >= 3:
            action, field = parts[1], parts[2]
            times = self.casts.get(action)
            if not times:
                return UNKNOWN
            i = bisect.bisect_left(times, t)
            if field == 'remains':
                # Time until the next cast: a proxy for cooldown remaining that
                # is right whenever the ability is used as soon as it is ready.
                return (times[i] - t) / 1000.0 if i < len(times) else UNKNOWN
            if field == 'up':
                nxt = (times[i] - t) / 1000.0 if i < len(times) else None
                return UNKNOWN if nxt is None else nxt < 1.0
            return UNKNOWN

        # Demonsurge belongs to Fel-Scarred, so on any other hero tree these are
        # false rather than unknown: the build cannot have the mechanic. Saying
        # unknown here threw away every cast of the abilities whose Fel-Scarred
        # lines an Aldrachi build simply falls through - 2281 Annihilations,
        # 1018 Death Sweeps and 97 Metamorphoses in one sample.
        if 'demonsurge' in term:
            if self.hero_tree and self.hero_tree != 'felscarred':
                return False
            return UNKNOWN

        # Everything else - variable.*, gcd.*, fury, active_enemies,
        # spell_targets, charges - has no equivalent in a log, or none worth
        # faking.
        return UNKNOWN


def _snake(name):
    if not name:
        return ''
    out = re.sub(r"[^a-z0-9]+", '_', name.lower())
    return out.strip('_')


def load_package(path):
    zf = zipfile.ZipFile(path)
    manifest = json.loads(zf.read('manifest.json'))
    return zf, manifest


def talents_for(zf, fight, talent_names_by_id):
    """Talent names a build took, from combatant.json plus an id->name table."""
    try:
        data = json.loads(zf.read('fights/%s/combatant.json' % fight['fight_key']))
    except KeyError:
        return set()
    for c in data.get('combatants', []):
        if c['player_guid'] == fight['player_guid']:
            return {talent_names_by_id[t['spell_id']]
                    for t in c.get('talents', [])
                    if t['spell_id'] in talent_names_by_id}
    return set()
