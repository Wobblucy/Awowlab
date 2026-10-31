#pragma once
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <functional>
#include <utility>
#include <vector>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <array>
#include "../../Database/OutputWriter.h"  // For ActorType
#include "../../Parser/Include/structures.h"  // For UnitReaction
#include "Core/UnifiedSettings.h"

// Role classification for automatic shape assignment
enum class ActorRole : uint8_t {
    Unknown = 0,     // Default - uses sphere
    Tank,            // Shield shape
    Healer,          // Medic cross shape
    MeleeDPS,        // Sword shape
    RangedDPS        // Bow shape
};

// Shape types available for actors
enum class ActorShape : uint8_t {
    Sphere = 0,      // Default for Players
    Cube,            // Default for Non-players
    Shield,          // Tank role
    MedicCross,      // Healer role
    Sword,           // Melee DPS role
    Bow,             // Ranged DPS role
    Cone,            // Pointy cone
    Cylinder,        // Barrel/pill shape
    Diamond,         // Octahedron (double pyramid)
    Pyramid,         // Single pyramid
    Star,            // 3D star
    ClassIcon,       // Textured disc with spec icon
    COUNT            // For iteration/array sizing
};

// Individual appearance override (color, shape, size)
struct AppearanceOverride {
    std::optional<ActorShape> shape;
    std::optional<std::string> customShapeName;  // Takes precedence over shape if set
    std::optional<float> r, g, b;     // Color override (0-1 range)
    std::optional<float> scale;        // Size multiplier (default 1.0)
    std::optional<float> rotationX, rotationY, rotationZ;  // Rotation in degrees (for custom models)

    bool hasAnyOverride() const {
        return shape.has_value() || customShapeName.has_value() || r.has_value() || scale.has_value() ||
               rotationX.has_value() || rotationY.has_value() || rotationZ.has_value();
    }

    void clear() {
        shape.reset();
        customShapeName.reset();
        r.reset();
        g.reset();
        b.reset();
        scale.reset();
        rotationX.reset();
        rotationY.reset();
        rotationZ.reset();
    }
};

// Resolved appearance for rendering (no optionals - all values concrete)
struct ResolvedAppearance {
    ActorShape shape = ActorShape::Sphere;
    std::string customShapeName;  // Non-empty if using custom shape
    float r = 1.0f, g = 1.0f, b = 1.0f;
    float scale = 1.0f;
    float rotationX = 0.0f, rotationY = 0.0f, rotationZ = 0.0f;  // Rotation in degrees

    bool isCustomShape() const { return !customShapeName.empty(); }
};

// Custom color preset (saved by user)
struct ColorPreset {
    float r = 0.5f, g = 0.5f, b = 0.5f;
    bool isSet = false;  // True if user has saved a color here
};

// Role-based default shapes (editable by user)
struct RoleDefaults {
    ActorShape tankShape = ActorShape::ClassIcon;
    ActorShape healerShape = ActorShape::ClassIcon;
    ActorShape meleeDPSShape = ActorShape::ClassIcon;
    ActorShape rangedDPSShape = ActorShape::ClassIcon;
    ActorShape unknownPlayerShape = ActorShape::Sphere;
    ActorShape creatureShape = ActorShape::Sphere;
    ActorShape vehicleShape = ActorShape::Cube;
    ActorShape petShape = ActorShape::Sphere;

    float defaultScale = 1.0f;
    bool autoAssignRoleShapes = true;  // Enable/disable automatic role detection

    // Custom saved color presets (up to 8 slots)
    static constexpr size_t MAX_CUSTOM_COLORS = 8;
    std::array<ColorPreset, MAX_CUSTOM_COLORS> customColors;
};

class ActorAppearanceSettings {
public:
    ActorAppearanceSettings() = default;

    // Resolve final appearance for an actor
    ResolvedAppearance resolveAppearance(
        const std::string& guid,
        const std::string& actorName, // For name group overrides
        ActorType actorType,
        UnitReaction reaction,        // For health-based scaling (hostile/neutral only)
        uint16_t specId,              // 0 if unknown
        float defaultR, float defaultG, float defaultB  // Existing color from system
    ) const;

    // Health-based scaling for hostile/neutral Creatures
    void setAveragePlayerHealth(uint64_t avgHealth);
    void cacheActorMaxHealth(const std::string& guid, uint64_t maxHealth);
    void clearHealthData();
    float getHealthBasedScale(const std::string& guid, ActorType actorType, UnitReaction reaction) const;

    // GUID Override management
    void setGuidOverride(const std::string& guid, const AppearanceOverride& override);
    void clearGuidOverride(const std::string& guid);
    bool hasGuidOverride(const std::string& guid) const;
    const AppearanceOverride* getGuidOverride(const std::string& guid) const;
    const std::unordered_map<std::string, AppearanceOverride>& getAllGuidOverrides() const { return guidOverrides_; }

    // Baked-in default model assignments (npcId -> custom model name).
    // Keyed by npcId rather than creature name: runtime names carry a per-spawn
    // instance suffix ("Venom Leech (12)") and are localized, so a name key
    // would miss. The npcId comes from the unit GUID.
    // Lowest-priority layer: a user NameGroup override for the same actor is
    // applied on top (per-field), so user tweaks always win. Never serialized.
    void setDefaultModelAssignments(const std::vector<std::pair<uint32_t, std::string>>& assignments);

