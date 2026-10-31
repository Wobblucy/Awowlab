#include "Rendering/Minimap2D.h"
#include "Color/ActorColorGenerator.h"
#include "UI/ActorVisibilitySettings.h"
#include "UI/NameplateSettings.h"
#include "../../Database/EventDatabase.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

Minimap2D::Minimap2D()
    : minX_(0), maxX_(0), minY_(0), maxY_(0)
    , boundsCached_(false)
    , zoomLevel_(1.0f)
    , panX_(0.0f), panY_(0.0f)
    , worldMapMinX_(0.0f), worldMapMaxX_(0.0f)
    , worldMapMinY_(0.0f), worldMapMaxY_(0.0f)
    , hasWorldMapBounds_(false)
    , worldMapTexture_(nullptr)
{
}

bool Minimap2D::setBounds(int32_t minX, int32_t maxX, int32_t minY, int32_t maxY) {
    if (boundsCached_ && minX_ == minX && maxX_ == maxX && minY_ == minY && maxY_ == maxY) {
        return false;
    }
    minX_ = minX;
    maxX_ = maxX;
    minY_ = minY;
    maxY_ = maxY;
    boundsCached_ = true;
    return true;
}

void Minimap2D::setZoom(float zoom) {
    zoomLevel_ = std::max(0.1f, std::min(50.0f, zoom));
}

void Minimap2D::setPan(float x, float y) {
    panX_ = x;
    panY_ = y;
}

void Minimap2D::applyPanDelta(float dx, float dy) {
    panX_ += dx;
    panY_ -= dy;  // Invert Y to match expected drag direction
}

void Minimap2D::applyZoomDelta(float delta) {
    zoomLevel_ += delta * 0.1f;
    zoomLevel_ = std::max(0.1f, std::min(50.0f, zoomLevel_));
}

void Minimap2D::setWorldMapBounds(float minX, float maxX, float minY, float maxY) {
    worldMapMinX_ = minX;
    worldMapMaxX_ = maxX;
    worldMapMinY_ = minY;
    worldMapMaxY_ = maxY;
    hasWorldMapBounds_ = true;
}

void Minimap2D::setWorldMapTexture(void* textureId) {
    worldMapTexture_ = textureId;
}

void Minimap2D::clearWorldMap() {
    hasWorldMapBounds_ = false;
    worldMapTexture_ = nullptr;
}

ImVec2 Minimap2D::worldToScreen(int32_t worldX, int32_t worldY,
                                 ImVec2 canvasPos, ImVec2 canvasSize) const {
    if (!hasWorldMapBounds_ && !boundsCached_) {
        return ImVec2(canvasPos.x + canvasSize.x * 0.5f, canvasPos.y + canvasSize.y * 0.5f);
    }

    // Use world map bounds (same as 3D floor)
    float minX = hasWorldMapBounds_ ? worldMapMinX_ : static_cast<float>(minX_);
    float maxX = hasWorldMapBounds_ ? worldMapMaxX_ : static_cast<float>(maxX_);
    float minY = hasWorldMapBounds_ ? worldMapMinY_ : static_cast<float>(minY_);
    float maxY = hasWorldMapBounds_ ? worldMapMaxY_ : static_cast<float>(maxY_);

    float width = maxX - minX;
    float height = maxY - minY;
    if (width < 1.0f) width = 1.0f;
    if (height < 1.0f) height = 1.0f;

    // Normalize to [0, 1] within world bounds
    float normX = (static_cast<float>(worldX) - minX) / width;
    float normY = (static_cast<float>(worldY) - minY) / height;

    normX = (normX - 0.5f) * zoomLevel_ + 0.5f + panX_;
    normY = (normY - 0.5f) * zoomLevel_ + 0.5f + panY_;

    // Map to screen - invert Y to match 3D view orientation
    // In 3D: camera looks down, +Y world = up on screen
    // In ImGui: +Y screen = down
    float screenX = canvasPos.x + normX * canvasSize.x;
    float screenY = canvasPos.y + (1.0f - normY) * canvasSize.y;

    return ImVec2(screenX, screenY);
}

