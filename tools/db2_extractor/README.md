# DB2 Cooldown Extraction

Turns WoW DB2 exports into the spell cooldown data AwowLab loads at runtime, and
works out which talents modify which cooldowns by matching spell family masks -
so a patch costs a rerun rather than hours of manual data entry.

Scripts here, besides the hero talent builder described in `docs/UPDATE-FLOW.md`:

| Script | Does |
|---|---|
| `build_cooldown_tables.py` | Parses the spell exports and writes `spell_data_{version}.bin` |
| `build_talent_mapping.py` | Adds WarcraftLogs-compatible talent IDs to `cooldown_modifiers_{version}.json` |
| `build_haste_buffs.py` | Writes `haste_buffs_{version}.json`, the percentage haste buffs the haste tracker applies |

For the whole update pipeline (converting `.db2`, map rebuilds, icons), see
[docs/UPDATE-FLOW.md](../../docs/UPDATE-FLOW.md). This file covers these two scripts.

## Prerequisites

- Python 3.7+
- DB2 **text exports** (not raw `.db2`) - see [docs/UPDATE-FLOW.md](../../docs/UPDATE-FLOW.md)
  for how to produce them. Either `{name}.txt` or `{name}_export.txt` works.
- `spell-data-generator` must run **first**: it produces the per-class CSVs that
  are the primary GCD source. `build_cooldown_tables.py` exits with an error if
  they are missing.

## build_cooldown_tables.py

```bash
# spell-data-generator first, then:
python build_cooldown_tables.py <db2_txt_dir> [output_dir] [spell_gen_output_dir] [--version 12.1]
```

`--version` defaults to `DEFAULT_VERSION` at the top of the script. It names the
output files **and** selects which column layout the exports are checked against.
With no arguments the script prompts for the paths.

Reads: `spellcooldowns`, `spellcategory`, `spellcategories`, `spellclassoptions`,
`spelleffect`, `spelllabel`, `spellpower`, `spell`, `spellname`, `traitdefinition`,
`traitnodeentry`.

Writes to `output/`:

| File | Loaded by the app? |
|---|---|
| `spell_data_{version}.bin` | **Yes** - copy to `AwowLab/data/` |
| `spell_data_{version}.csv` | No - for inspection |
| `spell_data_sample_{version}.json` | No - for eyeballing a run |

Binary format: `[uint32 count]` then records of 20 bytes sorted by spell id.
See `AwowLab/Database/SpellDataTable.cpp` for the layout it must match.

### Layout checks

The exports are positional dumps with no column names, so a column added or
reordered upstream shifts every later index and **still parses** - producing wrong
cooldowns rather than an error. Each export's `Layout:` hash is therefore checked
against `tools/db2_layouts.json` on open, and a mismatch stops the run naming the
table and both hashes. If that fires, remap that table's column indices, then
record the new hash. `python tools/db2_layouts.py --diff 12.0 12.1` lists what
changed between two versions.

## build_talent_mapping.py

```bash
python build_talent_mapping.py <db2_txt_dir>
```

WarcraftLogs reports talents by trait node entry ID, the game reports spell IDs.
This reads `traitnodeentry_export.txt` to map between them and rewrites
`AwowLab/data/cooldown_modifiers_{version}.json` **in place** with the WCL ids
added, plus `output/talent_id_mapping.json` for reference.

`cooldown_modifiers_{version}.json` is the file the app actually loads for talent
modifiers (`CooldownModifierRegistry::loadFromJson`). It is hand-maintainable -
this script only enriches it.

## How talent matching works

WoW groups related spells with 4-element family bitmasks. A talent effect declares
a target mask; any spell whose mask overlaps it is affected:

```python
def _masks_match(mask1, mask2):
    """Bitwise AND - if any element overlaps, the masks match"""
    return any(m1 & m2 for m1, m2 in zip(mask1, mask2))
```

Worked example - Survival of the Fittest reducing Barkskin:

```
spelleffect:         effect 300978 belongs to spell 203965 (Survival of the Fittest)
                     type 11 (modifies cooldown), value -12, target mask [0,262144,0,0]
spellclassoptions:   spell 22812 (Barkskin) has mask [0,262144,0,0]
match:               262144 & 262144 -> non-zero
result:              with the talent, Barkskin's cooldown drops 12%
```

Cooldown-relevant effect types:

- **11** - Apply Aura: modifies cooldown
- **1469** - Mod cooldown percent

Modifier types in the output: `0` flat, `1` percent, `2` resource-based,
`3` charge-based, `4` spell replacement.

## Consumers

- `AwowLab/Database/SpellDataTable.cpp` - reads `spell_data_{version}.bin`
- `AwowLab/Database/CooldownModifiers/CooldownModifierRegistry.cpp` - reads
  `cooldown_modifiers_{version}.json`
- `AwowLab/Database/CooldownCalculator.cpp` - combines player talents from
  COMBATANT_INFO with the registry
- `AwowLab/Core/EncounterDataLoader.cpp`, `AwowLab/Core/Application.cpp` - wire it up

The app picks the version at runtime from the `spellDataVersion` setting
(`Core/UnifiedSettings.h`) and builds filenames by concatenation, so a version with
no matching data files in `AwowLab/data/` degrades silently to no cooldown data.

## Troubleshooting

**"this table's shape changed"** - a DB2 column moved. Do not bypass it; the parse
would produce wrong numbers. Remap the column indices for that table and record
the new hash in `tools/db2_layouts.json`.

**"no export found"** - the export is missing from the directory under either
naming convention. Check the conversion step actually produced it.

**"spell-data-generator output not found"** - run `tools/spell-data-generator`
first; it is the primary GCD source.

**Values look wrong but nothing failed** - check the layout hash for that table
first, then verify a few spells against Wowhead. Large cooldowns (e.g. 2000+
seconds) are legitimate for some spells.
