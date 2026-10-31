# AwowLab Architecture

How the code is organised and how a combat log becomes a replay. Building and
running are covered in the [README](../README.md).

## Table of Contents
- [Module Details](#module-details)
- [Token Index Structure](#token-index-structure)
- [Parser System](#parser-system)
- [Database Layer](#database-layer)
- [UIManager](#uimanager)
- [Release System](#release-system)

---

## Module Details

### 1. MMapManager (Memory-Mapped File I/O)

**Location:** `AwowLab/MMapManager/`

The `MMapFile` class provides cross-platform memory-mapped file access:
- **Windows:** Uses `CreateFileA`, `CreateFileMappingA`, `MapViewOfFile`
- **Unix/Linux:** Uses `open`, `mmap`
- Move-only semantics (no copies), RAII-based resource management
- Returns raw `const char*` pointer and file size for zero-copy parsing

```cpp
MMapFile mmapFile;
mmapFile.open(filepath);
const char* data = mmapFile.get_data();
size_t size = mmapFile.get_size();
```

### 2. Txttokenizer (Log Tokenization)

**Location:** `AwowLab/Txttokenizer/`

The `tokenized_segment` class parses raw combat log text into tokens:
- **Input:** Memory-mapped pointer and size
- **Output:** `std::vector<std::vector<std::string_view>>` (lines of tokens)
- Zero-copy parsing using `std::string_view`
- Handles quoted strings, UTF-8 characters, comma-delimited format

**Key methods:**
- `tokenize()` - Full tokenization
- `tokenizeIncremental()` - Incremental parsing with byte tracking

```cpp
struct TokenizeResult {
    std::vector<std::vector<std::string_view>> lines;
    size_t bytes_consumed;
};
```

### 3. Parser (Event Parsing)

**Location:** `AwowLab/Parser/`

Template-based parser system converting tokenized lines into structured data.

**Architecture:**
- `EventParser<EventType>` template specializations
- Each event type has its own header file (`Parser_*.h`)
- Common utilities in `parser::util` namespace

### 4. Database Layer

**Location:** `AwowLab/Database/`

Specialized query databases operating as facades over central `ActorMap`.

**ActorMap (Central Store):** `Parser/Include/Actor/ActorMap.h`
- `std::unordered_map<GUID, ActorTable>` storing all parsed data per actor
- `ActorTable` contains: combat tables, resource tables, aura events, position events

**Specialized Databases (Non-owning facades):**
- `CombatDatabase.h` - DPS/HPS metrics, pet attribution
- `AuraDatabase.h` - Buff/debuff tracking
- `ResourceDatabase.h` - Health/power queries
- `DeathDatabase.h` - Death/resurrection timeline
- `SpellCastDatabase.h` - Spell cast history
- `EventDatabase.h` - Position events indexed by timestamp

### 5. Core Utilities

**Location:** `AwowLab/Core/`

- `Application.h/.cpp` - Main loop, landing page, encounter selection
- `LogSession.h/.cpp` - Memory-efficient log management with on-demand tokenization
- `LRUCache.h` - Generic LRU cache template with eviction callbacks

---

## Token Index Structure

Defined in `Parser/Include/structures.h`:

```
Base indices (all events):
  0: Date, 1: Time, 2: EventType
  3: SourceGUID, 4: SourceName, 5: SourceFlags, 6: SourceRaidFlags
  7: DestGUID, 8: DestName, 9: DestFlags, 10: DestRaidFlags

Spell prefix (SPELL_*, RANGE_*):
  11: SpellID, 12: SpellName, 13: SpellSchool

Advanced Combat Log - SPELL events (14-32):
  14: InfoGUID, 15: OwnerGUID
  16-17: CurrentHP, MaxHP
  18-21: AttackPower, SpellPower, Armor, Absorb
  22-27: PowerType, CurrentPower, MaxPower, PowerCost, AltPowerType, AltPowerAmount
  28-31: PosX, PosY, MapID, Facing
  32: Level

Advanced Combat Log - SWING events (11-29):
  Same structure as SPELL but offset by -3 (no spell prefix)
```

---

## Parser System

### Supported Event Parsers

| File | Events |
|------|--------|
| `Parser_Damage.h` | SPELL_DAMAGE, SPELL_PERIODIC_DAMAGE, SWING_DAMAGE_LANDED |
| `Parser_Heal.h` | SPELL_HEAL, SPELL_PERIODIC_HEAL |
| `Parser_Absorb.h` | SPELL_ABSORBED, SPELL_ABSORBED_SUPPORT |
| `Parser_Aura.h` | SPELL_AURA_APPLIED/REMOVED/REFRESH, dose events |
| `Parser_Cast.h` | SPELL_CAST_START/SUCCESS/FAILED, SPELL_EMPOWER_* |
| `Parser_Dispel.h` | SPELL_DISPEL, SPELL_INTERRUPT, SPELL_STOLEN |
| `Parser_Missed.h` | SPELL_MISSED, SWING_MISSED, SPELL_PERIODIC_MISSED |
| `Parser_Resource.h` | SPELL_ENERGIZE, SPELL_PERIODIC_ENERGIZE |
| `Parser_Meta.h` | COMBAT_LOG_VERSION, MAP_CHANGE, ZONE_CHANGE |
| `Parser_Encounter.h` | ENCOUNTER_START/END |
| `Parser_ChallengeMode.h` | CHALLENGE_MODE_START/END |
| `Parser_Combatant_Info.h` | COMBATANT_INFO |
| `Parser_Unit.h` | UNIT_DIED, PARTY_KILL |
| `Parser_Support.h` | *_SUPPORT events |

### Event Data Structures

- `BaseEventData` - timestamp, source/dest GUID/name/flags
- `SpellInfo` - spell_id, name, school
- `AdvancedUnitInfo` - HP, power, stats, position
- `DamageEventData` - amounts, overkill, crit/glancing/crushing
- `HealEventData` - amounts, overhealing, crit
- `MissedEventData` - miss type, amounts
- `AuraEventData` - aura type, stack count
- `EnergizeEventData` - power gain, type

### UnitFlags Parsing

From hex sourceFlags/destFlags:
- **Affiliation:** Mine, Party, Raid, Outsider (bits 0-3)
- **Reaction:** Friendly, Neutral, Hostile (bits 4-7)
- **Controller:** Player, NPC (bits 8-9)
- **UnitType:** Player, NPC, Pet, Guardian, Object (bits 10-15)
- **Special:** Target, Focus, MainTank, MainAssist (bits 16+)
- **RaidMarker:** Star, Circle, Diamond, Triangle, Moon, Square, Cross, Skull

### Coordinate Conversions

- Position (x, y): multiply by 100, round to int32_t
- Rotation: multiply by 10000, round to uint16_t

---

## Database Layer

### Key Structures

```cpp
struct ActorEvent {
    std::string source_guid;
    std::string source_actor;
    ActorType actor_type;
    UnitFlags flags;
    uint32_t timestamp_ms;
    float x_loc, y_loc;
    uint16_t map_id;
};

struct ExtractedData {
    std::vector<ActorEvent> actorEvents;
    std::vector<SpellCastEvent> spellCastEvents;
    std::vector<DeathEvent> deathEvents;
    std::vector<ResurrectEvent> resurrectEvents;
    std::vector<MapChangeEvent> mapChanges;
    std::vector<CombatantInfoEvent> combatantInfo;
    std::unique_ptr<ActorMap> actorMap;
};
```

### EventDatabase

- `std::multimap<uint32_t, ActorEvent>` for O(log n) time-windowed queries
- Zero-copy iteration via `forEachEventInWindow()` callbacks
- Stores min/max timestamps for timeline controls

---

## UIManager

**Location:** `AwowLab/UIManager/`

### Directory Structure

```
UIManager/
├── include/
│   ├── Animation/          # Position interpolation
│   ├── Camera/             # 3D camera system
│   ├── Color/              # Actor color generation
│   ├── Core/               # Main orchestration
│   ├── Geometry/           # Mesh generation
│   ├── Input/              # Mouse raycasting
│   ├── Playback/           # Timeline control
│   ├── Rendering/          # 2D/3D rendering
│   ├── UI/                 # ImGui panels
│   └── Vulkan/             # Vulkan abstraction
└── src/
```

### Core Subsystem (Facade Pattern)

- `MapVisualizer.h` - Thin facade coordinating:
  - `FrameDataProvider` - Event database access, position interpolation
  - `RenderingCoordinator` - 3D/2D rendering, SpellIconLoader
  - `UIWindowManager` - UI panel orchestration

### Vulkan Pipeline

| Component | Purpose |
|-----------|---------|
| `VulkanInstance.h` | Instance and validation layers |
| `VulkanDevice.h` | Device selection and queues |
| `VulkanSwapchain.h` | Swapchain management |
| `VulkanCommands.h` | Command buffer allocation |
| `VulkanSync.h` | Semaphores and fences |
| `VulkanPipeline3D.h` | Graphics pipeline |
| `VulkanRenderer.h` | Frame submission |
| `VulkanTextureManager.h` | Texture creation |

### UI Panels

| Panel | Purpose |
|-------|---------|
| `UIPlaybackPanel` | Timeline scrubbing, play/pause |
| `UICreaturesPanel` | Actor list with colors |
| `UIFiltersPanel` | Visibility filters |
| `UIActorSelector` | Player focus selection |
| `UISpellHistory` | GCD history with icons |
| `UIEncounterSelector` | Encounter selection |
| `UIActorAppearancePanel` | Per-actor customization |
| `UILandingPage` | Initial screen |
| `UICachedFightsPanel` | Cached WCL fights |
| `UIRaidFramePanel` | Raid frames grid |
| `UIEnemyGridPanel` | Enemy grid |
| `UICombatMeterPanel` | DPS meter |
| `UIHealingMeterPanel` | HPS meter |
| `UIDamageTakenPanel` | Damage taken |
| `UIDeathsPanel` | Death log |

### Geometry Types

- `SphereGeometry` - Default player shape
- `CubeGeometry` - Default NPC shape
- `ShieldGeometry` - Tank indicator
- `MedicCrossGeometry` - Healer indicator
- `SwordGeometry` - Melee DPS indicator
- `BowGeometry` - Ranged DPS indicator
- `PlaneGeometry` - Floor plane

### Actor Appearance System

`UI/ActorAppearanceSettings.h`:
- `ActorRole` enum: Unknown, Tank, Healer, MeleeDPS, RangedDPS
- `ActorShape` enum: Sphere, Cube, Shield, MedicCross, Sword, Bow
- Automatic role detection from COMBATANT_INFO

### Camera System

RTS/MOBA-style 3rd person camera:
- Actors in X-Y plane, Z axis up
- Default 45-degree tilt (adjustable 20-80)
- RMB drag: Rotate/tilt
- LMB/Middle drag: Pan
- Mouse wheel: Zoom (20-500 units)

---

## Release System

### Manifest Format

```json
{
  "version": "0.17.0",
  "generated_at": "2026-01-04T12:00:00Z",
  "assets_tag": "assets-v2",
  "files": [
    { "path": "AwowLab.exe", "sha256": "...", "size": 8388608, "url": "..." }
  ],
  "archives": [
    { "extract_to": "PNG/Spell Icons", "sha256": "...", "url": ".../assets-v2/SpellIcons.zip" },
    { "extract_to": "shaders", "sha256": "...", "url": ".../v0.17.0/shaders.zip" }
  ],
  "folders": ["custom_models", "triggers", "triggers/defaults"]
}
```

### Installed Directory Structure

```
AwowLab/
├── AwowLab.exe
├── awowlab.ico
├── version.json
├── updater/AwowLabUpdater.exe
├── shaders/
├── triggers/
│   ├── defaults/           # Shipped defaults
│   └── *.json              # User overrides
├── data/
│   ├── fonts/
│   ├── lang/
│   ├── spellnames/
│   └── *.bin, *.csv, *.json
└── PNG/
    ├── Spell Icons/
    ├── Worldmaps/defaults/
    └── EncounterMaps/defaults/
```

**Asset Layering:** User files in root folders override shipped defaults in `defaults/` subfolders.

### GitHub Release Structure

**Static Assets (`assets-v2`):** ~57 MB (uploaded once)
- `SpellIcons.zip` - Culled spell icons
- `Worldmaps.zip` - World map images

**Versioned Release (`v0.X.Y`):** ~15 MB per version
- `AwowLabUpdater.exe`, `AwowLab.exe`, `awowlab.ico`
- `manifest.json`, `version.json`
- `shaders.zip`, `data.zip`
