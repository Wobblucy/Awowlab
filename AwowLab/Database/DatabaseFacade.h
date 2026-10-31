#pragma once

// Forward declarations
class EventDatabase;
class CombatDatabase;
class ResourceDatabase;
class AuraDatabase;
class DeathDatabase;
class SpellCastDatabase;
class DispelDatabase;
class AvoidanceDatabase;

// DatabaseFacade - Unified access to all combat log databases
// Reduces coupling by providing a single object that can be passed instead of
// 8+ separate database pointers. Components only need to include this header
// instead of all individual database headers.
//
// Usage:
//   DatabaseFacade databases;
//   databases.setEventDatabase(&eventDb);
//   databases.setCombatDatabase(&combatDb);
//   // ... set others
//
//   // Pass to components:
//   myComponent.loadDatabases(databases);
//
// The facade does not own the databases; it only holds pointers to them.
// The actual database objects should be owned by DatabaseBundle or similar.

class DatabaseFacade {
public:
    DatabaseFacade() = default;

    // Setters (non-owning pointers)
    void setEventDatabase(const EventDatabase* db) { events_ = db; }
    void setCombatDatabase(const CombatDatabase* db) { combat_ = db; }
    void setResourceDatabase(const ResourceDatabase* db) { resources_ = db; }
    void setAuraDatabase(const AuraDatabase* db) { auras_ = db; }
    void setDeathDatabase(const DeathDatabase* db) { deaths_ = db; }
    void setSpellCastDatabase(const SpellCastDatabase* db) { spellCasts_ = db; }
    void setDispelDatabase(const DispelDatabase* db) { dispels_ = db; }
    void setAvoidanceDatabase(const AvoidanceDatabase* db) { avoidance_ = db; }

    // Getters
    const EventDatabase* getEventDatabase() const { return events_; }
    const CombatDatabase* getCombatDatabase() const { return combat_; }
    const ResourceDatabase* getResourceDatabase() const { return resources_; }
    const AuraDatabase* getAuraDatabase() const { return auras_; }
    const DeathDatabase* getDeathDatabase() const { return deaths_; }
    const SpellCastDatabase* getSpellCastDatabase() const { return spellCasts_; }
    const DispelDatabase* getDispelDatabase() const { return dispels_; }
    const AvoidanceDatabase* getAvoidanceDatabase() const { return avoidance_; }

    // Convenience checks
    bool hasEventDatabase() const { return events_ != nullptr; }
    bool hasCombatDatabase() const { return combat_ != nullptr; }
    bool hasResourceDatabase() const { return resources_ != nullptr; }
    bool hasAuraDatabase() const { return auras_ != nullptr; }
    bool hasDeathDatabase() const { return deaths_ != nullptr; }
    bool hasSpellCastDatabase() const { return spellCasts_ != nullptr; }
    bool hasDispelDatabase() const { return dispels_ != nullptr; }
    bool hasAvoidanceDatabase() const { return avoidance_ != nullptr; }

    bool isComplete() const {
        return events_ && combat_ && resources_ && auras_ && deaths_ && spellCasts_;
    }

    // Check if all databases including optional ones are loaded
    bool isFullyComplete() const {
        return isComplete() && dispels_ && avoidance_;
    }

    void clear() {
        events_ = nullptr;
        combat_ = nullptr;
        resources_ = nullptr;
        auras_ = nullptr;
        deaths_ = nullptr;
        spellCasts_ = nullptr;
        dispels_ = nullptr;
        avoidance_ = nullptr;
    }

private:
    const EventDatabase* events_ = nullptr;
    const CombatDatabase* combat_ = nullptr;
    const ResourceDatabase* resources_ = nullptr;
    const AuraDatabase* auras_ = nullptr;
    const DeathDatabase* deaths_ = nullptr;
    const SpellCastDatabase* spellCasts_ = nullptr;
    const DispelDatabase* dispels_ = nullptr;
    const AvoidanceDatabase* avoidance_ = nullptr;
};
