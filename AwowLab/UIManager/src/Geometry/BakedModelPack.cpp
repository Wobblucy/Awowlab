#include "Geometry/BakedModelPack.h"
#include <cstring>
#include <fstream>

// The pack stores vertices in the app's exact GPU wire layout; if Vertex3D ever
// changes, the bake tool and this assert must change together.
static_assert(sizeof(Vertex3D) == 44, "Vertex3D layout changed - rebake .awmp packs");

namespace {

// Little bounds-checked cursor over the blob.
struct Cursor {
    const uint8_t* p;
    size_t size;
    size_t off = 0;

    bool read(void* out, size_t n) {
        if (off + n > size) return false;
        std::memcpy(out, p + off, n);
        off += n;
        return true;
    }

    template <typename T>
    bool readT(T& out) { return read(&out, sizeof(T)); }

    bool readString(std::string& out, size_t n) {
        if (off + n > size) return false;
        out.assign(reinterpret_cast<const char*>(p + off), n);
        off += n;
        return true;
    }
};

} // namespace

bool BakedModelPack::load(const std::string& path) {
    // Parse into locals and only replace the current pack on success, so a
    // broken second pack doesn't wipe one that already loaded. The views
    // point into blob's heap buffer, which survives the move below.
    std::vector<uint8_t> blob;
    std::unordered_map<std::string, BakedModelView> models;
    std::vector<std::pair<uint32_t, std::string>> assignments;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return false;

    const std::streamsize fileSize = file.tellg();
    if (fileSize < 16)
        return false;

    blob.resize(static_cast<size_t>(fileSize));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(blob.data()), fileSize))
        return false;

    Cursor c{blob.data(), blob.size()};

    char magic[4];
    uint32_t version = 0, modelCount = 0, assignCount = 0;
    if (!c.read(magic, 4) || std::memcmp(magic, "AWMP", 4) != 0) return false;
    if (!c.readT(version) || version != 2) return false;
    if (!c.readT(modelCount) || !c.readT(assignCount)) return false;

    models.reserve(modelCount);
    for (uint32_t i = 0; i < modelCount; i++) {
        uint16_t nameLen = 0;
        std::string name;
        uint32_t vertCount = 0, idxCount = 0;
        uint64_t vertOff = 0, idxOff = 0;
        if (!c.readT(nameLen) || !c.readString(name, nameLen) ||
            !c.readT(vertCount) || !c.readT(idxCount) ||
            !c.readT(vertOff) || !c.readT(idxOff)) {
            return false;
        }

        // Validate blob ranges + alignment before creating a view.
        const uint64_t vertBytes = uint64_t(vertCount) * sizeof(Vertex3D);
        const uint64_t idxBytes = uint64_t(idxCount) * sizeof(uint32_t);
        if (vertOff % 4 != 0 || idxOff % 4 != 0 ||
            vertOff > blob.size() || idxOff > blob.size() ||
            vertBytes > blob.size() - vertOff || idxBytes > blob.size() - idxOff ||
            vertCount == 0 || idxCount == 0 || idxCount % 3 != 0) {
            continue;  // skip malformed entry, keep the rest
        }

        // Every index must land on a vertex, or the draw reads past the
        // vertex buffer
        const uint32_t* indices = reinterpret_cast<const uint32_t*>(blob.data() + idxOff);
        bool indicesInRange = true;
        for (uint32_t k = 0; k < idxCount; k++) {
            if (indices[k] >= vertCount) {
                indicesInRange = false;
                break;
            }
        }
        if (!indicesInRange) {
            continue;
        }

        BakedModelView view;
        view.vertices = reinterpret_cast<const Vertex3D*>(blob.data() + vertOff);
        view.vertexCount = vertCount;
        view.indices = indices;
        view.indexCount = idxCount;
        models[std::move(name)] = view;
    }

    if (models.empty())
        return false;

    assignments.reserve(assignCount);
    for (uint32_t i = 0; i < assignCount; i++) {
        uint32_t npcId = 0;
        uint16_t mLen = 0;
        std::string model;
        if (!c.readT(npcId) || !c.readT(mLen) || !c.readString(model, mLen)) {
            break;  // keep what we have
        }
        // Only keep assignments whose model actually exists in this pack.
        if (npcId != 0 && models.find(model) != models.end())
            assignments.emplace_back(npcId, std::move(model));
    }

    blob_ = std::move(blob);
    models_ = std::move(models);
    assignments_ = std::move(assignments);
    return true;
}

const BakedModelView* BakedModelPack::find(const std::string& name) const {
    auto it = models_.find(name);
    return (it != models_.end()) ? &it->second : nullptr;
}
