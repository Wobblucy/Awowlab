#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <string_view>
#include <memory>
#include <unordered_map>
#include "structures.h"  // For UnitReaction

struct ImVec2;
struct ImDrawList;
struct ActorVisibilitySettings;
struct NameplateSettings;
class ActorColorGenerator;
enum class ActorType;

struct MinimapActorPosition {
    std::string_view guid;
    std::string_view name;
    int32_t x;
    int32_t y;
    ActorType type;
    UnitReaction reaction;
};

class Minimap2D {
public:
    Minimap2D();

    // Set world bounds for coordinate mapping
    // Returns true if the bounds differ from the ones already set
    bool setBounds(int32_t minX, int32_t maxX, int32_t minY, int32_t maxY);

    bool hasBounds() const { return boundsCached_; }

    void render(ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize,
                const std::vector<MinimapActorPosition>& actors,
                const std::shared_ptr<ActorColorGenerator>& colorGen,
                const ActorVisibilitySettings& visibilitySettings,
                const NameplateSettings& nameplateSettings,
                std::string_view selectedPlayerGuid = "");

    void setZoom(float zoom);
    float getZoom() const { return zoomLevel_; }

    void setPan(float x, float y);
    void getPan(float& x, float& y) const { x = panX_; y = panY_; }

    void applyPanDelta(float dx, float dy);
    void applyZoomDelta(float delta);

    // Auto-zoom to fit player bounds (with 15% padding already applied)
    void autoZoomToPlayerBounds();

    // World map texture support
    void setWorldMapBounds(float minX, float maxX, float minY, float maxY);
    void setWorldMapTexture(void* textureId);  // ImTextureID is void*
    void clearWorldMap();

private:
    int32_t minX_, maxX_, minY_, maxY_;
    bool boundsCached_;

    float zoomLevel_;
    float panX_, panY_;

    float worldMapMinX_, worldMapMaxX_, worldMapMinY_, worldMapMaxY_;
    bool hasWorldMapBounds_;
    void* worldMapTexture_;  // ImTextureID

    ImVec2 worldToScreen(int32_t worldX, int32_t worldY,
                         ImVec2 canvasPos, ImVec2 canvasSize) const;

    void drawGrid(ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize);
    void drawWorldMapTexture(ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize);
};
