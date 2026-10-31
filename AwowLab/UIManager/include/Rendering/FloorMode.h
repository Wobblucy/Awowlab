#pragma once

// Which way the floor shader draws: sampling the bound texture, or the grid.
// An encounter texture bound by setMapBoundsWithTexture counts as a texture
// even though the world map manager has nothing loaded.
inline bool floorDrawsTexture(bool usingFallbackGrid, bool usingExternalTexture,
                              bool worldMapHasImage) {
    if (usingFallbackGrid) {
        return false;
    }
    return usingExternalTexture || worldMapHasImage;
}
