#pragma once

#include "AnnotationGeometryData.h"

namespace annotation_geometry {

// A short cylinder of radius 1 centred on the origin; the model matrix scales it
AnnotationGeometryData createCircleGeometry(uint32_t segments = 32);

} // namespace annotation_geometry
