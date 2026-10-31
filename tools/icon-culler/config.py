"""Configuration for icon culler."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from db2_layouts import DEFAULT_VERSION, default_db2_dir

# Target instance IDs for Midnight Season 2 (journalinstance ids, not uimap ids).
# Rotate these when the season changes: they decide which boss abilities keep their
# icons in the culled pack. Look ids up by name in the journalinstance export.
TARGET_INSTANCES = {
    # Midnight Dungeons
    1304,  # Murder Row
    1309,  # The Blinding Vale
    1311,  # Den of Nalorakk
    1313,  # Voidscar Arena
    1322,  # Altar of Fangs (added in 12.1)

    # Returning Dungeons
    1030,  # Temple of Sethraliss (Battle for Azeroth)
    1041,  # Kings' Rest (Battle for Azeroth)
    1202,  # Ruby Life Pools (Dragonflight)

    # Raids
    1320,  # The Venomous Abyss
}

# Spell ID threshold for "recent" spells (TWW/Midnight additions)
RECENT_SPELL_THRESHOLD = 1200000

VERSION = DEFAULT_VERSION

# Paths, derived from this file's location so the tools run from any checkout.
# repo_root/tools/icon-culler -> repo_root
SCRIPT_DIR = Path(__file__).resolve().parent
ROOT_DIR = SCRIPT_DIR.parent.parent

# Where the DB2 text exports live, defaulting to this version's dump. Override
# with the AWOWLAB_DB2_DIR environment variable to read one from elsewhere.
# Either {name}.txt or {name}_export.txt is accepted; use find_export() from
# db2_layouts rather than joining a filename yourself.
DB2_DIR = str(default_db2_dir(VERSION))

WCL_CACHE_DIR = str(ROOT_DIR / 'out' / 'build' / 'x64-debug' / 'AwowLab' / 'Debug' / 'wcl_cache')
SPELL_DATA_BIN = str(ROOT_DIR / 'AwowLab' / 'data' / f'spell_data_{VERSION}.bin')
# Written by tools/spell-icon-generator/generate_spell_icon_table.py
ICON_TABLE_CSV = str(ROOT_DIR / 'tools' / 'spell-icon-generator' / 'output' / 'spell_icon_table.csv')
SOURCE_ICONS_DIR = str(ROOT_DIR / 'AwowLab' / 'PNG' / 'Spell Icons')
OUTPUT_DIR = str(SCRIPT_DIR / 'output')
REQUIRED_ICONS_FILE = str(SCRIPT_DIR / 'required_icons.txt')
