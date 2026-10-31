#!/usr/bin/env python3
"""
Build Cooldown Tables from DB2 Files
=====================================

This script extracts spell cooldown data and talent cooldown modifiers from World of Warcraft
DB2 database export files. It generates binary tables that AwowLab uses for accurate cooldown
tracking during combat log replay.

WORKFLOW OVERVIEW
-----------------
1. Parse DB2 export files from WoW game data (extracted via tools like wow.tools)
2. Extract base spell cooldowns, GCDs, and cast times
3. Extract charge-based spell data (spells with multiple charges like Roll, Frenzied Regen)
4. Detect talent modifiers that affect spell cooldowns:
   - Flat reductions (e.g., "reduces cooldown by 15 seconds")
   - Percent reductions (e.g., "reduces cooldown by 20%")
   - Resource-based reductions (e.g., "every 30 Rage spent reduces CD by 1 sec")
   - Charge grants (e.g., "grants an additional charge")
5. Generate binary output files for efficient runtime loading

DB2 FILES USED
--------------
The script parses these DB2 export files (from WoW's client database):

  spellcooldowns_export.txt    - Base cooldown values for spells
  spellcategory_export.txt     - Category definitions with charge counts and recharge times
  spellcategories_export.txt   - Links spells to their charge categories
  spellclassoptions_export.txt - Spell family masks (used for effect targeting)
  spelleffect_export.txt       - Spell effects including cooldown modifiers
  spelllabel_export.txt        - Label-to-spell mappings (for label-based targeting)
  spellpower_export.txt        - Power costs (mana, rage, energy, etc.)
  spell_export.txt             - Spell descriptions (for pattern matching)
  spellname_export.txt         - Spell names
  traitdefinition_export.txt   - Talent definition -> spell ID mapping
  traitnodeentry_export.txt    - WarcraftLogs talent entry ID -> definition ID mapping

EFFECT TYPES FOR COOLDOWN MODIFIERS
-----------------------------------
WoW uses different effect types for cooldown modifications:

  Type 4:    Dummy Aura - Contains metadata values for resource-based modifiers
  Type 11:   Apply Aura: Mod Cooldown (percent) - Uses family mask targeting
  Type 219:  Apply Aura: Add Modifier - Flat (Label) - Uses label-based targeting
             *** NOT YET IMPLEMENTED - See TODO below ***
  Type 411:  Add Spell Charges - Grants additional charges to charge-based spells
  Type 1469: Mod Cooldown Haste (variant of type 11)

TARGETING MECHANISMS
--------------------
Cooldown modifiers use two different targeting mechanisms:

1. Family Mask Matching (Types 11, 1469):
   - Each spell has a "family" (class-specific) and a 128-bit mask
   - Effects store a target mask and match against spell masks via bitwise AND
   - Example: Druid spells have family=7, and effects target specific spells via mask bits

2. Label-Based Targeting (Type 219):
   - Effects store a [modifier_type, label_id] array
   - modifier_type 11 = cooldown modification
   - Labels are assigned to spells via spelllabel table
   - Example: Reaper's Mark has label 3243, Reaper's Onslaught targets label 3243

   *** TODO: Type 219 label-based modifiers are NOT YET EXTRACTED ***
   *** This causes flat CD reductions like Reaper's Onslaught to be missed ***

OUTPUT FILES
------------
  spell_data_{version}.bin      - Binary table of spell cooldown/GCD data (20 bytes/record)
  spell_data_{version}.csv      - The same data as CSV, for inspection
  spell_data_sample_{version}.json - A small sample, for eyeballing a run

Only the .bin is loaded by the app; copy it to AwowLab/data/. The talent modifiers
the app reads come from cooldown_modifiers_{version}.json, which build_talent_mapping.py
maintains - this script no longer emits a competing binary copy of them.

WCL COMPATIBILITY
-----------------
WarcraftLogs uses different talent IDs than the game client:
- Game client uses spell_id from the talent ability
- WarcraftLogs uses entry_id from traitnodeentry table

This script builds a mapping chain:
  WCL entry_id -> definition_id -> spell_id

Each modifier is written to the binary file multiple times:
- Once with spell_id (for local log parsing)
- Once for each WCL entry_id (for WarcraftLogs parsing)
"""

import struct
import os
import re
import json
import sys
from dataclasses import dataclass
from typing import List, Dict, Tuple, Set, Optional

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from db2_layouts import (find_export, verify_layout, LayoutMismatch, DEFAULT_VERSION,
                         DB2_DIR_ENV, default_db2_dir, resolve_db2_dir)


# ============================================================================
# Data Classes
# ============================================================================
#
# These dataclasses represent the structured data extracted from DB2 files.
# They serve as intermediate representations before writing to binary output.
# ============================================================================

@dataclass
class SpellData:
    """
    Spell cooldown/GCD/haste data.

    Represents the core timing data for a spell that AwowLab needs for
    accurate cooldown tracking and GCD visualization.

    Attributes:
        spell_id: Unique spell identifier from game data
        gcd_ms: Global cooldown in milliseconds (typically 1500ms base)
        cooldown_ms: Spell cooldown in milliseconds (0 = no cooldown)
        cast_time_ms: Cast time in milliseconds (0 = instant)
        gcd_haste_affected: True if GCD scales with haste (most spells)
        cd_haste_affected: True if cooldown scales with haste (rare, e.g., Keg Smash)
        base_charges: Number of charges for charge-based spells (0 = not charge-based)
                     From SpellCategory table. Examples: Roll has 2 charges,
                     Survival of the Fittest has 1 charge (can be modified by talents)
    """
    spell_id: int
    gcd_ms: int
    cooldown_ms: int
    cast_time_ms: int
    gcd_haste_affected: bool
    cd_haste_affected: bool
    base_charges: int = 0


@dataclass
class SpellClassMask:
    """
    Spell family mask for matching effects to spells.

    WoW uses a 128-bit mask (4 x 32-bit integers) to group spells by class
    and allow effects to target multiple related spells. Effects that modify
    cooldowns use these masks to specify which spells they affect.

    Attributes:
        spell_id: The spell this mask belongs to
        family: Class family ID (e.g., 7=Druid, 6=Death Knight, 15=Paladin)
        mask: 4-tuple of 32-bit integers forming a 128-bit bitmask.
              Each spell has specific bits set, and effects target spells
              by having overlapping bits in their target mask.

    Example:
        Frenzied Regeneration (spell 22842) might have mask [0, 2048, 0, 16777216]
        A talent effect targeting bit 11 of mask[1] would match this spell.
    """
    spell_id: int
    family: int
    mask: Tuple[int, int, int, int]


@dataclass
class CooldownEffect:
    """
    Spell effect that modifies cooldowns.

    Represents a cooldown-modifying effect extracted from spelleffect table.
    These effects use family mask targeting (types 11, 1469) to specify
    which spells they modify.

    Attributes:
        effect_id: Unique effect ID from spelleffect table
        spell_id: The talent/spell that provides this effect
        effect_type: Effect type ID:
                     11 = Apply Aura: Mod Cooldown (percent, family mask)
                     1469 = Mod Cooldown Haste variant
                     Note: Type 219 (flat, label-based) NOT YET SUPPORTED
        value: The modifier value (percent reduction, e.g., -20 for 20% reduction)
        target_mask: 4-tuple bitmask specifying which spells this affects.
                    Matches against SpellClassMask.mask via bitwise AND.
    """
    effect_id: int
    spell_id: int
    effect_type: int
    value: float
    target_mask: Tuple[int, int, int, int]


@dataclass
class TalentModifier:
    """
    Talent cooldown modifier - the final output representation.

    Represents a talent that modifies another spell's cooldown.
    Written to binary output and JSON reference files.

    Attributes:
        talent_spell_id: The talent spell ID that provides this modifier.
                        For WCL compatibility, this may be a WCL entry_id
                        instead of the actual spell_id.
        modified_spell_id: The spell whose cooldown is modified
        modifier_type: Type of modification:
                      0 = FlatReduction (reduces by X milliseconds)
                      1 = PercentReduction (reduces by X percent)
                      2 = ResourceBased (reduces per resource spent)
                      3 = ChargeBased (grants additional charges)
                      4 = ReplaceSpell (replaces spell entirely)
        value: The modifier value. Interpretation depends on modifier_type:
              - FlatReduction: milliseconds to reduce
              - PercentReduction: percent to reduce (e.g., 20 for 20%)
              - ResourceBased: resource per second for 1 sec reduction
              - ChargeBased: number of additional charges
              - ReplaceSpell: new spell ID
    """
    talent_spell_id: int
    modified_spell_id: int
    modifier_type: int
    value: int


# ============================================================================
# Binary Format Specifications
# ============================================================================
#
# These structs define the binary format for output files. The C++ code in
# AwowLab reads these files using matching struct definitions.
#
# Binary files use:
# - Little-endian byte order ('<' prefix in struct format)
# - 4-byte alignment with explicit padding
# - Sorted by primary key (spell_id or talent_spell_id) for binary search
#
# File structure:
# - Header: uint32_t count (number of records)
# - Records: packed array of fixed-size records
# ============================================================================

