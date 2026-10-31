#define TINYOBJLOADER_DISABLE_FAST_FLOAT
#define TINYOBJLOADER_IMPLEMENTATION
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4505) // unreferenced function with internal linkage
#pragma warning(disable: 4456) // declaration hides previous local (tinyobjloader internals)
#endif
#include "tiny_obj_loader.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "Geometry/CustomModelLoader.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include "Core/ErrorLogger.h"
#include <cfloat>
#include <unordered_map>
#include <map>

std::vector<LoadedModel> CustomModelLoader::scanAndLoadModels(const std::string& directory) {
    std::vector<LoadedModel> models;
    errors_.clear();

    namespace fs = std::filesystem;

    if (!fs::exists(directory)) {
        try {
            fs::create_directories(directory);
        } catch (const std::exception& e) {
            errors_.push_back({"<directory>", std::string("Failed to create directory: ") + e.what()});
            return models;
        }
    }

    // Recursive, so models can be organised in subfolders
    try {
        for (const auto& entry : fs::recursive_directory_iterator(directory)) {
            if (entry.is_regular_file()) {
                auto ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (ext == ".obj") {
                    auto result = loadModel(entry.path().string());
                    if (result.has_value()) {
                        models.push_back(std::move(result.value()));
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        errors_.push_back({"<scan>", std::string("Failed to scan directory: ") + e.what()});
    }

    // Sort models by name for consistent UI ordering
    std::sort(models.begin(), models.end(),
              [](const LoadedModel& a, const LoadedModel& b) { return a.name < b.name; });

    return models;
}

std::optional<LoadedModel> CustomModelLoader::loadModel(const std::string& filepath) {
    namespace fs = std::filesystem;

    LoadedModel model;
    model.fullPath = filepath;
    model.name = fs::path(filepath).stem().string();

    try {
        model.lastModified = fs::last_write_time(filepath);
    } catch (const std::exception&) {
        // Ignore, use default
    }

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    std::string mtlBasedir = fs::path(filepath).parent_path().string();
    if (!mtlBasedir.empty() && mtlBasedir.back() != '/' && mtlBasedir.back() != '\\') {
        mtlBasedir += '/';
    }

    bool success = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
                                    filepath.c_str(), mtlBasedir.c_str());

    if (!success) {
        errors_.push_back({model.name, err.empty() ? "Unknown error loading OBJ" : err});
        return std::nullopt;
    }

    if (!warn.empty()) {
        AWOW_LOG_WARNING("Models", "OBJ warning for " + model.name + ": " + warn);
    }

    if (shapes.empty()) {
        errors_.push_back({model.name, "No shapes found in OBJ file"});
        return std::nullopt;
    }

    bool hasNormals = !attrib.normals.empty();

    // Flatten tinyobj's per-attribute indices into Vertex3D, deduplicating
    std::unordered_map<std::string, uint32_t> uniqueVertices;

    // Material id of each output triangle
    std::vector<int> triangleMaterials;

    for (const auto& shape : shapes) {
        size_t indexOffset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            size_t fv = shape.mesh.num_face_vertices[f];

            int matId = (f < shape.mesh.material_ids.size()) ? shape.mesh.material_ids[f] : -1;

            if (fv < 3) {
                indexOffset += fv;
                continue;
            }

            // For quads and larger polygons, triangulate as a fan
            for (size_t v = 1; v < fv - 1; v++) {
                size_t triIndices[3] = {0, v, v + 1};

                for (size_t ti = 0; ti < 3; ti++) {
                    size_t idx = triIndices[ti];
                    tinyobj::index_t tinyIdx = shape.mesh.indices[indexOffset + idx];

                    Vertex3D vertex{};

                    if (tinyIdx.vertex_index >= 0) {
                        vertex.position[0] = attrib.vertices[3 * tinyIdx.vertex_index + 0];
                        vertex.position[1] = attrib.vertices[3 * tinyIdx.vertex_index + 1];
                        vertex.position[2] = attrib.vertices[3 * tinyIdx.vertex_index + 2];
                    }

                    // Normals are optional
                    if (hasNormals && tinyIdx.normal_index >= 0) {
                        vertex.normal[0] = attrib.normals[3 * tinyIdx.normal_index + 0];
                        vertex.normal[1] = attrib.normals[3 * tinyIdx.normal_index + 1];
                        vertex.normal[2] = attrib.normals[3 * tinyIdx.normal_index + 2];
                    } else {
                        // No normal in the file: point it up
                        vertex.normal[0] = 0.0f;
                        vertex.normal[1] = 1.0f;
                        vertex.normal[2] = 0.0f;
                    }

                    // V is flipped: the exported textures put V=0 at the top,
                    // the shaders expect it at the bottom
                    if (tinyIdx.texcoord_index >= 0 && !attrib.texcoords.empty()) {
                        vertex.uv[0] = attrib.texcoords[2 * tinyIdx.texcoord_index + 0];
                        vertex.uv[1] = 1.0f - attrib.texcoords[2 * tinyIdx.texcoord_index + 1];
                    } else {
                        vertex.uv[0] = 0.0f;
                        vertex.uv[1] = 0.0f;
                    }

                    // White; the push constant color tints it
                    vertex.color[0] = 1.0f;
                    vertex.color[1] = 1.0f;
                    vertex.color[2] = 1.0f;

                    std::string key = std::to_string(tinyIdx.vertex_index) + "/" +
                                      std::to_string(tinyIdx.normal_index) + "/" +
                                      std::to_string(tinyIdx.texcoord_index);

                    auto it = uniqueVertices.find(key);
                    if (it == uniqueVertices.end()) {
                        uint32_t newIndex = static_cast<uint32_t>(model.vertices.size());
                        uniqueVertices[key] = newIndex;
                        model.vertices.push_back(vertex);
                        model.indices.push_back(newIndex);
                    } else {
                        model.indices.push_back(it->second);
                    }
                }

                triangleMaterials.push_back(matId);
            }

            indexOffset += fv;
        }
    }

    // Regroup triangles by material so each material is one contiguous range
    std::map<int, std::vector<uint32_t>> materialIndices;
    for (size_t tri = 0; tri < triangleMaterials.size(); tri++) {
        int matId = triangleMaterials[tri];
        materialIndices[matId].push_back(model.indices[tri * 3 + 0]);
        materialIndices[matId].push_back(model.indices[tri * 3 + 1]);
        materialIndices[matId].push_back(model.indices[tri * 3 + 2]);
    }

    model.indices.clear();
    model.materialSegments.clear();

    for (auto& [matId, indices] : materialIndices) {
        MaterialSegment segment;
        segment.materialIndex = matId;
        segment.startIndex = static_cast<uint32_t>(model.indices.size());
        segment.indexCount = static_cast<uint32_t>(indices.size());

        if (matId >= 0 && static_cast<size_t>(matId) < materials.size()) {
            const auto& mat = materials[matId];
            if (!mat.diffuse_texname.empty()) {
                fs::path texPath = fs::path(mtlBasedir) / mat.diffuse_texname;
                if (fs::exists(texPath)) {
                    segment.texturePath = texPath.string();
                } else if (fs::exists(mat.diffuse_texname)) {
                    segment.texturePath = mat.diffuse_texname;
                }
            }
        }

        model.materialSegments.push_back(segment);
        model.indices.insert(model.indices.end(), indices.begin(), indices.end());
    }

    // Single-texture fields mirror the first textured material
    for (const auto& segment : model.materialSegments) {
        if (!segment.texturePath.empty()) {
            model.diffuseTexturePath = segment.texturePath;
            model.hasTexture = true;
            break;
        }
    }

    std::string validationError;
    if (!validateModel(model, validationError)) {
        errors_.push_back({model.name, validationError});
        return std::nullopt;
    }

    normalizeModel(model);

    return model;
}

void CustomModelLoader::normalizeModel(LoadedModel& model) {
    if (model.vertices.empty()) return;

    float minX = FLT_MAX, minY = FLT_MAX, minZ = FLT_MAX;
    float maxX = -FLT_MAX, maxY = -FLT_MAX, maxZ = -FLT_MAX;

    for (const auto& v : model.vertices) {
        minX = std::min(minX, v.position[0]);
        minY = std::min(minY, v.position[1]);
        minZ = std::min(minZ, v.position[2]);
        maxX = std::max(maxX, v.position[0]);
        maxY = std::max(maxY, v.position[1]);
        maxZ = std::max(maxZ, v.position[2]);
    }

    model.minX = minX; model.minY = minY; model.minZ = minZ;
    model.maxX = maxX; model.maxY = maxY; model.maxZ = maxZ;

    // Horizontal center only; the vertical axis is anchored on minY below.
    float centerX = (minX + maxX) * 0.5f;
    float centerZ = (minZ + maxZ) * 0.5f;

    // Size to match the built-in actor shapes (~2.0 world units across: sphere
    // radius 1.0, cube edge baseSize*2). A mesh only fills part of its bounding
    // box - a humanoid's limbs leave gaps, about 60% of the volume a sphere
    // fills solid - so the footprint overshoots 2.0 to read as comparable mass.
    // Without this the health-based scale multiplier has too little silhouette
    // to work with and stops being visually readable.
    constexpr float kTargetFootprint = 2.6f;
    constexpr float kMaxHeight = 3.0f;
    float sizeX = maxX - minX;
    float sizeY = maxY - minY;
    float sizeZ = maxZ - minZ;
    float footprint = std::max({sizeX, sizeZ, 0.001f});
    float height = std::max(sizeY, 0.001f);

    // Uniform scale sets the footprint; the height cap is then applied as a
    // squash on the vertical axis alone, so a tall mesh loses height rather
    // than having its footprint starved down with it.
    float scale = kTargetFootprint / footprint;
    float scaleY = (height * scale > kMaxHeight) ? (kMaxHeight / height) : scale;

    // Centered horizontally, but anchored on its base vertically: the renderer
    // places actors at ground level and scales about the model origin, so a
    // vertically-centered mesh sinks half its height into the floor - and sinks
    // deeper as the health-based scale grows. Anchoring the base at the origin
    // keeps it standing on the ground at any scale, with no per-model offset to
    // maintain (ModelRotation::zOffset stays free for deliberate hover).
    // The default -90 deg X rotation maps model +Y to world +Z, so "up" here is Y.
    for (auto& v : model.vertices) {
        v.position[0] = (v.position[0] - centerX) * scale;
        v.position[1] = (v.position[1] - minY) * scaleY;
        v.position[2] = (v.position[2] - centerZ) * scale;
    }
}

bool CustomModelLoader::validateModel(const LoadedModel& model, std::string& errorOut) {
    if (model.vertices.empty()) {
        errorOut = "No vertices found";
        return false;
    }

    if (model.indices.empty()) {
        errorOut = "No faces found";
        return false;
    }

    if (model.indices.size() % 3 != 0) {
        errorOut = "Invalid face count (not triangulated)";
        return false;
    }

    uint32_t maxIndex = static_cast<uint32_t>(model.vertices.size());
    for (uint32_t idx : model.indices) {
        if (idx >= maxIndex) {
            errorOut = "Invalid vertex index in face data";
            return false;
        }
    }

    return true;
}

bool CustomModelLoader::hasFileChanged(const std::string& filepath,
                                        std::filesystem::file_time_type lastKnownTime) const {
    namespace fs = std::filesystem;

    try {
        if (!fs::exists(filepath)) {
            return true;  // File was deleted
        }
        return fs::last_write_time(filepath) > lastKnownTime;
    } catch (const std::exception&) {
        return true;  // Assume changed on error
    }
}