void Minimap2D::drawGrid(ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize) {
    const float gridSpacing = 50.0f;
    ImU32 gridColor = IM_COL32(50, 50, 50, 255);

    for (float x = 0; x < canvasSize.x; x += gridSpacing) {
        drawList->AddLine(
            ImVec2(canvasPos.x + x, canvasPos.y),
            ImVec2(canvasPos.x + x, canvasPos.y + canvasSize.y),
            gridColor);
    }

    for (float y = 0; y < canvasSize.y; y += gridSpacing) {
        drawList->AddLine(
            ImVec2(canvasPos.x, canvasPos.y + y),
            ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + y),
            gridColor);
    }
}

void Minimap2D::drawWorldMapTexture(ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize) {
    if (!worldMapTexture_ || !hasWorldMapBounds_) {
        return;
    }

    // Calculate visible world region based on zoom/pan
    // Inverse of worldToScreen transform: worldNorm = (screenNorm - 0.5 - pan) / zoom + 0.5
    // Screen corners in normalized coords: TL(0,0), TR(1,0), BR(1,1), BL(0,1)
    // But screenY uses (1-normY), so we need to account for that

    // Screen top-left (screenNormX=0, screenNormY=0 → normY_transformed=1)
    float tlWorldX = (-0.5f - panX_) / zoomLevel_ + 0.5f;
    float tlWorldY = (0.5f - panY_) / zoomLevel_ + 0.5f;

    // Screen top-right (screenNormX=1, screenNormY=0 → normY_transformed=1)
    float trWorldX = (0.5f - panX_) / zoomLevel_ + 0.5f;
    float trWorldY = (0.5f - panY_) / zoomLevel_ + 0.5f;

    // Screen bottom-right (screenNormX=1, screenNormY=1 → normY_transformed=0)
    float brWorldX = (0.5f - panX_) / zoomLevel_ + 0.5f;
    float brWorldY = (-0.5f - panY_) / zoomLevel_ + 0.5f;

    // Screen bottom-left (screenNormX=0, screenNormY=1 → normY_transformed=0)
    float blWorldX = (-0.5f - panX_) / zoomLevel_ + 0.5f;
    float blWorldY = (-0.5f - panY_) / zoomLevel_ + 0.5f;

    // Convert world normalized coords to UV using 3D floor mapping
    // UV formula: U = 1 - worldY, V = 1 - worldX
    ImVec2 uvTL(1.0f - tlWorldY, 1.0f - tlWorldX);
    ImVec2 uvTR(1.0f - trWorldY, 1.0f - trWorldX);
    ImVec2 uvBR(1.0f - brWorldY, 1.0f - brWorldX);
    ImVec2 uvBL(1.0f - blWorldY, 1.0f - blWorldX);

    ImTextureID texId = reinterpret_cast<ImTextureID>(worldMapTexture_);

    ImVec2 topLeft = canvasPos;
    ImVec2 topRight = ImVec2(canvasPos.x + canvasSize.x, canvasPos.y);
    ImVec2 bottomRight = ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y);
    ImVec2 bottomLeft = ImVec2(canvasPos.x, canvasPos.y + canvasSize.y);

    drawList->AddImageQuad(
        texId,
        topLeft, topRight, bottomRight, bottomLeft,
        uvTL, uvTR, uvBR, uvBL,
        IM_COL32(255, 255, 255, 200)
    );
}