# SpellData binary record (20 bytes)
# ---------------------------------
# This record stores cooldown, GCD, and cast time data for a spell.
# Used for calculating when spells are available during log replay.
#
# struct SpellDataRecord {
#     uint32_t spell_id;           // Primary key, sorted for binary search
#     int32_t  gcd_ms;             // Global cooldown in milliseconds (1500 typical)
#     int32_t  cooldown_ms;        // Spell cooldown in milliseconds (0 = none)
#     int32_t  cast_time_ms;       // Cast time in milliseconds (0 = instant)
#     uint8_t  gcd_haste_affected; // 1 = GCD scales with haste, 0 = fixed
#     uint8_t  cd_haste_affected;  // 1 = CD scales with haste (rare), 0 = fixed
#     uint8_t  base_charges;       // Number of charges (0 = not charge-based)
#     uint8_t  padding[1];         // Alignment padding
# };

SPELL_DATA_FORMAT = '<IiiiBBBx'  # Little-endian: I=uint32, i=int32, B=uint8, x=pad
SPELL_DATA_SIZE = 20

# TalentModifier binary record (16 bytes)
# ---------------------------------------
# This record stores talent-based cooldown modifications.
# AwowLab loads these to adjust spell cooldowns based on player talents.
#
# struct CooldownModifierRecord {
#     uint32_t talent_spell_id;    // The talent that provides this modifier
#                                  // May be WCL entry_id for WarcraftLogs compatibility
#     uint32_t modified_spell_id;  // The spell whose cooldown is modified
#     uint8_t  modifier_type;      // 0=flat, 1=percent, 2=resource, 3=charge, 4=replace
#     uint8_t  power_type;         // For resource-based: 0=mana, 1=rage, 3=energy, etc.
#     uint8_t  padding[2];         // Alignment padding
#     int32_t  value;              // Modifier value (interpretation depends on type)
# };

MODIFIER_FORMAT = '<IIBBxxi'  # Little-endian: I=uint32, B=uint8, x=pad, i=int32
MODIFIER_SIZE = 16


# ============================================================================
# Known haste-affected spells
# ============================================================================

HASTE_AFFECTED_COOLDOWNS = {
    # Demon Hunter
    203720,  # Demon Spikes
    228537,  # Blade Dance

    # Monk
    115181,  # Breath of Fire
    121253,  # Keg Smash

    # Death Knight
    49028,   # Dancing Rune Weapon

    # Add more as needed...
}


# ============================================================================
# Power Type Mapping (for resource-based modifiers)
# ============================================================================
#
# WoW uses numeric power type IDs in the database. This mapping converts
# between human-readable names (from spell descriptions) and the numeric IDs.
#
# Resource-based cooldown modifiers work like:
# "Every 30 Rage spent reduces the cooldown of Shield Wall by 1 second"
#
# We detect the power type from the spell description, then store the
# numeric ID in the binary output for the C++ code to use.
# ============================================================================

POWER_TYPE_MAP = {
    'mana': 0,           # Casters
    'rage': 1,           # Warriors, Guardian Druids
    'focus': 2,          # Hunters
    'energy': 3,         # Rogues, Feral Druids, Monks
    'combo points': 4,   # Rogues, Feral Druids
    'runes': 5,          # Death Knights (special resource)
    'runic power': 6,    # Death Knights
    'soul shards': 7,    # Warlocks
    'lunar power': 8,    # Balance Druids (Astral Power in-game)
    'holy power': 9,     # Paladins
    'alternate power': 10,  # Boss encounter mechanics
    'maelstrom': 11,     # Elemental/Enhancement Shamans
    'chi': 12,           # Monks
    'insanity': 13,      # Shadow Priests
    'arcane charges': 16,  # Arcane Mages
    'fury': 17,          # Demon Hunters
    'pain': 18,          # Vengeance Demon Hunters
    'essence': 19,       # Evokers
}

# Reverse mapping: ID -> Display name (for JSON output)
POWER_TYPE_NAMES = {v: k.title() for k, v in POWER_TYPE_MAP.items()}


# ============================================================================
# DB2 Parser
# ============================================================================
#
# The main parser class that processes all DB2 export files and generates
# the binary output tables.
#
# Parsing Pipeline:
# 1. parse_spellcooldowns()       - Extract base cooldowns
# 2. parse_spellcategory()        - Extract charge category definitions
# 3. parse_spellcategories()      - Link spells to charge categories
# 4. parse_spellclassoptions()    - Extract family masks for effect targeting
# 5. parse_spelleffect()          - Extract cooldown modifier effects
# 6. parse_spell_labels()         - Extract label-to-spell mappings
# 7. parse_spell_power()          - Extract power costs
# 8. parse_spell_descriptions()   - Extract spell descriptions (for pattern matching)
# 9. parse_spell_names()          - Extract spell names
# 10. parse_traitnodeentry()      - Build WCL talent ID mappings
# 11. build_talent_modifiers()    - Match effects to spells via masks
# 12. apply_category_data()       - Apply charge data to spells
# 13. detect_charge_modifiers()   - Detect charge-granting talents
# 14. write_*()                   - Generate output files
# ============================================================================

