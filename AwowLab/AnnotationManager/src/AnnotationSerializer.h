#pragma once

#include "AnnotationTypes.h"
#include <string>
#include <vector>

namespace AnnotationSerializer {

// The full per-encounter save format
std::string toJson(const std::vector<Annotation>& annotations, const AnnotationKey& key);

// Returns false on a parse error or unsupported version
bool fromJson(const std::string& json, std::vector<Annotation>& annotations, uint64_t& maxIdFound);

// The cut-down format for sharing; keeps only manual, world-anchored annotations
std::string toShareableJson(const std::vector<Annotation>& annotations, const std::string& encounterName);

// The caller assigns IDs to the returned annotations
std::vector<Annotation> fromShareableJson(const std::string& json);

} // namespace AnnotationSerializer