void Minimap2D::autoZoomToPlayerBounds() {
    if (!hasWorldMapBounds_ || !boundsCached_) {
        return;
    }

    // Player bounds (already include 15% padding from FrameDataProvider)
    float playerMinX = static_cast<float>(minX_);
    float playerMaxX = static_cast<float>(maxX_);
    float playerMinY = static_cast<float>(minY_);
    float playerMaxY = static_cast<float>(maxY_);

    float worldWidth = worldMapMaxX_ - worldMapMinX_;
    float worldHeight = worldMapMaxY_ - worldMapMinY_;
    if (worldWidth < 1.0f || worldHeight < 1.0f) return;

    // Player bounds in normalized world coords [0, 1]
    float playerMinNormX = (playerMinX - worldMapMinX_) / worldWidth;
    float playerMaxNormX = (playerMaxX - worldMapMinX_) / worldWidth;
    float playerMinNormY = (playerMinY - worldMapMinY_) / worldHeight;
    float playerMaxNormY = (playerMaxY - worldMapMinY_) / worldHeight;

    float playerNormWidth = playerMaxNormX - playerMinNormX;
    float playerNormHeight = playerMaxNormY - playerMinNormY;

    if (playerNormWidth < 0.001f || playerNormHeight < 0.001f) return;

    // Calculate zoom to fit player bounds (use smaller zoom to fit both dimensions)
    float zoomX = 1.0f / playerNormWidth;
    float zoomY = 1.0f / playerNormHeight;
    zoomLevel_ = std::min(zoomX, zoomY);

    zoomLevel_ = std::max(0.5f, std::min(10.0f, zoomLevel_));

    // Calculate pan to center on player bounds
    float playerCenterNormX = (playerMinNormX + playerMaxNormX) / 2.0f;
    float playerCenterNormY = (playerMinNormY + playerMaxNormY) / 2.0f;

    // Pan formula: pan = (0.5 - playerCenter) * zoom
    panX_ = (0.5f - playerCenterNormX) * zoomLevel_;
    panY_ = (0.5f - playerCenterNormY) * zoomLevel_;
}

void Minimap2D::render(ImDrawList* drawList, ImVec2 canvasPos, ImVec2 canvasSize,
                        const std::vector<MinimapActorPosition>& actors,
                        const std::shared_ptr<ActorColorGenerator>& colorGen,
                        const ActorVisibilitySettings& visibilitySettings,
                        const NameplateSettings& /*nameplateSettings*/,
                        std::string_view selectedPlayerGuid) {
    if (worldMapTexture_ && hasWorldMapBounds_) {
        drawWorldMapTexture(drawList, canvasPos, canvasSize);
    } else {
        drawGrid(drawList, canvasPos, canvasSize);
    }

    for (const auto& actor : actors) {
        // Apply filters using the same logic as 3D view
        if (!visibilitySettings.shouldRender(actor.type, actor.reaction)) {
            continue;
        }

        ImVec2 pos = worldToScreen(actor.x, actor.y, canvasPos, canvasSize);

        ActorColor color = colorGen->getCachedColor(actor.guid);

        bool isSelected = !selectedPlayerGuid.empty() && actor.guid == selectedPlayerGuid && actor.type == ActorType::Player;

        // Fade non-selected players to 80% opacity
        int alpha = isSelected ? 255 : static_cast<int>(255 * 0.8f);

        ImU32 imColor = IM_COL32(
            static_cast<int>(color.r * 255),
            static_cast<int>(color.g * 255),
            static_cast<int>(color.b * 255),
            alpha);

        if (isSelected) {
            float glowRadius = 9.0f;
            ImU32 glowColor = IM_COL32(255, 25, 0, 180);  // Bright red/orange
            drawList->AddCircleFilled(pos, glowRadius, glowColor);
            drawList->AddCircle(pos, glowRadius, IM_COL32(255, 80, 20, 220), 0, 2.5f);
        }

        drawList->AddCircleFilled(pos, 6.0f, imColor);
        drawList->AddCircle(pos, 6.0f, IM_COL32(255, 255, 255, 255), 0, 2.0f);

        ImVec2 mousePos = ImGui::GetMousePos();
        float dx = mousePos.x - pos.x;
        float dy = mousePos.y - pos.y;
        float distSq = dx * dx + dy * dy;
        constexpr float hoverRadius = 10.0f;  // Slightly larger than circle (6.0f) for easier targeting

        if (distSq <= hoverRadius * hoverRadius) {
            std::string nameplateStr(actor.name);
            ImVec2 textSize = ImGui::CalcTextSize(nameplateStr.c_str());
            ImVec2 textPos(pos.x - textSize.x * 0.5f, pos.y - 20.0f);  // Center text above actor
            drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), nameplateStr.c_str());
        }
    }
}
