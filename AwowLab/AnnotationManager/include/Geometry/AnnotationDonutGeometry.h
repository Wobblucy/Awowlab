#pragma once

#include "AnnotationGeometryData.h"

namespace annotation_geometry {

// An annulus with unit outer radius. The vertex shader moves the inner ring to
// the real inner radius; inner vertices are marked with uv.y = 1.
AnnotationGeometryData createDonutGeometry(uint32_t segments = 32, float defaultInnerRadiusRatio = 0.5f);

} // namespace annotation_geometry
