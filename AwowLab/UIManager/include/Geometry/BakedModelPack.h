#pragma once
#include "SphereGeometry.h"  // Vertex3D
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// A baked model pack (.awmp) holds pre-processed creature models in the exact
// Vertex3D wire layout: decimated, unit-cube normalized, normals precomputed.
// Loading a model from the pack is a straight memcpy into GPU staging - no OBJ
// parsing, no worker thread, no normalization. Produced offline by
// tools/model-extractor/bake_season_models.py.
//
// Assignments are keyed by npcId (taken from the unit GUID), not creature name:
// names carry a per-spawn instance suffix at runtime ("Venom Leech (12)") and
// are localized, so neither form matches a baked table.
//
// Format (little-endian, offsets absolute, blobs 4-byte aligned):
//   'AWMP' u32 version=2  u32 modelCount  u32 assignCount
//   models:  [u16 nameLen, name, u32 vertCount, u32 idxCount,
//             u64 vertOffset, u64 idxOffset] * modelCount
//   assigns: [u32 npcId, u16 mLen, modelName] * assignCount
//   blobs

// Zero-copy view into the pack blob. Valid while the pack is alive.
struct BakedModelView {
    const Vertex3D* vertices = nullptr;
    uint32_t vertexCount = 0;
    const uint32_t* indices = nullptr;
    uint32_t indexCount = 0;
};

class BakedModelPack {
public:
    // Single sequential read of the whole pack; builds name -> view index.
    // Models with out-of-range data are skipped. Returns false on any
    // structural error or if no model survives, and then keeps whatever
    // pack was loaded before.
    bool load(const std::string& path);

    const BakedModelView* find(const std::string& name) const;

    bool empty() const { return models_.empty(); }
    size_t modelCount() const { return models_.size(); }

    const std::unordered_map<std::string, BakedModelView>& models() const { return models_; }

    // Embedded npcId -> model-name defaults table.
    const std::vector<std::pair<uint32_t, std::string>>& assignments() const { return assignments_; }

private:
    std::vector<uint8_t> blob_;  // whole file; views point into this
    std::unordered_map<std::string, BakedModelView> models_;
    std::vector<std::pair<uint32_t, std::string>> assignments_;
};