    // Existence check for custom model names, used to ignore stale
    // customShapeName references in saved overrides (models renamed/removed
    // between sessions). Without this a dead reference silently forces the
    // sphere fallback and shadows the baked default model.
    void setCustomShapeValidator(std::function<bool(const std::string&)> validator) {
        customShapeValidator_ = std::move(validator);
    }

    // WarcraftLogs actor id -> real creature id. WCL builds synthetic GUIDs from
    // its own per-report actor ids ("Creature-0-0-0-0-16-3"), so the id field of
    // the GUID is an actor id, not a creature id; this table maps it across.
    // Empty for real combat logs, where the GUID holds the npc id directly.
    void setActorNpcIdMap(std::unordered_map<int32_t, uint32_t> map) {
        actorIdToNpcId_ = std::move(map);
    }

    // Name Group Override management (for hostile actor grouping by name)
    void setNameGroupOverride(const std::string& name, const AppearanceOverride& override);
    void clearNameGroupOverride(const std::string& name);
    bool hasNameGroupOverride(const std::string& name) const;
    const AppearanceOverride* getNameGroupOverride(const std::string& name) const;
    const std::unordered_map<std::string, AppearanceOverride>& getAllNameGroupOverrides() const { return nameGroupOverrides_; }

    // Spec Override management
    void setSpecOverride(uint16_t specId, const AppearanceOverride& override);
    void clearSpecOverride(uint16_t specId);
    bool hasSpecOverride(uint16_t specId) const;
    const AppearanceOverride* getSpecOverride(uint16_t specId) const;

    // Class Override management
    void setClassOverride(const std::string& className, const AppearanceOverride& override);
    void clearClassOverride(const std::string& className);
    bool hasClassOverride(const std::string& className) const;
    const AppearanceOverride* getClassOverride(const std::string& className) const;

    // Role Override management
    void setRoleOverride(ActorRole role, const AppearanceOverride& override);
    void clearRoleOverride(ActorRole role);
    bool hasRoleOverride(ActorRole role) const;
    const AppearanceOverride* getRoleOverride(ActorRole role) const;

    // Role defaults access
    RoleDefaults& getRoleDefaults() { return roleDefaults_; }
    const RoleDefaults& getRoleDefaults() const { return roleDefaults_; }

    // Favorites management
    void addFavorite(const std::string& guid);
    void removeFavorite(const std::string& guid);
    void toggleFavorite(const std::string& guid);
    bool isFavorite(const std::string& guid) const;
    const std::unordered_set<std::string>& getAllFavorites() const { return favorites_; }
    void clearFavorites();

    // Persistence (uses encrypted unified settings)
    bool saveToFile(const std::string& filename = "") const;
    bool loadFromFile(const std::string& filename = "");

    // Serialization to/from string (INI format)
    std::string serializeToString() const;
    bool deserializeFromString(const std::string& data);

    // Utility functions
    static ActorRole getRoleFromSpecId(uint16_t specId);
    static std::string getClassFromSpecId(uint16_t specId);
    static const char* getShapeName(ActorShape shape);
    static ActorShape getShapeFromName(const std::string& name);
    static const char* getRoleName(ActorRole role);
    static ActorRole getRoleFromName(const std::string& name);

private:
    RoleDefaults roleDefaults_;

    // Override maps (priority: GUID > NameGroup > Spec > Class > Role > Default)
    std::unordered_map<std::string, AppearanceOverride> guidOverrides_;
    std::unordered_map<std::string, AppearanceOverride> nameGroupOverrides_;  // Actor name -> override
    std::unordered_map<uint32_t, AppearanceOverride> defaultNpcOverrides_;  // Baked model defaults by npcId (not persisted)
    std::unordered_map<int32_t, uint32_t> actorIdToNpcId_;  // WCL actor id -> creature id (empty for real logs)
    std::function<bool(const std::string&)> customShapeValidator_;  // Model-name existence check (drops stale refs)

    // Creature id for an actor. For WCL replays the GUID's id field is a WCL
    // actor id that has to be mapped across; for real combat logs it already is
    // the creature id.
    uint32_t npcIdForGuid(const std::string& guid) const;

    // applyOverride, but with a stale customShapeName reference ignored so the
    // override's other fields (color/scale) still apply.
    void applyOverrideValidated(ResolvedAppearance& result, const AppearanceOverride& override) const;
    std::unordered_map<uint16_t, AppearanceOverride> specOverrides_;
    std::unordered_map<std::string, AppearanceOverride> classOverrides_;
    std::unordered_map<uint8_t, AppearanceOverride> roleOverrides_;  // Key is ActorRole cast to uint8_t

    // Health-based scaling data (for hostile/neutral Creatures)
    uint64_t avgPlayerMaxHealth_ = 0;
    std::unordered_map<std::string, uint64_t> actorMaxHealth_;  // GUID -> max_health

    // Favorites (starred actors for quick access)
    std::unordered_set<std::string> favorites_;

    // Internal resolution helpers
    ActorShape resolveDefaultShape(ActorType actorType, ActorRole role) const;
    void applyOverride(ResolvedAppearance& result, const AppearanceOverride& override) const;
};
