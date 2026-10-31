#pragma once

#include <vector>
#include <cstdint>

struct AnnotationVertex;

struct AnnotationGeometryData {
    std::vector<AnnotationVertex> vertices;
    std::vector<uint32_t> indices;

    bool isValid() const {
        return !vertices.empty() && !indices.empty();
    }

    void clear() {
        vertices.clear();
        indices.clear();
    }
};
