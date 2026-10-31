#pragma once

#include "AnnotationGeometryData.h"

namespace annotation_geometry {

// A pie slice on the XZ plane with its apex at the origin. Other angles are
// reached by scaling X in the model matrix.
AnnotationGeometryData createConeGeometry(uint32_t segments = 16, float defaultAngleDegrees = 60.0f);

} // namespace annotation_geometry
