#pragma once
#include "SphereGeometry.h"  // For Vertex3D
#include <vector>
#include <string>
#include <optional>
#include <filesystem>

// Material segment info for multi-material models
struct MaterialSegment {
    int materialIndex = -1;       // Index into materials vector (-1 = no material)
    uint32_t startIndex = 0;      // First index in index buffer
    uint32_t indexCount = 0;      // Number of indices for this segment
    std::string texturePath;      // Resolved texture path (empty if none)
};

// Result of loading a single OBJ file
struct LoadedModel {
    std::string name;           // Filename without extension (e.g., "dragon")
    std::string fullPath;       // Full path for reload detection
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;

    // Bounding box before normalization
    float minX = 0.0f, minY = 0.0f, minZ = 0.0f;
    float maxX = 0.0f, maxY = 0.0f, maxZ = 0.0f;

    std::filesystem::file_time_type lastModified;

    // First material texture from the MTL file (empty if none)
    std::string diffuseTexturePath;  // Absolute path resolved from MTL's map_Kd
    bool hasTexture = false;

    std::vector<MaterialSegment> materialSegments;

    bool isValid() const { return !vertices.empty() && !indices.empty(); }
};

// Error information for failed loads
struct ModelLoadError {
    std::string filename;
    std::string errorMessage;
};

class CustomModelLoader {
public:
    CustomModelLoader() = default;
    ~CustomModelLoader() = default;

    // Scan directory and load all valid OBJ files
    // Returns list of successfully loaded models
    // Populates errors_ with any failed loads
    std::vector<LoadedModel> scanAndLoadModels(const std::string& directory);

    // Load a single OBJ file
    // Rescales to actor size, centers horizontally and stands it on the origin;
    // missing normals default to +Y
    std::optional<LoadedModel> loadModel(const std::string& filepath);

    const std::vector<ModelLoadError>& getErrors() const { return errors_; }

    // Check if a file has been modified since last load
    bool hasFileChanged(const std::string& filepath,
                        std::filesystem::file_time_type lastKnownTime) const;

private:
    std::vector<ModelLoadError> errors_;

    // Rescale to actor size and recenter
    void normalizeModel(LoadedModel& model);

    // Validate model has reasonable data
    bool validateModel(const LoadedModel& model, std::string& errorOut);
};
