#pragma once

#include <string>

// Forward declarations
struct UISettings;
class ActorAppearanceSettings;
struct NameplateSettings;
struct CameraSettings;
struct FacingIndicatorSettings;
class UISettingsExportPanel;
class Camera3D;

// Unified Settings Hub panel
// Consolidates Display, Appearance, Camera settings into a single organized panel
class UISettingsHub {
public:
    UISettingsHub();
    ~UISettingsHub();

    // Set dependencies (non-owning pointers)
    void setUISettings(UISettings* settings) { uiSettings_ = settings; }
    void setAppearanceSettings(ActorAppearanceSettings* settings) { appearanceSettings_ = settings; }
    void setNameplateSettings(NameplateSettings* settings) { nameplateSettings_ = settings; }
    void setCameraSettings(CameraSettings* settings) { cameraSettings_ = settings; }
    void setFacingIndicatorSettings(FacingIndicatorSettings* settings) { facingSettings_ = settings; }
    void setSettingsExportPanel(UISettingsExportPanel* panel) { settingsExportPanel_ = panel; }
    void setCamera(Camera3D* camera) { camera_ = camera; }

    // Render the Settings Hub window
    void render();

    // Visibility control
    bool isVisible() const { return visible_; }
    void setVisible(bool visible) { visible_ = visible; }
    void toggleVisible() { visible_ = !visible_; }

private:
    // Visibility state
    bool visible_ = false;

    // Dependencies (non-owning)
    UISettings* uiSettings_ = nullptr;
    ActorAppearanceSettings* appearanceSettings_ = nullptr;
    NameplateSettings* nameplateSettings_ = nullptr;
    CameraSettings* cameraSettings_ = nullptr;
    FacingIndicatorSettings* facingSettings_ = nullptr;
    UISettingsExportPanel* settingsExportPanel_ = nullptr;
    Camera3D* camera_ = nullptr;

    // Section navigation
    enum class Section { Display, Appearance, Camera, ImportExport };
    Section currentSection_ = Section::Display;

    // Appearance sub-tabs
    enum class AppearanceTab { Actors, Nameplates, Facing };
    AppearanceTab currentAppearanceTab_ = AppearanceTab::Actors;

    // Section renderers
    void renderSidebar();
    void renderContent();

    void renderDisplaySection();
    void renderAppearanceSection();
    void renderCameraSection();
    void renderImportExportSection();

    // Appearance sub-section renderers
    void renderActorsTab();
    void renderNameplatesTab();
    void renderFacingTab();

    // Helper to check if section is selected
    bool isSectionSelected(Section section) const { return currentSection_ == section; }

    // Mark settings dirty after changes
    void markSettingsDirty();
};