class DB2CooldownParser:
    """
    Parses WoW DB2 export files to extract spell and talent modifier data.

    This class reads the various DB2 export files, cross-references them to
    build a complete picture of spell cooldowns and how talents modify them,
    then outputs binary tables for AwowLab's runtime use.
    """

    def __init__(self, db2_dir: str, version: str = DEFAULT_VERSION):
        """
        Initialize the parser.

        Args:
            db2_dir: Path to directory containing DB2 export .txt files
            version: Game version whose column layout these parsers expect.
                     Each export's layout hash is checked against it on open.
        """
        self.db2_dir = db2_dir
        self.version = version

        # ========================
        # Core spell data storage
        # ========================
        self.spell_data: Dict[int, SpellData] = {}              # spell_id -> SpellData
        self.spell_masks: Dict[int, SpellClassMask] = {}        # spell_id -> family mask (for effect targeting)
        self.spell_names: Dict[int, str] = {}                   # spell_id -> display name
        self.spell_descriptions: Dict[int, str] = {}            # spell_id -> tooltip text

        # ========================
        # Charge-based spell data
        # ========================
        # WoW uses "categories" to group charge-based spells. Each category defines
        # the base charges and recharge time. Multiple spells can share a category.
        self.category_data: Dict[int, Tuple[int, int]] = {}     # category_id -> (charges, charge_cooldown_ms)
        self.spell_to_charge_category: Dict[int, int] = {}      # spell_id -> category_id

        # ========================
        # Effect extraction
        # ========================
        self.cooldown_effects: List[CooldownEffect] = []        # Family-mask based CD modifiers (types 11, 1469)
        self.charge_grant_effects: List[Tuple[int, int]] = []   # (talent_spell_id, target_category_id) for type 411
        self.spell_dummy_effects: Dict[int, List[Tuple[int, float]]] = {}  # For resource-based detection

        # ========================
        # Label-based targeting (Type 219)
        # ========================
        # Some effects use labels instead of family masks to target spells.
        # Labels are arbitrary numeric IDs assigned to spells.
        # Type 219 effects store [modifier_type, label_id] and we need to
        # look up which spells have that label to find the target.
        self.spell_labels: Dict[int, Set[int]] = {}             # spell_id -> {label_ids}
        self.label_to_spells: Dict[int, List[int]] = {}         # label_id -> [spell_ids] (reverse mapping)
        self.label_modifiers: List[Dict] = []                   # Type 219 flat CD modifiers

        # ========================
        # Power/resource data
        # ========================
        self.spell_power_types: Dict[int, int] = {}             # spell_id -> power_type

        # ========================
        # Final output
        # ========================
        self.talent_modifiers: List[TalentModifier] = []        # All detected modifiers
        self.resource_modifiers: List[TalentModifier] = []      # Auto-detected resource modifiers

    # ========================================================================
    # Step 1: Parse spell cooldowns from spellcooldowns_export.txt
    # ========================================================================
    #
    # FILE FORMAT: spellcooldowns_export.txt
    # --------------------------------------
    # Contains base cooldown values for spells.
    #
    # Line format: EntryID: field0,field1,cooldown_ms,field3,field4,spell_id
    #
    # Example:
    #   12345: 0,0,120000,0,0,22842
    #   ^^^^^                ^^^^^
    #   Entry ID             Spell ID (Frenzied Regeneration)
    #             ^^^^^^
    #             Cooldown in ms (120 seconds)
    #
    # Note: For charge-based spells, this cooldown may be a "trigger" cooldown
    # that's different from the recharge time. The SpellCategory table has
    # the authoritative recharge time.
    # ========================================================================

    def resolve_export(self, base_name: str) -> Optional[str]:
        """
        Locate an export and confirm its columns are where this parser expects.

        Accepts either naming convention ({name}.txt or {name}_export.txt).
        Returns None when the file is absent, matching the callers that warn and
        skip. A shape mismatch raises instead of returning: the parse would still
        succeed but produce wrong numbers, so it must not be allowed to proceed.
        """
        path = find_export(self.db2_dir, base_name)
        if path is None:
            return None
        verify_layout(path, base_name, self.version)
        return str(path)

    def parse_spellcooldowns(self):
        """
        Extract base cooldowns for all spells from the spellcooldowns export.

        This provides the initial cooldown values which may be overridden by
        category data for charge-based spells.
        """
        filepath = self.resolve_export("spellcooldowns")

        if filepath is None:
            print(f"Warning: {filepath} not found")
            return

        with open(filepath, 'r', encoding='utf-8') as f:
            next(f)  # Skip header

            for line in f:
                if not line.strip():
                    continue

                # Format: SpellID: field0,field1,cooldown_ms,field3,field4,spell_id
                match = re.match(r'^(\d+):\s*(.+)$', line)
                if not match:
                    continue

                fields = match.group(2).split(',')
                if len(fields) < 6:
                    continue

                try:
                    spell_id = int(fields[5])
                    cooldown_ms = int(fields[2])
                    gcd_ms = int(fields[3])  # Extract GCD from field 3 (DB2 fallback)

                    # Initialize spell data (will be filled in by other parsers)
                    if spell_id not in self.spell_data:
                        # Off-GCD spells (gcd_ms==0) are never haste-affected
                        gcd_haste_affected = (gcd_ms > 0)

                        self.spell_data[spell_id] = SpellData(
                            spell_id=spell_id,
                            gcd_ms=gcd_ms,  # Use DB2 value (fallback for spells not in CSV)
                            cooldown_ms=cooldown_ms,
                            cast_time_ms=0,
                            gcd_haste_affected=gcd_haste_affected,
                            cd_haste_affected=spell_id in HASTE_AFFECTED_COOLDOWNS
                        )
                    else:
                        # Update existing spell (may be from category data)
                        self.spell_data[spell_id].cooldown_ms = cooldown_ms
                        # Also update GCD if not already set by spell-data-generator
                        if self.spell_data[spell_id].gcd_ms == 1500:  # Default value
                            self.spell_data[spell_id].gcd_ms = gcd_ms
                            self.spell_data[spell_id].gcd_haste_affected = (gcd_ms > 0)

                except (ValueError, IndexError):
                    continue

    # ========================================================================
    # Step 1b: Parse SpellCategory table for charge/cooldown data
    # ========================================================================
    #
    # FILE FORMAT: spellcategory_export.txt
    # -------------------------------------
    # Defines spell categories with charge counts and recharge times.
    # Charge-based spells (like Roll, Frenzied Regeneration, Survival of the Fittest)
    # are linked to categories that define their charge behavior.
    #
    # Line format: CategoryID: name,flags,uses_per_week,charges,charge_cooldown,type_mask
    #
    # Example:
    #   1568: Class - Druid - Frenzied Regeneration,0,0,1,36000,1
    #   ^^^^                                          ^ ^^^^^
    #   Category ID                          Charges=1 Recharge=36sec
    #
    #   1830: Hunter - Survival of the Fittest,0,0,1,90000,1
    #                                            ^ ^^^^^
    #                                    1 charge, 90 sec recharge
    #
    # The "charges" field is the BASE charge count before talent modifications.
    # Talents like "Padded Armor" add additional charges via type 411 effects.
    # ========================================================================

    def parse_spellcategory(self):
        """
        Parse spellcategory_export.txt for category-based charges and cooldowns.

        Categories group charge-based spells and define their base charges and
        recharge times. This is the authoritative source for charge spell timing.
        """
        filepath = self.resolve_export("spellcategory")

        if filepath is None:
            print(f"Warning: {filepath} not found")
            return

        with open(filepath, 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('Layout:'):
                    continue

                # Format: CategoryID: name,flags,uses_per_week,charges,charge_cooldown,type_mask
                match = re.match(r'^(\d+):\s*([^,]+),(\d+),(\d+),(\d+),(\d+),(\d+)', line)
                if not match:
                    continue

                try:
                    cat_id = int(match.group(1))
                    # name = match.group(2)  # Not needed, but available
                    # flags = int(match.group(3))
                    # uses_per_week = int(match.group(4))
                    charges = int(match.group(5))
                    charge_cooldown = int(match.group(6))
                    # type_mask = int(match.group(7))

                    # Only store categories with meaningful data
                    if charges > 0 or charge_cooldown > 0:
                        self.category_data[cat_id] = (charges, charge_cooldown)
                except (ValueError, IndexError):
                    continue

        print(f"  Loaded {len(self.category_data)} spell categories with charge/cooldown data")

    # ========================================================================
    # Step 1c: Parse SpellCategories table to link spells to categories
    # ========================================================================
    #
    # FILE FORMAT: spellcategories_export.txt
    # ---------------------------------------
    # Links individual spells to their charge categories.
    # This is the bridge between spell IDs and the category definitions.
    #
    # Line format: EntryID: f0,f1,f2,f3,f4,f5,f6,f7,id_charge_category,spell_id
    #
    # Example:
    #   81468: 0,0,0,0,0,0,0,1830,1830,264735
    #                           ^^^^  ^^^^^^
    #                   Category ID   Spell ID (Survival of the Fittest)
    #
    # The same category can be shared by multiple spells. For example, if
    # two spells share charges, they'd reference the same category.
    # ========================================================================

    def parse_spellcategories(self):
        """
        Parse spellcategories_export.txt to link spells to their charge categories.

        Builds the spell_id -> category_id mapping that allows us to look up
        charge counts and recharge times from the category definitions.
        """
        filepath = self.resolve_export("spellcategories")

        if filepath is None:
            print(f"Warning: {filepath} not found")
            return

        with open(filepath, 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('Layout:'):
                    continue

                match = re.match(r'^(\d+):\s*(.+)$', line)
                if not match:
                    continue

                fields = match.group(2).split(',')
                if len(fields) < 10:
                    continue

                try:
                    # Field 8 is id_charge_category, field 9 is spell_id
                    charge_category = int(fields[8])
                    spell_id = int(fields[9])

                    if charge_category > 0:
                        self.spell_to_charge_category[spell_id] = charge_category
                except (ValueError, IndexError):
                    continue

        print(f"  Loaded {len(self.spell_to_charge_category)} spell -> category mappings")

    # ========================================================================
    # Step 1d: Apply category-based cooldowns and charges to spell data
    # ========================================================================

    def apply_category_data(self):
        """Apply category-based cooldowns and charges to spells

        For charge-based spells, the category's charge_cooldown is the authoritative recharge time.
        This takes precedence over any cooldown in spellcooldowns (which may be 0 or a trigger cd).
        """
        updated_cooldowns = 0
        updated_charges = 0

        for spell_id, cat_id in self.spell_to_charge_category.items():
            if cat_id not in self.category_data:
                continue

            charges, cat_cooldown = self.category_data[cat_id]

            # Get or create spell data entry
            if spell_id not in self.spell_data:
                self.spell_data[spell_id] = SpellData(
                    spell_id=spell_id,
                    gcd_ms=1500,  # Default GCD
                    cooldown_ms=0,
                    cast_time_ms=0,
                    gcd_haste_affected=True,
                    cd_haste_affected=spell_id in HASTE_AFFECTED_COOLDOWNS,
                    base_charges=0
                )

            spell = self.spell_data[spell_id]

            # For charge-based spells, category cooldown is the recharge time (overrides spellcooldowns)
            # spellcooldowns may have a different value (trigger CD) but charge_cooldown is authoritative
            if cat_cooldown > 0:
                spell.cooldown_ms = cat_cooldown
                updated_cooldowns += 1

            # Apply base charges from category
            if charges > 0:
                spell.base_charges = charges
                updated_charges += 1

        print(f"  Applied category data: {updated_cooldowns} cooldowns, {updated_charges} charge counts")

        # Debug: Show some example spells
        debug_spells = [264735, 22842, 109132]  # SotF, Frenzied Regen, Roll
        for sid in debug_spells:
            if sid in self.spell_data:
                spell = self.spell_data[sid]
                name = self.spell_names.get(sid, f"Spell {sid}")
                cat_id = self.spell_to_charge_category.get(sid, 0)
                print(f"    {name} ({sid}): cd={spell.cooldown_ms}ms, charges={spell.base_charges} (category {cat_id})")

    # ========================================================================
    # Step 1e: Detect charge-granting talent modifiers
    # ========================================================================

    def detect_charge_modifiers(self):
        """Detect talents that grant additional charges to spells

        Uses effect type 411 entries collected during parse_spelleffect().
        These effects target charge categories (e.g., category 1830 for SotF).
        """
        # Build reverse map: category_id -> list of spell_ids
        category_to_spells: Dict[int, List[int]] = {}
        for spell_id, cat_id in self.spell_to_charge_category.items():
            if cat_id not in category_to_spells:
                category_to_spells[cat_id] = []
            category_to_spells[cat_id].append(spell_id)

        charge_modifiers_added = 0

        for talent_spell_id, target_category in self.charge_grant_effects:
            # Check if this category has any spells associated with it
            if target_category not in category_to_spells:
                continue

            # This talent grants +1 charge to all spells in the target category
            for modified_spell_id in category_to_spells[target_category]:
                talent_name = self.spell_names.get(talent_spell_id, f"Spell {talent_spell_id}")
                modified_name = self.spell_names.get(modified_spell_id, f"Spell {modified_spell_id}")

                self.talent_modifiers.append(TalentModifier(
                    talent_spell_id=talent_spell_id,
                    modified_spell_id=modified_spell_id,
                    modifier_type=3,  # ChargeBased
                    value=1  # +1 charge
                ))
                charge_modifiers_added += 1

                print(f"  [CHARGE] {talent_name} ({talent_spell_id}): +1 charge to {modified_name} ({modified_spell_id})")

        print(f"  Detected {charge_modifiers_added} charge-granting talent modifiers")

    # ========================================================================
    # Step 2: Parse spell class options (family masks)
    # ========================================================================
    #
    # FILE FORMAT: spellclassoptions_export.txt
    # -----------------------------------------
    # Contains spell family assignments and masks used for effect targeting.
    #
    # FAMILY MASK CONCEPT:
    # Each spell belongs to a "family" (roughly by class) and has a 128-bit mask.
    # Effects that modify spells use a target mask - if the target mask has any
    # overlapping bits with a spell's mask (bitwise AND != 0), the effect applies.
    #
    # Line format: EntryID: SpellID,field1,Family,[mask0,mask1,mask2,mask3]
    #
    # Example:
    #   75104: 439843,0,15,[0,2048,0,16777216]
    #   ^^^^^  ^^^^^^   ^^  ^^^^^^^^^^^^^^^^^
    #   Entry  Spell    Family=15   128-bit mask (4 x 32-bit)
    #          (Reaper's Mark)
    #
    # Family IDs (partial list):
    #   6  = Death Knight
    #   7  = Druid
    #   9  = Hunter
    #   10 = Mage
    #   11 = Paladin (Retribution)
    #   15 = Paladin (Holy/Protection) or shared
    #
    # This is used by effect types 11 and 1469 for percent-based CD modifiers.
    # ========================================================================

    def parse_spellclassoptions(self):
        """
        Extract spell family masks from spellclassoptions_export.txt.

        These masks are used by percent-based cooldown modifiers (effect types
        11 and 1469) to determine which spells they affect.
        """
        filepath = self.resolve_export("spellclassoptions")

        if filepath is None:
            print(f"Warning: {filepath} not found")
            return

        with open(filepath, 'r', encoding='utf-8') as f:
            next(f)  # Skip header

            for line in f:
                if not line.strip():
                    continue

                # Format: EntryID: SpellID,f1,Family,[m0,m1,m2,m3]
                match = re.match(r'^(\d+):\s*(\d+),(\d+),(\d+),\[([^\]]+)\]', line)
                if not match:
                    continue

                spell_id = int(match.group(2))
                family = int(match.group(4))
                mask_str = match.group(5)

                mask_parts = mask_str.split(',')
                if len(mask_parts) != 4:
                    continue

                mask = tuple(int(x) for x in mask_parts)
                self.spell_masks[spell_id] = SpellClassMask(spell_id, family, mask)

    # ========================================================================
    # Step 3: Parse spell effects (cooldown modifiers)
    # ========================================================================
    #
    # FILE FORMAT: spelleffect_export.txt
    # -----------------------------------
    # Contains all spell effects - this is the core file for modifier detection.
    # Each spell can have multiple effects, identified by effect_type.
    #
    # Line format (simplified):
    #   EffectID: effect_type,f1,effect_index,...,value,...,[arr0],[arr1],[target_mask],[arr3],spell_id
    #
    # EFFECT TYPES FOR COOLDOWN MODIFICATIONS:
    # ----------------------------------------
    #
    # Type 4 (Dummy Aura):
    #   Contains metadata values used for resource-based modifier detection.
    #   Example: Anger Management has Dummy effects with resource amount and CD reduction.
    #
    # Type 11 (Apply Aura: Mod Cooldown - Percent):
    #   Uses FAMILY MASK targeting (array index 2).
    #   Example: A talent reducing CD by 20% has value=-20 and targets via mask.
    #   Currently EXTRACTED - matched against spellclassoptions masks.
    #
    # Type 219 (Apply Aura: Add Modifier - Flat, Label-based):
    #   Uses LABEL targeting via first array: [modifier_type, label_id]
    #   modifier_type 11 = cooldown modification
    #   Example line for Reaper's Onslaught (469870):
    #     1184516: 219,0,0,6,0,0,0,0,1,0,0,0,0,0,0,0,0,1,0,0,0,1,-15000,0,0,[11,3243],[0,0],[0,0,0,0],[1,0],469870
    #              ^^^                                           ^^^^^^  ^^^^^^^^^
    #              Type 219                                      Value   [11=CD modifier, 3243=label_id]
    #   The label 3243 maps to Reaper's Mark (439843) via spelllabel_export.txt
    #
    #   *** NOT YET IMPLEMENTED - FLAT CD REDUCTIONS ARE MISSED! ***
    #   TODO: Add extraction for type 219 with modifier_type=11
    #
    # Type 411 (Add Spell Charges):
    #   Uses CATEGORY targeting via first array: [category_id, ...]
    #   Grants additional charges to all spells in the target category.
    #   Example: Padded Armor talent grants +1 charge to Survival of the Fittest.
    #   Currently EXTRACTED.
    #
    # Type 1469 (Mod Cooldown Haste):
    #   Similar to type 11, variant for haste-scaling cooldowns.
    #   Uses FAMILY MASK targeting.
    #   Currently EXTRACTED.
    #
    # ARRAY FORMAT:
    # -------------
    # Effects contain multiple arrays in brackets:
    #   [arr0]       - Varies by effect type (category ID for 411, [mod,label] for 219)
    #   [arr1]       - Usually padding/unused
    #   [arr2]       - Family mask for targeting (4 integers)
    #   [arr3]       - Misc flags
    #
    # ========================================================================

    def parse_spelleffect(self):
        """
        Extract cooldown modifier effects from spelleffect_export.txt.

        Parses effect types 4, 11, 219, 411, and 1469 for various modifier types.
        Note: Type 219 (flat, label-based) is captured but not yet processed.
        """
        filepath = self.resolve_export("spelleffect")

        if filepath is None:
            print(f"Warning: {filepath} not found")
            return

        with open(filepath, 'r', encoding='utf-8') as f:
            next(f)  # Skip header

            for line_num, line in enumerate(f, start=2):
                if not line.strip():
                    continue

                match = re.match(r'^(\d+):\s*(.+)$', line)
                if not match:
                    continue

                effect_id = int(match.group(1))
                data = match.group(2)

                try:
                    fields, arrays = self._parse_effect_line(data)

                    if len(fields) < 23 or len(arrays) < 1:
                        continue

                    effect_type = int(fields[0]) if len(fields) > 0 else 0
                    value = float(fields[22])
                    effect_index = int(fields[2]) if len(fields) > 2 else 0

                    if len(arrays) < 3 or len(arrays[2]) != 4:
                        continue

                    target_mask = tuple(arrays[2])
                    spell_id = int(fields[-1])

                    # --------------------------------------------------------
                    # EFFECT TYPE FILTERING
                    # --------------------------------------------------------
                    # We extract different effect types for different purposes:
                    #
                    # Types 11, 1469 (Percent CD modifiers with family mask):
                    #   - Stored in self.cooldown_effects for mask matching
                    #   - Processed in build_talent_modifiers()
                    #
                    # Type 4 (Dummy Aura):
                    #   - Stored in self.spell_dummy_effects
                    #   - Used for resource-based modifier detection
                    #
                    # Type 411 (Charge Grant):
                    #   - Stored in self.charge_grant_effects
                    #   - Processed in detect_charge_modifiers()
                    #
                    # TODO: Type 219 (Flat CD modifier with LABEL targeting):
                    #   - NOT YET IMPLEMENTED
                    #   - arrays[0] = [modifier_type, label_id]
                    #   - modifier_type 11 = cooldown modification
                    #   - Need to look up label_id in spelllabel to find target spells
                    #   - Example: Reaper's Onslaught (469870) targets label 3243
                    #             which maps to Reaper's Mark (439843)
                    # --------------------------------------------------------

                    # Types 11, 1469: Percent-based CD modifiers (family mask targeting)
                    if effect_type in {11, 1469} and any(target_mask):
                        self.cooldown_effects.append(CooldownEffect(
                            effect_id=effect_id,
                            spell_id=spell_id,
                            effect_type=effect_type,
                            value=value,
                            target_mask=target_mask
                        ))

                    # Type 4: Dummy aura effects (for resource-based modifier detection)
                    if effect_type == 4:
                        if spell_id not in self.spell_dummy_effects:
                            self.spell_dummy_effects[spell_id] = []
                        self.spell_dummy_effects[spell_id].append((effect_index, value))

                        # Debug specific spells - print ALL fields to find related spell ID
                        if spell_id in [117204, 393414]:
                            print(f"  DEBUG: Found Dummy effect for spell {spell_id}: index={effect_index}, value={value}")
                            print(f"    ALL FIELDS: {fields}")
                            print(f"    ALL ARRAYS: {arrays}")

                    # Type 411: Charge grant modifier (category targeting)
                    # arrays[0] = [category_id, ...] specifying target charge category
                    # Example: Padded Armor targets category 1830 (Survival of the Fittest)
                    if effect_type == 411 and len(arrays) >= 1 and len(arrays[0]) >= 1:
                        target_category = arrays[0][0]
                        if target_category > 0:
                            self.charge_grant_effects.append((spell_id, target_category))

                    # Type 219: Flat CD modifier with LABEL targeting
                    # arrays[0] = [modifier_type, label_id] where modifier_type 11 = cooldown
                    # Example: Reaper's Onslaught (469870) has [11, 3243] targeting label 3243
                    #          which maps to Reaper's Mark (439843) via spelllabel table
                    if effect_type == 219 and len(arrays) >= 1 and len(arrays[0]) >= 2:
                        modifier_type_val = arrays[0][0]
                        label_id = arrays[0][1]

                        # modifier_type 11 = cooldown modification
                        if modifier_type_val == 11 and label_id > 0:
                            self.label_modifiers.append({
                                'effect_id': effect_id,
                                'spell_id': spell_id,
                                'value': value,
                                'label_id': label_id
                            })

                except Exception:
                    continue

    def _parse_effect_line(self, data: str) -> Tuple[List[str], List[List[int]]]:
        """Parse mixed field/array format"""
        fields = []
        arrays = []

        parts = []
        in_array = False
        current = ""

        for char in data:
            if char == '[':
                in_array = True
                current += char
            elif char == ']':
                in_array = False
                current += char
            elif char == ',' and not in_array:
                parts.append(current.strip())
                current = ""
            else:
                current += char

        if current:
            parts.append(current.strip())

        for part in parts:
            if part.startswith('['):
                arr_str = part.strip('[]')
                if arr_str:
                    arr = [int(x) for x in arr_str.split(',') if x.strip()]
                    arrays.append(arr)
                else:
                    arrays.append([])
            else:
                fields.append(part)

        return fields, arrays

    # ========================================================================
    # Step 3b: Parse spell labels for label-based targeting
    # ========================================================================
    #
    # FILE FORMAT: spelllabel_export.txt
    # ----------------------------------
    # Maps label IDs to spell IDs. Labels are arbitrary numeric tags that can
    # be assigned to spells for targeting purposes.
    #
    # Line format: EntryID: label_id,spell_id
    #
    # Example:
    #   147843: 3243,439843
    #           ^^^^  ^^^^^^
    #           Label Spell ID (Reaper's Mark)
    #
    # This is used by effect type 219 for label-based targeting:
    # 1. Effect 219 specifies [modifier_type=11, label_id=3243]
    # 2. We look up label 3243 in spelllabel to find spell 439843
    # 3. The effect now targets spell 439843 (Reaper's Mark)
    #
    # Some labels have special meanings (e.g., label 292 for cooldown modifiers)
    # but most are just arbitrary groupings.
    # ========================================================================

    def parse_spell_labels(self):
        """
        Parse spell labels from spelllabel_export.txt.

        Builds two mappings:
        - spell_id -> {label_ids} (forward mapping)
        - label_id -> [spell_ids] (reverse mapping for type 219 extraction)
        """
        filepath = self.resolve_export("spelllabel")

        if filepath is None:
            print(f"Warning: {filepath} not found")
            return

        with open(filepath, 'r', encoding='utf-8') as f:
            next(f)  # Skip header

            for line in f:
                if not line.strip():
                    continue

                # Format: EntryID: LabelID,SpellID
                match = re.match(r'^(\d+):\s*(\d+),(\d+)', line)
                if not match:
                    continue

                label_id = int(match.group(2))
                spell_id = int(match.group(3))

                # Forward mapping: spell -> labels
                if spell_id not in self.spell_labels:
                    self.spell_labels[spell_id] = set()
                self.spell_labels[spell_id].add(label_id)

                # Reverse mapping: label -> spells (for type 219 extraction)
                if label_id not in self.label_to_spells:
                    self.label_to_spells[label_id] = []
                self.label_to_spells[label_id].append(spell_id)

        print(f"  Loaded {len(self.spell_labels)} spell label mappings, {len(self.label_to_spells)} unique labels")

    # ========================================================================
    # Step 3c: Parse spell power types for resource-based modifiers
    # ========================================================================

    def parse_spell_power(self):
        """Parse spell power costs to determine power types"""
        filepath = self.resolve_export("spellpower")

        if filepath is None:
            print(f"Warning: {filepath} not found")
            return

        with open(filepath, 'r', encoding='utf-8') as f:
            next(f)  # Skip header

            for line in f:
                if not line.strip():
                    continue

                # Format: EntryID: ...,PowerType,...,SpellID
                match = re.match(r'^(\d+):\s*(.+)$', line)
                if not match:
                    continue

                fields = match.group(2).split(',')
                if len(fields) < 14:
                    continue

                try:
                    power_type = int(fields[11])  # Power type at index 11
                    spell_id = int(fields[-1])    # Spell ID is last field

                    # Only store if power type is non-zero
                    if power_type > 0:
                        self.spell_power_types[spell_id] = power_type
                except (ValueError, IndexError):
                    continue

    # ========================================================================
    # Step 3d: Parse spell descriptions and names
    # ========================================================================

    def parse_spell_descriptions(self):
        """Parse spell descriptions from spell_export.txt"""
        filepath = self.resolve_export("spell")

        if filepath is None:
            print(f"Warning: {filepath} not found")
            return

        with open(filepath, 'r', encoding='utf-8') as f:
            next(f)  # Skip header

            for line in f:
                if not line.strip():
                    continue

                # Format: SpellID: ,description,$
                match = re.match(r'^(\d+):\s*,(.*),$', line)
                if not match:
                    continue

                spell_id = int(match.group(1))
                description = match.group(2)

                self.spell_descriptions[spell_id] = description

    def parse_spell_names(self):
        """Parse spell names from spellname_export.txt"""
        filepath = self.resolve_export("spellname")

        if filepath is None:
            print(f"Warning: {filepath} not found")
            return

        with open(filepath, 'r', encoding='utf-8') as f:
            next(f)  # Skip header

            for line in f:
                if not line.strip():
                    continue

                match = re.match(r'^(\d+):\s*(.+)$', line)
                if not match:
                    continue

                spell_id = int(match.group(1))
                name = match.group(2).strip()

                self.spell_names[spell_id] = name

    # ========================================================================
    # Step 3e: Parse trait definitions for definition_id -> spell_id mapping
    # ========================================================================
    #
    # FILE FORMAT: traitdefinition_export.txt
    # ---------------------------------------
    # Contains trait (talent) definitions with their spell IDs.
    # This is part of the WCL compatibility mapping chain.
    #
    # Line format: DefinitionID: f0,f1,f2,f3,spell_id,...
    #
    # Example:
    #   105234: 0,0,0,0,459450,...
    #   ^^^^^^           ^^^^^^
    #   Definition ID    Spell ID (talent ability)
    #
    # WCL MAPPING CHAIN:
    # WarcraftLogs uses "entry IDs" that differ from game spell IDs.
    # The mapping chain is:
    #
    #   WCL entry_id -> definition_id -> spell_id
    #   (traitnodeentry)  (traitdefinition)
    #
    # This allows us to match WCL talent data to our modifier database.
    # ========================================================================

    def parse_traitdefinition(self):
        """
        Parse traitdefinition_export.txt to build definition_id -> spell_id mapping.

        This is step 1 of the WCL compatibility mapping chain.
        """
        filepath = self.resolve_export("traitdefinition")

        if filepath is None:
            print(f"Warning: {filepath} not found")
            self.definition_to_spell = {}
            return

        self.definition_to_spell = {}  # definition_id -> spell_id

        with open(filepath, 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("Layout:"):
                    continue

                if ':' not in line:
                    continue

                id_part, data_part = line.split(':', 1)
                fields = data_part.strip().split(',')

                if len(fields) < 5:
                    continue

                try:
                    definition_id = int(id_part.strip())
                    spell_id = int(fields[4])  # Fifth field is spell ID

                    if spell_id > 0:
                        self.definition_to_spell[definition_id] = spell_id
                except ValueError:
                    continue

        print(f"  Loaded {len(self.definition_to_spell)} definition -> spell mappings")

    # ========================================================================
    # Step 3f: Parse trait node entries for WCL talent ID mapping (two-step)
    # ========================================================================
    #
    # FILE FORMAT: traitnodeentry_export.txt
    # --------------------------------------
    # Contains trait node entries that WarcraftLogs uses as talent identifiers.
    # This is step 2 of the WCL mapping chain.
    #
    # Line format: EntryID: f0,definition_id,...
    #
    # Example:
    #   126470: 0,105234,...
    #   ^^^^^^    ^^^^^^
    #   WCL ID    Definition ID
    #
    # WCL MAPPING CHAIN (complete):
    # 1. WCL provides talent IDs like 126470, 126747 in API responses
    # 2. traitnodeentry maps: 126470 -> definition_id 105234
    # 3. traitdefinition maps: 105234 -> spell_id 459450
    # 4. We can now match WCL talents to our modifier database
    #
    # MULTIPLE WCL IDs PER SPELL:
    # Some talents have multiple "ranks" or "nodes" in the talent tree,
    # each with a different WCL entry_id but the same spell_id.
    # Example: Padded Armor (spell 459450) has WCL IDs 126470 AND 126747.
    # We store all WCL IDs for each spell in spell_to_entries.
    # ========================================================================

    def parse_traitnodeentry(self):
        """
        Parse traitnodeentry_export.txt to build WCL entry_id -> spell_id mapping.

        This is step 2 of the WCL mapping chain. Calls parse_traitdefinition()
        first to build the definition_id -> spell_id mapping.
        """
        # First, parse trait definitions (definition_id -> spell_id)
        self.parse_traitdefinition()

        filepath = self.resolve_export("traitnodeentry")

        if filepath is None:
            print(f"Warning: {filepath} not found")
            self.entry_to_spell = {}
            self.spell_to_entries = {}
            return

        self.entry_to_definition = {}  # entry_id -> definition_id
        self.entry_to_spell = {}  # entry_id -> spell_id (chained)

        with open(filepath, 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("Layout:"):
                    continue

                if ':' not in line:
                    continue

                id_part, data_part = line.split(':', 1)
                fields = data_part.strip().split(',')

                if len(fields) < 2:
                    continue

                try:
                    entry_id = int(id_part.strip())
                    definition_id = int(fields[1])  # Second field is DEFINITION ID (not spell ID!)

                    if definition_id > 0:
                        self.entry_to_definition[entry_id] = definition_id

                        # Chain the mapping: entry_id -> definition_id -> spell_id
                        if definition_id in self.definition_to_spell:
                            spell_id = self.definition_to_spell[definition_id]
                            self.entry_to_spell[entry_id] = spell_id
                except ValueError:
                    continue

        # Build reverse mapping: spell_id -> [entry_ids]
        self.spell_to_entries = {}
        for entry_id, spell_id in self.entry_to_spell.items():
            if spell_id not in self.spell_to_entries:
                self.spell_to_entries[spell_id] = []
            self.spell_to_entries[spell_id].append(entry_id)

        print(f"  Loaded {len(self.entry_to_spell)} entry -> spell mappings (via definitions)")

    # ========================================================================
    # Step 3g: Detect resource-based cooldown modifiers
    # ========================================================================
    #
    # RESOURCE-BASED MODIFIERS:
    # -------------------------
    # Some talents reduce cooldowns based on resource (power) spending.
    # Example: "Every 30 Rage spent reduces the cooldown of Shield Wall by 1 sec"
    #
    # These are detected via pattern matching in spell descriptions because
    # the DB2 data doesn't explicitly encode this relationship.
    #
    # DETECTION ALGORITHM:
    # 1. Find spells with 2+ Dummy (type 4) effects (metadata values)
    # 2. Check if description mentions "cooldown" + "reduce" + resource spending
    # 3. Extract: power type, resource amount, CD reduction, target spell name
    # 4. Look up target spell ID by name
    # 5. Calculate resource_per_second = (resource_amount * 1000) / cd_reduction_ms
    #
    # OUTPUT FORMAT:
    # The value stored is "resource per second for 1 second reduction".
    # This is multiplied by 10 for WCL compatibility (WCL uses 10x multiplier).
    # ========================================================================

    def detect_resource_based_modifiers(self):
        """
        Automatically detect resource-based cooldown modifiers from spell descriptions.

        Scans spells with Dummy effects for patterns like:
        "Every X <Power> spent reduces the cooldown of <Spell> by Y sec"

        Adds detected modifiers to self.talent_modifiers with modifier_type=2.
        """
        results = []

        for spell_id, dummy_effects in self.spell_dummy_effects.items():
            # DEBUG: Print all spells we're checking
            if spell_id in [117204, 393414]:
                print(f"\n  [DEBUG] Checking spell {spell_id} with {len(dummy_effects)} dummy effects")

            # Only look at spells with 2+ Dummy effects
            if len(dummy_effects) < 2:
                if spell_id in [117204, 393414]:
                    print(f"  [DEBUG] SKIP {spell_id}: Only {len(dummy_effects)} dummy effects")
                continue

            # Get spell description
            description_original = self.spell_descriptions.get(spell_id, "")
            description = description_original.lower()

            if spell_id in [117204, 393414]:
                print(f"  [DEBUG] Description for {spell_id}: '{description_original}'")

            # Check if description mentions cooldown reduction
            if "cooldown" not in description or "reduce" not in description:
                if spell_id in [117204, 393414]:
                    print(f"  [DEBUG] SKIP {spell_id}: Missing 'cooldown' or 'reduce' in description")
                continue

            # Check if description mentions resource spending
            if not any(word in description for word in ["spend", "spent", "spending", "consume", "consuming"]):
                if spell_id in [117204, 393414]:
                    print(f"  [DEBUG] SKIP {spell_id}: Missing resource spending keywords")
                continue

            # Try to extract modified spell name(s)
            modified_spell_names = []
            match = re.search(r'cooldown (?:of|on) (.+?)(?:\s+by)', description_original)
            if match:
                spell_text = match.group(1).strip()

                # Extract spell names from WoW conditional syntax and regular text
                spell_names_found = set()

                # Find all spell names in square brackets (conditional syntax)
                bracket_matches = re.findall(r'\[([A-Z][A-Za-z\s:]+?)\]', spell_text)
                for name in bracket_matches:
                    spell_names_found.add(name.strip())

                # Find spell names before "and"
                before_and = re.findall(r'\b([A-Z][A-Za-z\s:]+?)\s+and\s+', spell_text)
                for name in before_and:
                    cleaned = name.strip()
                    if cleaned and not cleaned.startswith('$') and not any(c in cleaned for c in '[]?'):
                        spell_names_found.add(cleaned)

                # If no spell names found yet, use the whole captured text
                if not spell_names_found:
                    cleaned_text = re.sub(r'\$\{[^}]+\}', '', spell_text)
                    cleaned_text = re.sub(r'\$\w+', '', cleaned_text)
                    cleaned_text = re.sub(r'\s+', ' ', cleaned_text).strip()
                    if cleaned_text and cleaned_text[0].isupper():
                        spell_names_found.add(cleaned_text)

                # Clean up any spell names that have trailing "is reduced" or similar
                spell_names_found = {re.sub(r'\s+(is|are)\s+reduced.*$', '', name).strip()
                                    for name in spell_names_found if name.strip()}

                modified_spell_names = sorted(list(spell_names_found))

            # Try to extract power type
            power_type = None
            for power_name in POWER_TYPE_MAP.keys():
                if power_name in description:
                    power_type = power_name
                    break

            # Find resource amount and CD reduction from effect values
            values = [v for _, v in dummy_effects]

            resource_amount = None
            cd_reduction_ms = None

            # Look for CD reduction first (more distinctive pattern)
            for v in values:
                if abs(v) >= 100:
                    cd_reduction_ms = int(abs(v))
                    break
                elif 1 <= abs(v) <= 9:
                    cd_reduction_ms = int(abs(v) * 1000)  # Convert seconds to ms
                    break

            # Look for resource amount (10-100 range, but not the CD value)
            for v in values:
                if 10 <= abs(v) <= 100 and abs(v) != abs(cd_reduction_ms or 0):
                    resource_amount = int(abs(v))
                    break

            # Fallback: if no resource amount found, try 1-9 range
            if not resource_amount:
                for v in values:
                    if 1 <= abs(v) <= 9 and abs(v) != abs((cd_reduction_ms or 0) / 1000):
                        resource_amount = int(abs(v))
                        break

            # Fallback: if no CD reduction found, try parsing description for "by X sec"
            if not cd_reduction_ms:
                desc_match = re.search(r'by\s+(\d+(?:\.\d+)?)\s*sec', description)
                if desc_match:
                    cd_seconds = float(desc_match.group(1))
                    cd_reduction_ms = int(cd_seconds * 1000)

            if resource_amount and cd_reduction_ms and modified_spell_names and power_type:
                # Find modified spell ID by name
                for spell_name in modified_spell_names:
                    modified_spell_id = None

                    # Search for spell by name
                    for sid, sname in self.spell_names.items():
                        if sname.lower() == spell_name.lower():
                            modified_spell_id = sid
                            break

                    if not modified_spell_id:
                        print(f"  [SKIP] Talent {spell_id}: Could not find spell ID for '{spell_name}'")
                        continue

                    # Calculate resource per second
                    resource_per_sec = int((resource_amount * 1000) / cd_reduction_ms)

                    talent_name = self.spell_names.get(spell_id, f"Spell {spell_id}")
                    print(f"  [OK] {talent_name} ({spell_id}): {resource_amount} {power_type} -> {cd_reduction_ms}ms CD on {spell_name} ({modified_spell_id})")

                    # Add to talent modifiers
                    self.talent_modifiers.append(TalentModifier(
                        talent_spell_id=spell_id,
                        modified_spell_id=modified_spell_id,
                        modifier_type=2,  # resource_based
                        value=resource_per_sec
                    ))

                    # Store power_type for binary writing
                    if not hasattr(self, 'resource_modifier_power_types'):
                        self.resource_modifier_power_types = {}
                    self.resource_modifier_power_types[(spell_id, modified_spell_id)] = POWER_TYPE_MAP[power_type]

    # ========================================================================
    # Step 4: Match effects to spells via family masks
    # ========================================================================
    #
    # FAMILY MASK MATCHING ALGORITHM:
    # -------------------------------
    # For each cooldown effect (types 11, 1469):
    # 1. Get the effect's target_mask (4-tuple of 32-bit integers)
    # 2. For each spell with a family mask in spell_masks:
    #    - If ANY corresponding bits match (bitwise AND), the effect targets this spell
    # 3. Create a TalentModifier linking the talent spell to the target spell
    #
    # Example:
    #   Effect target_mask = [0, 2048, 0, 0]  (bit 11 of mask[1] set)
    #   Spell mask = [0, 2048, 0, 16777216]   (bit 11 of mask[1] set)
    #   Result: 2048 & 2048 = 2048 != 0, so effect matches this spell
    #
    # ========================================================================

    def build_talent_modifiers(self):
        """
        Match cooldown effects to spells via family mask matching and label lookup.

        Processes:
        - Types 11, 1469: Percent-based modifiers via family mask matching
        - Type 219: Flat modifiers via label-to-spell lookup
        - Resource-based modifiers via description pattern matching
        """
        # Step 1: Percent modifiers via family mask matching (types 11, 1469)
        for effect in self.cooldown_effects:
            for spell_id, spell_mask in self.spell_masks.items():
                if self._masks_match(effect.target_mask, spell_mask.mask):
                    # Types 11, 1469 are always percent-based
                    self.talent_modifiers.append(TalentModifier(
                        talent_spell_id=effect.spell_id,
                        modified_spell_id=spell_id,
                        modifier_type=1,  # PercentReduction
                        value=abs(int(effect.value))
                    ))

        # Step 2: Automatically detect resource-based modifiers
        print("\nDetecting resource-based cooldown modifiers...")
        self.detect_resource_based_modifiers()

        # Step 3: Flat modifiers via label lookup (type 219)
        print("\nDetecting flat cooldown modifiers (type 219, label-based)...")
        flat_count = 0
        for label_mod in self.label_modifiers:
            label_id = label_mod['label_id']

            # Look up which spells have this label
            if label_id in self.label_to_spells:
                for target_spell_id in self.label_to_spells[label_id]:
                    talent_name = self.spell_names.get(label_mod['spell_id'], f"Spell {label_mod['spell_id']}")
                    target_name = self.spell_names.get(target_spell_id, f"Spell {target_spell_id}")
                    reduction_sec = abs(label_mod['value']) / 1000

                    print(f"  [FLAT] {talent_name} ({label_mod['spell_id']}): -{reduction_sec}s CD on {target_name} ({target_spell_id})")

                    self.talent_modifiers.append(TalentModifier(
                        talent_spell_id=label_mod['spell_id'],
                        modified_spell_id=target_spell_id,
                        modifier_type=0,  # FlatReduction
                        value=abs(int(label_mod['value']))
                    ))
                    flat_count += 1

        print(f"  Detected {flat_count} flat cooldown modifiers")

    def _masks_match(self, mask1: Tuple, mask2: Tuple) -> bool:
        """
        Check if two spell family masks overlap (have any matching bits).

        Args:
            mask1: First 128-bit mask as 4-tuple of 32-bit integers
            mask2: Second 128-bit mask as 4-tuple of 32-bit integers

        Returns:
            True if any corresponding 32-bit segments have overlapping bits.
            False if no bits overlap.

        Example:
            mask1 = (0, 2048, 0, 0)    # Bit 11 of segment 1 set
            mask2 = (0, 2048, 0, 1)    # Bit 11 of segment 1 set, bit 0 of segment 3 set
            Result: True (segment 1: 2048 & 2048 = 2048 != 0)
        """
        return any(m1 & m2 for m1, m2 in zip(mask1, mask2))

    # ========================================================================
    # Step 5: Load spell data from spell-data-generator
    # ========================================================================

    def load_spell_data_from_generator(self, generator_output_dir: str):
        """Load GCD/cast time data from spell-data-generator output"""
        classes = [
            'deathknight', 'demonhunter', 'druid', 'evoker',
            'hunter', 'mage', 'monk', 'paladin',
            'priest', 'rogue', 'shaman', 'warlock', 'warrior',
            'general'
        ]

        for class_name in classes:
            filepath = os.path.join(generator_output_dir, f"{class_name}_spells.csv")

            if not os.path.exists(filepath):
                continue

            with open(filepath, 'r', encoding='utf-8') as f:
                next(f)  # Skip header

                for line in f:
                    if not line.strip():
                        continue

                    parts = line.strip().split(',')
                    if len(parts) < 5:
                        continue

                    try:
                        spell_id = int(parts[0])
                        gcd_ms = int(parts[2])
                        cooldown_ms = int(parts[3])
                        cast_time_ms = int(parts[4])

                        if spell_id in self.spell_data:
                            # Update existing entry
                            self.spell_data[spell_id].gcd_ms = gcd_ms
                            if self.spell_data[spell_id].cooldown_ms == 0:
                                self.spell_data[spell_id].cooldown_ms = cooldown_ms
                            self.spell_data[spell_id].cast_time_ms = cast_time_ms
                        else:
                            # Create new entry
                            self.spell_data[spell_id] = SpellData(
                                spell_id=spell_id,
                                gcd_ms=gcd_ms,
                                cooldown_ms=cooldown_ms,
                                cast_time_ms=cast_time_ms,
                                gcd_haste_affected=True,
                                cd_haste_affected=spell_id in HASTE_AFFECTED_COOLDOWNS
                            )

                    except (ValueError, IndexError):
                        continue

    # ========================================================================
    # Step 6: Parse Spell Power Costs (for WCL fallback)
    # ========================================================================

    def parse_spell_power(self):
        """Parse spell power costs from spellpower_export.txt"""
        filepath = self.resolve_export("spellpower")

        if filepath is None:
            print(f"  Warning: {filepath} not found, skipping power cost extraction")
            return

        self.spell_power_costs = {}  # spell_id -> (power_type, cost)

        with open(filepath, 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith('Layout:'):
                    continue

                try:
                    # Format: spell_id: spell_id,field1,flat_cost,f3,f4,f5,f6,pct_cost,f8,f9,f10,f11,power_type_or_unknown,f13,last_field
                    if ':' not in line:
                        continue

                    parts = line.split(':', 1)[1].strip().split(',')
                    if len(parts) < 15:
                        continue

                    spell_id = int(parts[0])
                    flat_cost = int(parts[2])  # Integer cost
                    pct_cost = float(parts[7])  # Percentage/float cost

                    # Power type can be in field 11 or field 14
                    power_type_11 = int(parts[11])
                    power_type_14 = int(parts[14])

                    # Determine cost and power_type
                    cost = 0
                    power_type = 0

                    if flat_cost > 0:
                        cost = flat_cost
                        # For flat costs, power_type is usually in field 11
                        power_type = power_type_11 if power_type_11 > 0 else power_type_14
                    elif pct_cost > 0:
                        # For percentage costs, treat as flat for now
                        # (Most percentage costs are base power percentages)
                        cost = int(pct_cost)
                        # For pct costs, power_type might be in field 14
                        power_type = power_type_14 if power_type_14 < 100 else 0

                    if cost > 0 and power_type > 0 and power_type < 20:
                        self.spell_power_costs[spell_id] = (power_type, cost)

                except (ValueError, IndexError):
                    continue

        print(f"  Loaded {len(self.spell_power_costs)} spell power costs")

    # ========================================================================
    # Step 7: Write binary output files
    # ========================================================================
    #
    # BINARY FILE FORMAT:
    # -------------------
    # Both output files use the same structure:
    #
    # Header:
    #   uint32_t count    // Number of records
    #
    # Records (sorted by primary key for binary search):
    #   struct Record[count]
    #
    # Records are packed with explicit padding for C++ struct compatibility.
    # ========================================================================

    def write_spell_data_binary(self, output_path: str):
        """
        Write spell data to binary file, sorted by spell_id.

        Output format: [count:u32][SpellDataRecord * count]
        Records are sorted by spell_id for efficient binary search at runtime.
        """
        # Sort by spell_id for binary search
        sorted_spells = sorted(self.spell_data.values(), key=lambda s: s.spell_id)

        with open(output_path, 'wb') as f:
            # Write header: count
            f.write(struct.pack('<I', len(sorted_spells)))

            # Write records
            for spell in sorted_spells:
                record = struct.pack(
                    SPELL_DATA_FORMAT,
                    spell.spell_id,
                    spell.gcd_ms,
                    spell.cooldown_ms,
                    spell.cast_time_ms,
                    1 if spell.gcd_haste_affected else 0,
                    1 if spell.cd_haste_affected else 0,
                    spell.base_charges  # NEW: include base charges from category
                )
                f.write(record)

        print(f"Wrote {len(sorted_spells)} spell records to {output_path}")
        print(f"  File size: {os.path.getsize(output_path):,} bytes")

        # Print charge statistics
        charge_spells = sum(1 for s in sorted_spells if s.base_charges > 0)
        print(f"  Spells with charges: {charge_spells}")

    def write_spell_data_human_readable(self, output_dir: str):
        """Write spell data in human-readable CSV and JSON formats"""
        import csv
        import json

        version = self.version

        # CSV output - all spell data
        csv_path = os.path.join(output_dir, f"spell_data_{version}.csv")
        with open(csv_path, 'w', newline='', encoding='utf-8') as f:
            writer = csv.writer(f)
            writer.writerow(['spell_id', 'name', 'gcd_ms', 'cooldown_ms', 'cast_time_ms',
                            'gcd_haste_affected', 'cd_haste_affected', 'base_charges'])

            for spell in sorted(self.spell_data.values(), key=lambda s: s.spell_id):
                name = self.spell_names.get(spell.spell_id, f"Spell {spell.spell_id}")
                writer.writerow([
                    spell.spell_id,
                    name,
                    spell.gcd_ms,
                    spell.cooldown_ms,
                    spell.cast_time_ms,
                    1 if spell.gcd_haste_affected else 0,
                    1 if spell.cd_haste_affected else 0,
                    spell.base_charges
                ])

        print(f"  Wrote CSV: {csv_path}")
        print(f"  File size: {os.path.getsize(csv_path):,} bytes")

        # JSON output - sample of interesting spells
        json_path = os.path.join(output_dir, f"spell_data_sample_{version}.json")

        # Select interesting spells: off-GCD, 1000ms GCD, charge-based, haste-affected CD
        interesting_spells = []
        for spell in self.spell_data.values():
            name = self.spell_names.get(spell.spell_id, f"Spell {spell.spell_id}")

            # Include if: off-GCD, 1000ms GCD, has charges, or haste-affected CD
            if (spell.gcd_ms == 0 or
                spell.gcd_ms == 1000 or
                spell.base_charges > 0 or
                spell.cd_haste_affected):
                interesting_spells.append({
                    'spell_id': spell.spell_id,
                    'name': name,
                    'gcd_ms': spell.gcd_ms,
                    'cooldown_ms': spell.cooldown_ms,
                    'cast_time_ms': spell.cast_time_ms,
                    'gcd_haste_affected': spell.gcd_haste_affected,
                    'cd_haste_affected': spell.cd_haste_affected,
                    'base_charges': spell.base_charges
                })

        with open(json_path, 'w', encoding='utf-8') as f:
            json.dump({
                'version': version,
                'total_spells': len(self.spell_data),
                'interesting_spells_count': len(interesting_spells),
                'description': 'Sample of notable spells: off-GCD, fast GCD, charge-based, or haste-affected cooldowns',
                'spells': sorted(interesting_spells, key=lambda s: s['spell_id'])
            }, f, indent=2)

        print(f"  Wrote JSON sample: {json_path}")
        print(f"  File size: {os.path.getsize(json_path):,} bytes")

    # ========================================================================
    # Statistics
    # ========================================================================

    def print_stats(self):
        """Print extraction statistics"""
        cd_haste_count = sum(1 for s in self.spell_data.values() if s.cd_haste_affected)
        has_cd_count = sum(1 for s in self.spell_data.values() if s.cooldown_ms > 0)
        has_charges_count = sum(1 for s in self.spell_data.values() if s.base_charges > 0)

        # GCD statistics
        off_gcd_count = sum(1 for s in self.spell_data.values() if s.gcd_ms == 0)
        gcd_1000_count = sum(1 for s in self.spell_data.values() if s.gcd_ms == 1000)
        gcd_1500_count = sum(1 for s in self.spell_data.values() if s.gcd_ms == 1500)
        gcd_other_count = sum(1 for s in self.spell_data.values()
                              if s.gcd_ms not in [0, 1000, 1500])

        print(f"\n{'='*60}")
        print(f"Extraction Statistics:")
        print(f"{'='*60}")
        print(f"Spell Data:")
        print(f"  Total spells: {len(self.spell_data)}")
        print(f"  Spells with cooldowns: {has_cd_count}")
        print(f"  Spells with charges: {has_charges_count}")
        print(f"  Cooldowns haste-affected: {cd_haste_count}")
        print(f"\nGCD Statistics:")
        print(f"  Off-GCD (0ms): {off_gcd_count}")
        print(f"  Fast GCD (1000ms): {gcd_1000_count}")
        print(f"  Normal GCD (1500ms): {gcd_1500_count}")
        print(f"  Other GCD values: {gcd_other_count}")
        print(f"\nCategory Data:")
        print(f"  Categories with charge data: {len(self.category_data)}")
        print(f"  Spell -> category links: {len(self.spell_to_charge_category)}")
        print(f"\nTalent Modifiers:")
        print(f"  Total modifiers: {len(self.talent_modifiers)}")
        print(f"  Unique talents: {len(set(m.talent_spell_id for m in self.talent_modifiers))}")
        print(f"  Unique modified spells: {len(set(m.modified_spell_id for m in self.talent_modifiers))}")
        print(f"{'='*60}\n")


# ============================================================================
# Main
# ============================================================================

def main():
    import sys

    print("="*70)
    print("WoW DB2 -> Binary Cooldown Tables Generator")
    print("="*70)

    # Pull --version out before the positional arguments are read, so it can be
    # passed in any position without disturbing them.
    version = DEFAULT_VERSION
    if "--version" in sys.argv:
        idx = sys.argv.index("--version")
        if idx + 1 >= len(sys.argv):
            print("Error: --version needs a value, e.g. --version 12.1")
            return
        version = sys.argv[idx + 1]
        del sys.argv[idx:idx + 2]

    print(f"Target version: {version}")

    # Get DB2 folder path from command line or user input
    if len(sys.argv) > 1:
        db2_dir = str(resolve_db2_dir(sys.argv[1], version))
    elif os.environ.get(DB2_DIR_ENV):
        db2_dir = str(resolve_db2_dir(None, version))
    else:
        default_db2 = default_db2_dir(version)
        print(f"\nDefault DB2 folder: {default_db2}")
        db2_input = input("Enter path to DB2 txt files (or press Enter for default): ").strip().strip('"')
        db2_dir = db2_input if db2_input else str(default_db2)

        if not os.path.exists(db2_dir):
            print(f"Error: Directory not found: {db2_dir}")
            print(f"Pass the export folder as an argument, or set {DB2_DIR_ENV} to it.")
            if sys.stdin.isatty():
                input("Press Enter to exit...")
            return

    # Get output directory (default to script's directory)
    script_dir = os.path.dirname(os.path.abspath(__file__))
    default_output = os.path.join(script_dir, "output")

    if len(sys.argv) > 2:
        output_dir = sys.argv[2]
    else:
        print(f"\nDefault output directory: {default_output}")
        output_dir_input = input("Press Enter to use default, or enter custom path: ").strip().strip('"')
        output_dir = output_dir_input if output_dir_input else default_output

    # Create output directory if it doesn't exist
    os.makedirs(output_dir, exist_ok=True)

    # Optional: spell-data-generator output (can skip if not available)
    spell_gen_dir = None
    if len(sys.argv) > 3:
        # Third argument is spell-data-generator path
        spell_gen_input = sys.argv[3].strip().strip('"')
        if spell_gen_input and os.path.exists(spell_gen_input):
            spell_gen_dir = spell_gen_input
    else:
        # Try interactive input if stdin is available
        try:
            print("\n(Optional) Enter path to spell-data-generator output folder")
            spell_gen_input = input("Press Enter to skip, or enter path: ").strip().strip('"')
            if spell_gen_input and os.path.exists(spell_gen_input):
                spell_gen_dir = spell_gen_input
        except EOFError:
            # Non-interactive mode, skip
            pass

    parser = DB2CooldownParser(db2_dir, version)

    print("="*60)
    print("Building Cooldown Tables from DB2 Files")
    print("="*60)

    # Parse DB2 files
    print("\n[1/15] Parsing spellcooldowns_export.txt...")
    parser.parse_spellcooldowns()

    print("[2/15] Parsing spellcategory_export.txt (charges/cooldowns)...")
    parser.parse_spellcategory()

    print("[3/15] Parsing spellcategories_export.txt (spell->category links)...")
    parser.parse_spellcategories()

    print("[4/15] Parsing spellclassoptions_export.txt...")
    parser.parse_spellclassoptions()

    print("[5/15] Parsing spelleffect_export.txt...")
    parser.parse_spelleffect()

    print("[6/15] Parsing spelllabel_export.txt...")
    parser.parse_spell_labels()

    print("[7/15] Parsing spellpower_export.txt...")
    parser.parse_spell_power()

    print("[8/15] Parsing spell_export.txt (descriptions)...")
    parser.parse_spell_descriptions()

    print("[9/15] Parsing spellname_export.txt (names)...")
    parser.parse_spell_names()

    print("[10/15] Parsing traitnodeentry_export.txt (WCL talent IDs)...")
    parser.parse_traitnodeentry()

    print("[11/16] Matching effects to spells...")
    parser.build_talent_modifiers()

    print("[12/16] Applying category-based cooldowns and charges...")
    parser.apply_category_data()

    print("[13/16] Detecting charge-granting talent modifiers...")
    parser.detect_charge_modifiers()

    # Load spell-data-generator CSV files (PRIMARY GCD source)
    if not spell_gen_dir:
        # Default to the spell-data-generator output beside this tool
        tools_dir = os.path.dirname(script_dir)
        spell_gen_dir = os.path.join(tools_dir, "spell-data-generator", "output")

    if not os.path.exists(spell_gen_dir):
        print(f"ERROR: spell-data-generator output not found at {spell_gen_dir}")
        print("Please run spell-data-generator first to extract GCD data")
        print("  cd tools/spell-data-generator")
        print(f"  python extract_spell_gcd.py \"{db2_dir}\"")
        sys.exit(1)

    print("[14/16] Loading spell-data-generator output (PRIMARY GCD source)...")
    parser.load_spell_data_from_generator(spell_gen_dir)

    # Write binary outputs
    print("\n[15/16] Writing binary output files...")
    spell_data_path = os.path.join(output_dir, f"spell_data_{version}.bin")

    parser.write_spell_data_binary(spell_data_path)

    # Write human-readable CSV for inspection
    parser.write_spell_data_human_readable(output_dir)

    # Statistics
    print("\n[16/16] Complete!")
    parser.print_stats()

    print("\n" + "="*70)
    print("SUCCESS!")
    print("="*70)
    print(f"\nGenerated files in: {output_dir}")
    print(f"  - spell_data_{version}.bin ({os.path.getsize(spell_data_path):,} bytes)")
    print(f"\nCopy spell_data_{version}.bin to your AwowLab data/ folder")

    if sys.stdin.isatty():
        input("\nPress Enter to exit...")


if __name__ == "__main__":
    main()
