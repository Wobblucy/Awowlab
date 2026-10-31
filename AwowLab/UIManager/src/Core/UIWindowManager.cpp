#include "Core/UIWindowManager.h"
#include "Core/FrameDataProvider.h"
#include "Core/FrameProfiler.h"
#include "Core/LocalizationManager.h"
#include "Core/ErrorLogger.h"
#include "UI/Panels/Navigation/UIPlaybackPanel.h"
#include "UI/Panels/Navigation/UIHelpPanel.h"
#include "UI/UICreaturesPanel.h"
#include "UI/UIActorSelector.h"
#include "UI/Panels/Cooldowns/UISpellHistory.h"
#include "UI/Panels/Frames/UIRaidFramePanel.h"
#include "UI/Panels/Frames/UIEnemyGridPanel.h"
#include "UI/UIActorAppearancePanel.h"
#include "UI/UINameplateSettingsPanel.h"
#include "UI/Panels/Combat/UIActorBreakdownPanel.h"
#include "UI/Panels/Combat/UIDeathRecapPanel.h"
#include "UI/Panels/Combat/UIMeterPanel.h"
#include "UI/Panels/Debug/UIDeathsPanel.h"
#include "UI/Panels/Debug/UIAuraDebugPanel.h"
#include "UI/UISpellAssignmentPanel.h"
#include "UI/SpellAssignmentSettings.h"
#include "UI/Panels/Cooldowns/UICooldownPanel.h"
#include "UI/Panels/Cooldowns/UIRaidCooldownPanel.h"
#include "UI/Panels/Frames/UIPlayerBuffPanel.h"
#include "UI/Panels/Frames/UIPlayerDebuffPanel.h"
#include "UI/ActorAppearanceSettings.h"
#include "AnnotationManager/include/UIAutoAnnotationPanel.h"
#include "AnnotationManager/include/UIAnnotationListPanel.h"
#include "AnnotationManager/include/AutoAnnotationTypes.h"
#include "Parser/WowClass.h"
#include "../../Database/AuraDatabase.h"
#include "../../Database/DeathDatabase.h"
#include "UI/SpellIconLoader.h"
#include "Playback/PlaybackController.h"
#include "Color/ActorColorGenerator.h"
#include "Camera/Camera3D.h"
#include "../../Database/SpellCastDatabase.h"
#include "../../Database/EmoteDatabase.h"
#include "UI/SpellGCDDatabase.h"
#include "UI/BossTimerSettings.h"
#include "UI/Panels/Navigation/BossTimerStrip.h"
#include "UI/Panels/Navigation/UIUpcomingBossCasts.h"
#include "UI/Panels/Settings/UIEnemyCastFilterPanel.h"
#include "UI/Panels/Settings/UIMobWeightPanel.h"
#include "UI/Panels/Settings/UIPhaseEditorPanel.h"
#include "Core/Benchmark/BenchmarkStore.h"
#include "Core/Benchmark/BenchmarkSession.h"
#include "Core/Benchmark/SpellClassification.h"
#include "UI/Panels/Settings/UISettingsHub.h"
#include "UI/Panels/UISettingsExportPanel.h"
#include "UI/Panels/RaidNotes/UIRaidNotesPanel.h"
#include "UI/SpellContextMenu.h"
#include "RaidNoteContextMenu.h"
#include "Core/UnifiedSettings.h"
#include "Core/MobWeightSettings.h"
#include "Core/PhaseSettings.h"
#include "Core/PhaseResolver.h"
#include "Core/CurrentEncounter.h"
#include "../../Database/CombatDatabase.h"
#include "Core/Settings/SettingsExportManager.h"
#include "Core/Settings/ExportableSettingsAdapters.h"
#include <imgui.h>
#include <cstdio>

namespace {

// The breakdown a meter row drills into, for each meter view
CombatMetricType breakdownMetricFor(MeterViewType viewType) {
    switch (viewType) {
        case MeterViewType::HealingDone:
        case MeterViewType::Overhealing:
            return CombatMetricType::HealingDone;
        case MeterViewType::HealingTaken:
            // "who healed X" - synthesizes a per-spell breakdown
            // from every source that healed this actor.
            return CombatMetricType::HealingReceived;
        case MeterViewType::DamageTaken:
        case MeterViewType::DamageTakenByAbility:
            return CombatMetricType::DamageTaken;
        case MeterViewType::DamageTakenBy:
            // Rows here are players ranked by the damage they did
            // to the selected enemy, so their drill-down is damage
            // dealt - their abilities against that target - not the
            // damage they took.
            return CombatMetricType::DamageDealt;
        case MeterViewType::FriendlyFire:
            // Restrict the actor's damage breakdown to hits on
            // player targets so the drill-down only lists
            // FF-relevant spells.
            return CombatMetricType::FriendlyFire;
        default:
            return CombatMetricType::DamageDealt;
    }
}

constexpr int32_t kGcdHistoryWindowMs = 30000;

// The actor's recent casts as the GCD history panel shows them
std::vector<SpellCastDisplay> buildGcdHistory(const SpellCastDatabase& spellCastDb,
                                              const std::string& actorGuid, uint16_t specId,
                                              const SpellGCDDatabase* gcdDatabase, uint32_t currentTime) {
    auto& spellSettings = spell_assignment::SpellAssignmentSettings::instance();
    std::vector<SpellCastDisplay> recentCasts;

    for (const auto& castEvent : spellCastDb.getCastsForActor(actorGuid, currentTime, kGcdHistoryWindowMs)) {
        // Skip melee attacks (spell IDs 0 and 1 from WCL imports)
        if (castEvent.spell_id == 0 || castEvent.spell_id == 1) {
            continue;
        }
        if (specId != 0 && spellSettings.isSpellExcludedFromGCDTracker(specId, castEvent.spell_id)) {
            continue;
        }
        // gcd_ms is precomputed by EncounterDataLoader; it is 0 for local
        // files parsed without spell data, which get the default
        int32_t actualGcd = castEvent.gcd_ms > 0 ? castEvent.gcd_ms : SpellGCDDatabase::DEFAULT_GCD_MS;

        // Cast time is not precomputed in SpellCastEvent, so derive it here
        int32_t actualCastTime = 0;
        if (gcdDatabase) {
            SpellTimingData timing = gcdDatabase->getSpellTiming(castEvent.spell_id);
            if (timing.cast_time_ms > 0 && castEvent.haste_permille > 0) {
                float hasteMultiplier = 1.0f + (castEvent.haste_permille / 1000.0f);
                actualCastTime = static_cast<int32_t>(timing.cast_time_ms / hasteMultiplier);
            } else {
                actualCastTime = timing.cast_time_ms;
            }
        }

        recentCasts.push_back({
            castEvent.spell_id,
            castEvent.start_timestamp_ms,
            castEvent.end_timestamp_ms,
            actualGcd,
            actualCastTime,
            castEvent.haste_permille,
            castEvent.cancelled
        });
    }

    return recentCasts;
}

} // namespace

UIWindowManager::UIWindowManager()
    : playbackPanel_(std::make_unique<UIPlaybackPanel>())
    , creaturesPanel_(std::make_unique<UICreaturesPanel>())
    , actorSelector_(std::make_unique<UIActorSelector>())
    , spellHistory_(std::make_unique<UISpellHistory>())
    , raidFramePanel_(std::make_unique<UIRaidFramePanel>())
    , enemyGridPanel_(std::make_unique<UIEnemyGridPanel>())
    , appearancePanel_(std::make_unique<UIActorAppearancePanel>())
    , nameplateSettingsPanel_(std::make_unique<UINameplateSettingsPanel>())
    , breakdownPanel_(std::make_unique<UIActorBreakdownPanel>())
    , meterPanel1_(std::make_unique<UIMeterPanel>(1))
    , meterPanel2_(std::make_unique<UIMeterPanel>(2))
    , meterPanel3_(std::make_unique<UIMeterPanel>(3))
    , deathRecapPanel_(std::make_unique<UIDeathRecapPanel>())
    , deathsPanel_(std::make_unique<UIDeathsPanel>())
    , auraDebugPanel_(std::make_unique<UIAuraDebugPanel>())
    , spellAssignmentPanel_(std::make_unique<UISpellAssignmentPanel>())
    , cooldownPanel_(std::make_unique<UICooldownPanel>())
    , raidCooldownPanel1_(std::make_unique<UIRaidCooldownPanel>())
    , raidCooldownPanel2_(std::make_unique<UIRaidCooldownPanel>())
    , raidCooldownPanel3_(std::make_unique<UIRaidCooldownPanel>())
    , playerBuffPanel_(std::make_unique<UIPlayerBuffPanel>())
    , playerDebuffPanel_(std::make_unique<UIPlayerDebuffPanel>())
    , helpPanel_(std::make_unique<UIHelpPanel>())
    , bossTimerStrip_(std::make_unique<BossTimerStrip>())
    , enemyCastFilterPanel_(std::make_unique<UIEnemyCastFilterPanel>())
    , upcomingBossCastsPanel_(std::make_unique<UIUpcomingBossCasts>())
    , bossTimerSettings_(std::make_unique<BossTimerSettings>())
    , settingsExportPanel_(std::make_unique<UISettingsExportPanel>())
    , raidNotesPanel_(std::make_unique<UIRaidNotesPanel>())
    , settingsHub_(std::make_unique<UISettingsHub>())
    , mobWeightPanel_(std::make_unique<UIMobWeightPanel>())
    , phaseEditorPanel_(std::make_unique<UIPhaseEditorPanel>())
    , deathDatabase_(nullptr)
    , auraDatabase_(nullptr)
    , appearanceSettings_(std::make_shared<ActorAppearanceSettings>())
{
    raidCooldownPanel1_->setGrouping(1);
    raidCooldownPanel2_->setGrouping(2);
    raidCooldownPanel3_->setGrouping(3);

    bossTimerStrip_->setSettings(bossTimerSettings_.get());
    enemyCastFilterPanel_->setSettings(bossTimerSettings_.get());
    enemyCastFilterPanel_->setBossTimerStrip(bossTimerStrip_.get());
    enemyCastFilterPanel_->setUpcomingBossCastsPanel(upcomingBossCastsPanel_.get());

    settingsHub_->setUISettings(&uiSettings_);
    settingsHub_->setAppearanceSettings(appearanceSettings_.get());
    settingsHub_->setNameplateSettings(&nameplateSettings_);
    settingsHub_->setCameraSettings(&cameraSettings_);
    settingsHub_->setFacingIndicatorSettings(&facingIndicatorSettings_);
    settingsHub_->setSettingsExportPanel(settingsExportPanel_.get());

    registerSettingsExporters();
}

UIWindowManager::~UIWindowManager() = default;

void UIWindowManager::loadSettings() {
    // Missing files leave the defaults in place
    uiSettings_.loadFromFile();
    appearanceSettings_->loadFromFile();

    const auto& cache = SettingsCache::instance().get();
    if (!cache.nameplateSettingsJson.empty()) {
        nameplateSettings_.fromJson(cache.nameplateSettingsJson);
    }
    if (!cache.raidFrameConfigJson.empty()) {
        raidFramePanel_->getConfig().fromJson(cache.raidFrameConfigJson);
    }
    if (!cache.bossTimerSettingsJson.empty()) {
        *bossTimerSettings_ = BossTimerSettings::deserializeFromJson(cache.bossTimerSettingsJson);
    }
    if (!cache.facingIndicatorSettingsJson.empty()) {
        facingIndicatorSettings_.fromJson(cache.facingIndicatorSettingsJson);
    }
    if (!cache.mobWeightSettingsJson.empty()) {
        MobWeightSettings::instance().fromJson(cache.mobWeightSettingsJson);
    }
    if (!cache.phaseSettingsJson.empty()) {
        PhaseSettings::instance().fromJson(cache.phaseSettingsJson);
    }
    // Spell classifications live in a plain file so they can be shared or
    // deleted without touching the encrypted settings. Anything saved by an
    // older build is migrated across on first run, so no rulings are lost.
    {
        std::string classifications =
            benchmark::store::readFile(benchmark::store::classificationsPath());

        if (classifications.empty()) {
            if (auto it = cache.extraValues.find("benchmarkClassifications");
                it != cache.extraValues.end() && !it->second.empty()) {
                classifications = it->second;
                benchmark::store::writeFile(benchmark::store::classificationsPath(),
                                            classifications);
            }
        }

        if (!classifications.empty()) {
            benchmark::ClassificationStore::instance().fromJson(classifications);
        }
    }
    // Saved reference logs, keyed by encounter. Live mode looks one up when a
    // pull ends, so they have to be in memory before any pull happens.
    benchmark::BenchmarkSession::instance().loadSavedMentors();

    // Font scale is applied in ImGuiManager::initialize(), once the ImGui context exists
}

void UIWindowManager::saveSettings() {
    uiSettings_.saveToFile();

    auto& cache = SettingsCache::instance();
    cache.get().nameplateSettingsJson = nameplateSettings_.toJson();
    cache.get().raidFrameConfigJson = raidFramePanel_->getConfig().toJson();
    cache.get().bossTimerSettingsJson = bossTimerSettings_->serializeToJson();
    cache.get().facingIndicatorSettingsJson = facingIndicatorSettings_.toJson();
    cache.markDirty();
}

void UIWindowManager::registerSettingsExporters() {
    auto& manager = SettingsExportManager::instance();

    actorAppearanceExporter_ = std::make_unique<ActorAppearanceExporter>(appearanceSettings_.get());
    manager.registerProvider(actorAppearanceExporter_.get());

    spellAssignmentExporter_ = std::make_unique<SpellAssignmentExporter>();
    manager.registerProvider(spellAssignmentExporter_.get());

    nameplateSettingsExporter_ = std::make_unique<NameplateSettingsExporter>(&nameplateSettings_);
    manager.registerProvider(nameplateSettingsExporter_.get());

    facingIndicatorExporter_ = std::make_unique<FacingIndicatorExporter>(&facingIndicatorSettings_);
    manager.registerProvider(facingIndicatorExporter_.get());

    bossTimerExporter_ = std::make_unique<BossTimerExporter>(bossTimerSettings_.get());
    manager.registerProvider(bossTimerExporter_.get());

    uiLayoutExporter_ = std::make_unique<UILayoutExporter>(&uiSettings_);
    manager.registerProvider(uiLayoutExporter_.get());
}

void UIWindowManager::setPlaybackController(std::shared_ptr<PlaybackController> playback) {
    playbackController_ = std::move(playback);
    if (bossTimerStrip_ && playbackController_) {
        bossTimerStrip_->setPlaybackController(playbackController_.get());
    }
}

void UIWindowManager::setColorGenerator(std::shared_ptr<ActorColorGenerator> colorGen) {
    colorGenerator_ = std::move(colorGen);
}

void UIWindowManager::setCamera(std::shared_ptr<Camera3D> camera) {
    camera3D_ = std::move(camera);
}

void UIWindowManager::setCustomShapeRegistry(CustomShapeRegistry* registry, const std::string& customModelsPath) {
    if (appearancePanel_) {
        appearancePanel_->setCustomShapeRegistry(registry);
        appearancePanel_->setCustomModelsPath(customModelsPath);
    }

    // Baked model pack ships default creature -> model assignments; feed them
    // to the appearance settings as the lowest-priority name layer (user
    // NameGroup overrides still win, and these are never saved to settings).
    if (appearanceSettings_ && registry) {
        appearanceSettings_->setDefaultModelAssignments(registry->getBakedAssignments());

        // Saved overrides can reference models that no longer exist (renamed or
        // removed between sessions); without a validity check those dead
        // references shadow the baked defaults and silently fall back to
        // spheres. The registry outlives the settings' use of this callback
        // (both live for the visualizer's lifetime).
        appearanceSettings_->setCustomShapeValidator(
            [registry](const std::string& name) { return registry->hasShape(name); });
    }
}

void UIWindowManager::loadDeathDatabase(const DeathDatabase* db) {
    deathDatabase_ = db;
}

void UIWindowManager::loadAuraDatabase(const AuraDatabase* db) {
    auraDatabase_ = db;
}

void UIWindowManager::loadActorNpcIdMap(const std::unordered_map<int32_t, uint32_t>& actorIdToNpcId) {
    if (appearanceSettings_) {
        appearanceSettings_->setActorNpcIdMap(actorIdToNpcId);
    }
}

void UIWindowManager::loadHealthScalingData(uint64_t avgPlayerHealth,
                                             const std::unordered_map<std::string, uint64_t>& actorMaxHealth) {
    if (appearanceSettings_) {
        appearanceSettings_->setAveragePlayerHealth(avgPlayerHealth);
        for (const auto& [guid, maxHealth] : actorMaxHealth) {
            appearanceSettings_->cacheActorMaxHealth(guid, maxHealth);
        }
    }
}

std::optional<std::string> UIWindowManager::getSelectedActor() const {
    if (actorSelector_) {
        return actorSelector_->getSelectedActor();
    }
    return std::nullopt;
}

void UIWindowManager::updateKnownPlayers(const std::shared_ptr<ActorColorGenerator>& colorGen,
                                          const std::unordered_map<std::string, std::string>& players) {
    if (actorSelector_) {
        actorSelector_->updateKnownPlayers(colorGen, players);
    }
}

void UIWindowManager::renderPanels(FrameDataProvider* dataProvider,
                                    SpellIconLoader* spellIconLoader,
                                    const SpellGCDDatabase* gcdDatabase,
                                    uint32_t currentTime) {
    FRAME_PROFILE_SCOPE("RenderPanels");

    // Phase rules apply per encounter, so rebuild the meter phase list
    // whenever a new encounter (or a fresh pull of the same one, which
    // swaps the spell cast database) arrives
    uint32_t encounterId = CurrentEncounter::instance().getId();
    const SpellCastDatabase* spellCastDb = dataProvider ? dataProvider->getSpellCastDatabase() : nullptr;
    if (encounterId != phaseEncounterId_ || spellCastDb != phaseSpellCastDb_) {
        phaseEncounterId_ = encounterId;
        phaseSpellCastDb_ = spellCastDb;
        SpellContextMenu::setCurrentEncounterId(encounterId);
        rebuildPhases(dataProvider);
    }

    renderNavigationPanels(dataProvider, spellIconLoader);
    renderFramePanels(dataProvider, spellIconLoader, currentTime);
    renderCombatPanels(dataProvider, spellIconLoader, currentTime);
    renderCooldownPanels(dataProvider, spellIconLoader, gcdDatabase, currentTime);
    renderDebugPanels(dataProvider, spellIconLoader, currentTime);
    renderModalPanels(dataProvider, spellIconLoader, currentTime);

    if (helpPanel_) {
        helpPanel_->render();
    }
}

void UIWindowManager::rebuildPhases(FrameDataProvider* dataProvider) {
    auto pushPhases = [this](std::vector<MeterPhase> phases) {
        if (meterPanel1_) meterPanel1_->setPhases(phases);
        if (meterPanel2_) meterPanel2_->setPhases(phases);
        if (meterPanel3_) meterPanel3_->setPhases(std::move(phases));
    };

    uint32_t encounterId = CurrentEncounter::instance().getId();
    const auto* rules = PhaseSettings::instance().rulesFor(encounterId);
    const auto* spellCastDb = dataProvider ? dataProvider->getSpellCastDatabase() : nullptr;
    const auto* combatDb = dataProvider ? dataProvider->getCombatDatabase() : nullptr;
    const auto* emoteDb = dataProvider ? dataProvider->getEmoteDatabase() : nullptr;
    if (!rules || !spellCastDb || !combatDb) {
        pushPhases({});
        return;
    }

    // Resolve the rules through the shared resolver so the main app and
    // the live overlay produce identical phase windows.
    phase::RuleInputs inputs;
    inputs.pullStart_ms = combatDb->getMinTimestamp();
    inputs.pullEnd_ms = combatDb->getMaxTimestamp();
    inputs.firstCastTime = [spellCastDb](uint32_t spellId) {
        return spellCastDb->firstCastTime(spellId);
    };
    if (emoteDb) {
        inputs.firstEmoteTime = [emoteDb](std::string_view text) {
            return emoteDb->firstEmoteTime(text);
        };
    }

    auto resolved = phase::resolvePhases(*rules, inputs);
    std::vector<MeterPhase> phases;
    phases.reserve(resolved.size());
    for (auto& p : resolved) {
        phases.push_back(MeterPhase{std::move(p.label), p.start_ms, p.end_ms});
    }
    pushPhases(std::move(phases));
}

void UIWindowManager::renderWarningPopup() {
    auto& logger = ErrorLogger::instance();

    if (logger.hasNewWarnings() && !warningPopupOpen_) {
        warningPopupOpen_ = true;
        ImGui::OpenPopup("###ParseWarnings");
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(550, 350), ImGuiCond_Appearing);

    // The id after ### stays fixed so the popup keeps working in every language
    const std::string warningsTitle = std::string(L("warnings.title")) + "###ParseWarnings";
    if (ImGui::BeginPopupModal(warningsTitle.c_str(), &warningPopupOpen_, ImGuiWindowFlags_None)) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "%s", L("warnings.heading"));
        ImGui::Spacing();

        ImGui::TextWrapped("%s", L("warnings.intro"));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text(L("warnings.details_fmt"), logger.warningCount());
        ImGui::BeginChild("WarningList", ImVec2(0, 150), true);
        for (const auto& warning : logger.getWarnings()) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "[%s]", warning.timestamp.c_str());
            ImGui::SameLine();
            ImGui::TextWrapped("%s", warning.message.c_str());
            ImGui::Separator();
        }
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f),
            L("warnings.log_saved_fmt"), logger.getLogFilePath().string().c_str());

        ImGui::Spacing();
        ImGui::TextWrapped("%s", L("warnings.report_hint"));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button(L("btn.ok"), ImVec2(120, 0))) {
            warningPopupOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(L("warnings.clear"), ImVec2(120, 0))) {
            logger.clearWarnings();
            warningPopupOpen_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

MenuAction UIWindowManager::renderMenuBar() {
    MenuAction action = MenuAction::None;

    if (!uiSettings_.showMenuBar) {
        return MenuAction::None;
    }

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu(L("menu.file"))) {
            if (ImGui::MenuItem(L("menu.change_encounter"))) {
                action = MenuAction::ChangeEncounter;
            }
            if (ImGui::MenuItem(L("menu.return_home"))) {
                action = MenuAction::ReturnToLandingPage;
            }
            ImGui::Separator();
            if (ImGui::MenuItem(L("menu.import_export_settings"))) {
                if (settingsExportPanel_) {
                    settingsExportPanel_->show();
                }
            }
            ImGui::EndMenu();
        }

        // Comparing pulls against a reference is its own flow, reached from
        // the landing page. It is deliberately not offered here: a replay is
        // for watching what happened, and a half-configured comparison
        // launched mid-playback was competing with that rather than serving it.

        if (ImGui::BeginMenu(L("menu.view"))) {
            ImGui::MenuItem(L("menu.playback_controls"), nullptr, &uiSettings_.playbackControls.visible);
            ImGui::MenuItem(L("menu.raid_frames"), nullptr, &uiSettings_.raidFramePanel.visible);
            ImGui::MenuItem(L("menu.enemy_grid"), nullptr, &uiSettings_.enemyGridPanel.visible);
            ImGui::MenuItem(L("menu.minimap_2d"), nullptr, &uiSettings_.minimapWindow.visible);
            ImGui::MenuItem(L("menu.gcd_history"), nullptr, &uiSettings_.spellHistory.visible);
            ImGui::MenuItem(L("menu.upcoming_boss_casts"), nullptr, &uiSettings_.upcomingBossCastsPanel.visible);
            if (enemyCastFilterPanel_) {
                bool filterVisible = enemyCastFilterPanel_->isVisible();
                if (ImGui::MenuItem(L("menu.enemy_cast_filters"), nullptr, &filterVisible)) {
                    enemyCastFilterPanel_->setVisible(filterVisible);
                }
            }
            if (mobWeightPanel_) {
                bool weightVisible = mobWeightPanel_->isVisible();
                if (ImGui::MenuItem(L("mobweight.title"), nullptr, &weightVisible)) {
                    mobWeightPanel_->setVisible(weightVisible);
                }
            }
            if (cooldownPanel_) {
                bool visible = cooldownPanel_->isVisible();
                if (ImGui::MenuItem(L("menu.cooldowns"), nullptr, &visible)) {
                    cooldownPanel_->setVisible(visible);
                }
            }
            if (raidCooldownPanel1_) {
                ImGui::MenuItem(L("menu.raid_cd1"), nullptr, raidCooldownPanel1_->getVisiblePtr());
            }
            if (meterPanel1_) {
                ImGui::MenuItem(L("menu.meter_1"), nullptr, meterPanel1_->getVisiblePtr());
            }
            if (meterPanel2_) {
                ImGui::MenuItem(L("menu.meter_2"), nullptr, meterPanel2_->getVisiblePtr());
            }
            if (meterPanel3_) {
                ImGui::MenuItem(L("menu.meter_3"), nullptr, meterPanel3_->getVisiblePtr());
            }
            if (deathsPanel_) {
                ImGui::MenuItem(L("menu.deaths"), nullptr, deathsPanel_->getVisiblePtr());
            }
            if (playerBuffPanel_) {
                ImGui::MenuItem(L("menu.player_buffs"), nullptr, playerBuffPanel_->getVisiblePtr());
            }
            if (playerDebuffPanel_) {
                ImGui::MenuItem(L("menu.player_debuffs"), nullptr, playerDebuffPanel_->getVisiblePtr());
            }

            ImGui::Separator();

            if (spellAssignmentPanel_) {
                ImGui::MenuItem(L("menu.spell_assignments"), nullptr, spellAssignmentPanel_->getVisiblePtr());
            }

            ImGui::Separator();

            ImGui::Text("%s", L("menu.ui_scale"));
            auto& cache = SettingsCache::instance();
            float fontScale = cache.get().fontScale;

            if (ImGui::Button(L("btn.scale_small"))) {
                fontScale = 0.85f;
            }
            ImGui::SameLine();
            if (ImGui::Button(L("btn.scale_normal"))) {
                fontScale = 1.0f;
            }
            ImGui::SameLine();
            if (ImGui::Button(L("btn.scale_large"))) {
                fontScale = 1.25f;
            }
            ImGui::SameLine();
            if (ImGui::Button(L("btn.scale_xl"))) {
                fontScale = 1.5f;
            }

            ImGui::SetNextItemWidth(150.0f);
            ImGui::SliderFloat("##FontScale", &fontScale, 0.5f, 2.0f, "%.2fx");

            if (fontScale != cache.get().fontScale) {
                cache.get().fontScale = fontScale;
                ImGui::GetIO().FontGlobalScale = fontScale;
                cache.markDirty();
            }

            ImGui::Separator();
            if (ImGui::MenuItem(L("menu.reset_layout"))) {
                uiSettings_ = UISettings();
            }

            if (ImGui::MenuItem(L("menu.save_layout"))) {
                saveSettings();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(L("menu.tools"))) {
            if (autoAnnotationPanel_) {
                bool visible = autoAnnotationPanel_->isVisible();
                if (ImGui::MenuItem(L("menu.auto_annotations"), nullptr, &visible)) {
                    autoAnnotationPanel_->setVisible(visible);
                }
            }

            if (annotationListPanel_) {
                bool visible = annotationListPanel_->isVisible();
                if (ImGui::MenuItem(L("menu.annotation_list"), nullptr, &visible)) {
                    annotationListPanel_->setVisible(visible);
                }
            }

            if (raidNotesPanel_) {
                ImGui::MenuItem(L("menu.raid_notes"), nullptr, raidNotesPanel_->getVisiblePtr());
            }

            ImGui::Separator();

            // Actor customization (opens full modal with Individual/Enemies tabs)
            if (ImGui::MenuItem(L("menu.customize_appearances"))) {
                if (appearancePanel_) {
                    appearancePanel_->open();
                }
            }

            ImGui::Separator();

            if (ImGui::MenuItem(L("menu.export_snapshot"))) {
                if (playbackController_) {
                    int32_t currentTime = playbackController_->getCurrentTime();
                    int32_t maxTime = playbackController_->getMaxTime();
                    openExportSnapshotModal(currentTime, maxTime);
                }
            }

            ImGui::EndMenu();
        }

        if (ImGui::MenuItem(L("menu.settings"))) {
            if (settingsHub_) {
                settingsHub_->setVisible(true);
            }
        }
        ImGui::EndMainMenuBar();
    }

    return action;
}

void UIWindowManager::applyWindowState(const char* windowName, WindowState& state) {
    if (!state.visible) {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    if (state.firstFrame) {
        ImVec2 absolutePos = state.getAbsolutePosition(viewport);
        ImGui::SetNextWindowPos(absolutePos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(state.size, ImGuiCond_FirstUseEver);
        state.firstFrame = false;
    }

    if (ImGui::Begin(windowName)) {
        ImVec2 currentPos = ImGui::GetWindowPos();
        ImVec2 currentSize = ImGui::GetWindowSize();

        state.size = currentSize;

        state.updateFromAbsolutePosition(currentPos, viewport);
    }
}

void UIWindowManager::clearSpellAssignmentCache() {
    if (spellAssignmentPanel_) {
        spellAssignmentPanel_->clearCache();
    }
}

void UIWindowManager::openAppearancePanelForActor(const std::string& actorGuid) {
    if (actorSelector_ && !actorGuid.empty()) {
        actorSelector_->setSelectedActor(actorGuid);
    }
    if (appearancePanel_) {
        appearancePanel_->openForActor(actorGuid);
    }
}

void UIWindowManager::openSpellAssignmentPanelForSpec(uint16_t specId) {
    if (spellAssignmentPanel_ && specId > 0) {
        spellAssignmentPanel_->setSelectedSpec(specId);
    }
}


void UIWindowManager::renderNavigationPanels(FrameDataProvider* dataProvider, SpellIconLoader* spellIconLoader) {
    if (uiSettings_.playbackControls.visible && playbackController_) {
        FRAME_PROFILE_SCOPE("Playback");
        applyWindowState("Playback", uiSettings_.playbackControls);
        playbackPanel_->render(playbackController_, deathDatabase_, cameraSettings_, bossTimerSettings_.get());

        // The boss timer strip draws inside the playback window and rebuilds
        // its cache in render() when dirty
        if (bossTimerStrip_) {
            bossTimerStrip_->setSpellIconLoader(spellIconLoader);
            bossTimerStrip_->render();
        }

        ImGui::End();
    }

    if (uiSettings_.creaturesPanel.visible && dataProvider && dataProvider->hasEvents()) {
        FRAME_PROFILE_SCOPE("Creatures");
        applyWindowState("Creatures/Vehicles/Pets", uiSettings_.creaturesPanel);
        creaturesPanel_->render(colorGenerator_, dataProvider->getActorPositions(),
                                dataProvider->getActorResources(),
                                dataProvider->getGuidToName(), visibilitySettings_.renderPets());
        ImGui::End();
    }

    if (uiSettings_.upcomingBossCastsPanel.visible && upcomingBossCastsPanel_ && playbackController_) {
        FRAME_PROFILE_SCOPE("UpcomingBossCasts");
        applyWindowState("Upcoming Boss Casts", uiSettings_.upcomingBossCastsPanel);
        upcomingBossCastsPanel_->render(playbackController_->getCurrentTime());
        ImGui::End();
    }
}

void UIWindowManager::renderFramePanels(FrameDataProvider* dataProvider,
                                         SpellIconLoader* spellIconLoader,
                                         uint32_t currentTime) {
    if (uiSettings_.raidFramePanel.visible && dataProvider && dataProvider->hasEvents()) {
        FRAME_PROFILE_SCOPE("RaidFrames");
        applyWindowState("Raid Frames", uiSettings_.raidFramePanel);
        raidFramePanel_->render(colorGenerator_, dataProvider->getActorPositions(),
                                dataProvider->getActorResources(),
                                dataProvider->getGuidToName(),
                                dataProvider->getSpellCastDatabase(),
                                dataProvider->getAuraDatabase(),
                                spellIconLoader,
                                currentTime);

        if (auto clicked = raidFramePanel_->getClickedPlayer()) {
            actorSelector_->setSelectedActor(*clicked);
            raidFramePanel_->clearClickedPlayer();
        }

        if (raidFramePanel_->isClearSelectionRequested()) {
            actorSelector_->clearSelection();
            raidFramePanel_->clearClearSelectionRequest();
        }
        ImGui::End();
    }

    if (uiSettings_.enemyGridPanel.visible && dataProvider && dataProvider->hasEvents()) {
        FRAME_PROFILE_SCOPE("EnemyGrid");
        applyWindowState("Enemy Grid", uiSettings_.enemyGridPanel);
        enemyGridPanel_->render(colorGenerator_, dataProvider->getActorPositions(),
                                dataProvider->getActorResources(),
                                dataProvider->getGuidToName());

        if (enemyGridPanel_->isClearSelectionRequested()) {
            actorSelector_->clearSelection();
            enemyGridPanel_->clearClearSelectionRequest();
        }

        // Handle enemy click for "Taken By" meter view (propagate to all meters)
        if (auto clickedEnemy = enemyGridPanel_->getClickedEnemy()) {
            if (meterPanel1_) meterPanel1_->setSelectedTarget(*clickedEnemy);
            if (meterPanel2_) meterPanel2_->setSelectedTarget(*clickedEnemy);
            if (meterPanel3_) meterPanel3_->setSelectedTarget(*clickedEnemy);
            enemyGridPanel_->clearClickedEnemy();
        }
        ImGui::End();
    }

    // Render Player Buff/Debuff panels - only when a player is selected
    if (dataProvider && dataProvider->hasEvents() && actorSelector_) {
        auto selectedActorOpt = actorSelector_->getSelectedActor();
        if (selectedActorOpt.has_value()) {
            std::string targetGuid = selectedActorOpt.value();

            if (playerBuffPanel_ && playerBuffPanel_->isVisible()) {
                FRAME_PROFILE_SCOPE("PlayerBuffs");
                playerBuffPanel_->render(
                    dataProvider->getAuraDatabase(),
                    spellIconLoader,
                    colorGenerator_.get(),
                    targetGuid,
                    currentTime
                );
            }

            if (playerDebuffPanel_ && playerDebuffPanel_->isVisible()) {
                FRAME_PROFILE_SCOPE("PlayerDebuffs");
                playerDebuffPanel_->render(
                    dataProvider->getAuraDatabase(),
                    spellIconLoader,
                    colorGenerator_.get(),
                    targetGuid,
                    currentTime
                );
            }
        }
    }
}

void UIWindowManager::renderCombatPanels(FrameDataProvider* dataProvider,
                                          SpellIconLoader* spellIconLoader,
                                          uint32_t currentTime) {
    const auto* combatDb = dataProvider ? dataProvider->getCombatDatabase() : nullptr;
    const auto* dispelDb = dataProvider ? dataProvider->getDispelDatabase() : nullptr;

    auto renderMeterPanel = [&](UIMeterPanel* panel) {
        if (!panel || !panel->isVisible() || !dataProvider) return;

        panel->render(combatDb, dispelDb, colorGenerator_,
                      dataProvider->getGuidToName(), currentTime, spellIconLoader,
                      dataProvider->getGuidToNameMap(),
                      deathDatabase_,
                      dataProvider->getResourceDatabase(),
                      dataProvider->getAuraDatabase(),
                      dataProvider->getAvoidanceDatabase());

        if (auto request = panel->getBreakdownRequest()) {
            actorSelector_->setSelectedActor(request->actorGuid);
            if (breakdownPanel_) {
                breakdownPanel_->open(request->actorGuid, breakdownMetricFor(panel->getConfig().view_type));
            }
            panel->clearBreakdownRequest();
            panel->clearClickedActor();
        }

        if (auto clickedGuid = panel->getClickedActor()) {
            if (panel->getConfig().view_type == MeterViewType::Deaths && deathRecapPanel_) {
                // Collect every death for this actor so the new
                // recap panel can list them all on its left pane.
                if (deathDatabase_) {
                    auto deathPtrs = deathDatabase_->getPlayerDeaths();
                    std::vector<DeathEvent> actorDeaths;
                    std::string actorName;
                    for (const auto* death : deathPtrs) {
                        if (death->actor_guid == *clickedGuid) {
                            if (actorName.empty()) actorName = death->actor_name;
                            actorDeaths.push_back(*death);
                        }
                    }
                    deathRecapPanel_->open(
                        *clickedGuid, actorName, std::move(actorDeaths));
                }
            }
            panel->clearClickedActor();
        }
    };

    if (meterPanel1_) {
        FRAME_PROFILE_SCOPE("Meter1");
        renderMeterPanel(meterPanel1_.get());
    }

    if (meterPanel2_) {
        FRAME_PROFILE_SCOPE("Meter2");
        renderMeterPanel(meterPanel2_.get());
    }

    if (meterPanel3_) {
        FRAME_PROFILE_SCOPE("Meter3");
        renderMeterPanel(meterPanel3_.get());
    }
}

void UIWindowManager::renderCooldownPanels(FrameDataProvider* dataProvider,
                                            SpellIconLoader* spellIconLoader,
                                            const SpellGCDDatabase* gcdDatabase,
                                            uint32_t currentTime) {
    const auto* spellCastDb = dataProvider ? dataProvider->getSpellCastDatabase() : nullptr;

    // Render spell history (GCD history) panel - only when a player is selected
    if (uiSettings_.spellHistory.visible && spellCastDb && actorSelector_ && spellIconLoader) {
        auto selectedActorOpt = actorSelector_->getSelectedActor();
        if (selectedActorOpt.has_value()) {
            FRAME_PROFILE_SCOPE("SpellHistory");

            uint16_t specId = colorGenerator_ ? colorGenerator_->getSpecId(selectedActorOpt.value()) : 0;
            std::vector<SpellCastDisplay> recentCasts = buildGcdHistory(
                *spellCastDb, selectedActorOpt.value(), specId, gcdDatabase, currentTime);

            applyWindowState("GCD History", uiSettings_.spellHistory);
            spellHistory_->render(selectedActorOpt, recentCasts, currentTime,
                                 spellIconLoader, false);
            ImGui::End();
        }
    }

    // Render cooldown panel - only when a player is selected
    if (cooldownPanel_ && spellCastDb && spellIconLoader && actorSelector_) {
        auto selectedActorOpt = actorSelector_->getSelectedActor();
        if (selectedActorOpt.has_value()) {
            FRAME_PROFILE_SCOPE("Cooldowns");
            cooldownPanel_->setSpellCastDatabase(spellCastDb);
            cooldownPanel_->setAuraDatabase(auraDatabase_);
            cooldownPanel_->setSpellIconLoader(spellIconLoader);
            cooldownPanel_->setSelectedPlayerGuid(*selectedActorOpt);

            if (colorGenerator_) {
                uint16_t specId = colorGenerator_->getSpecId(*selectedActorOpt);
                cooldownPanel_->setSelectedPlayerSpecId(specId);
            }

            cooldownPanel_->render(currentTime);
        }
    }

    if (spellCastDb && spellIconLoader && colorGenerator_ && dataProvider) {
        auto setupRaidPanel = [&](UIRaidCooldownPanel* panel) {
            panel->setSpellCastDatabase(spellCastDb);
            panel->setAuraDatabase(auraDatabase_);
            panel->setSpellIconLoader(spellIconLoader);
            panel->setColorGenerator(colorGenerator_.get());
            panel->setGuidToNameMap(&dataProvider->getGuidToName());
        };

        if (raidCooldownPanel1_ && raidCooldownPanel1_->isVisible()) {
            FRAME_PROFILE_SCOPE("RaidCD1");
            setupRaidPanel(raidCooldownPanel1_.get());
            raidCooldownPanel1_->render(currentTime);
        }

        if (raidCooldownPanel2_ && raidCooldownPanel2_->isVisible()) {
            FRAME_PROFILE_SCOPE("RaidCD2");
            setupRaidPanel(raidCooldownPanel2_.get());
            raidCooldownPanel2_->render(currentTime);
        }

        if (raidCooldownPanel3_ && raidCooldownPanel3_->isVisible()) {
            FRAME_PROFILE_SCOPE("RaidCD3");
            setupRaidPanel(raidCooldownPanel3_.get());
            raidCooldownPanel3_->render(currentTime);
        }
    }
}

void UIWindowManager::renderDebugPanels(FrameDataProvider* dataProvider,
                                         SpellIconLoader* spellIconLoader,
                                         uint32_t currentTime) {
    const auto* combatDb = dataProvider ? dataProvider->getCombatDatabase() : nullptr;

    if (deathsPanel_ && deathsPanel_->isVisible() && deathDatabase_) {
        FRAME_PROFILE_SCOPE("Deaths");
        deathsPanel_->render(
            deathDatabase_,
            playbackController_,
            actorSelector_.get(),
            combatDb,
            dataProvider ? dataProvider->getResourceDatabase() : nullptr,
            dataProvider ? dataProvider->getSpellNameMap() : nullptr,
            spellIconLoader
        );
    }

    if (auraDebugPanel_ && auraDebugPanel_->isVisible() && auraDatabase_ && dataProvider) {
        FRAME_PROFILE_SCOPE("AuraDebug");
        auraDebugPanel_->render(auraDatabase_, dataProvider->getGuidToName(),
                                 currentTime, playbackController_);
    }

    if (autoAnnotationPanel_ && autoAnnotationPanel_->isVisible()) {
        FRAME_PROFILE_SCOPE("AutoAnnotation");
        autoAnnotationPanel_->render(currentTime);
    }
}

void UIWindowManager::renderModalPanels(FrameDataProvider* dataProvider,
                                         SpellIconLoader* spellIconLoader,
                                         uint32_t currentTime) {
    const auto* combatDb = dataProvider ? dataProvider->getCombatDatabase() : nullptr;

    if (appearancePanel_ && dataProvider) {
        FRAME_PROFILE_SCOPE("Appearance");
        std::string selectedGuid = actorSelector_->getSelectedActor().value_or("");
        appearancePanel_->render(*appearanceSettings_, colorGenerator_,
                                  dataProvider->getGuidToName(), selectedGuid);
    }

    if (nameplateSettingsPanel_) {
        FRAME_PROFILE_SCOPE("NameplateSettings");
        nameplateSettingsPanel_->render(nameplateSettings_);
    }

    if (breakdownPanel_ && dataProvider && combatDb) {
        FRAME_PROFILE_SCOPE("Breakdown");
        bool useFullEncounter = (meterPanel1_ && meterPanel1_->getConfig().time_mode == MeterTimeMode::FullEncounter);
        breakdownPanel_->render(combatDb, colorGenerator_, dataProvider->getGuidToName(),
                                 spellIconLoader, currentTime, useFullEncounter);
    }

    if (deathRecapPanel_ && dataProvider && combatDb) {
        FRAME_PROFILE_SCOPE("DeathRecap");
        deathRecapPanel_->render(
            combatDb,
            dataProvider->getResourceDatabase(),
            colorGenerator_,
            dataProvider->getGuidToName(),
            spellIconLoader,
            dataProvider->getGuidToNameMap(),
            dataProvider->getSpellNameMap(),
            dataProvider->getAuraDatabase(),
            &SettingsCache::instance().get().trackedDefensiveSpellIds);
    }

    if (spellAssignmentPanel_ && dataProvider) {
        FRAME_PROFILE_SCOPE("SpellAssignment");
        RaidFrameConfig* raidConfig = raidFramePanel_ ? &raidFramePanel_->getConfig() : nullptr;
        spellAssignmentPanel_->render(
            dataProvider->getSpellCastDatabase(),
            auraDatabase_,
            colorGenerator_.get(),
            dataProvider->getSpellNameMap(),
            spellIconLoader,
            raidConfig,
            &nameplateSettings_
        );
    }

    if (enemyCastFilterPanel_ && enemyCastFilterPanel_->isVisible()) {
        FRAME_PROFILE_SCOPE("EnemyCastFilter");
        enemyCastFilterPanel_->setSpellIconLoader(spellIconLoader);
        enemyCastFilterPanel_->render();
    }

    if (mobWeightPanel_ && mobWeightPanel_->isVisible()) {
        FRAME_PROFILE_SCOPE("MobWeight");
        mobWeightPanel_->render(combatDb, dataProvider ? dataProvider->getGuidToNameMap() : nullptr);
        // Weight edits invalidate the database's per-target weight cache;
        // rebuild it and nudge each open meter to re-aggregate this frame
        // (the meters cache their ranked stats and a weight edit isn't one
        // of their own refresh triggers, so without this the discounted
        // numbers would only appear on the next periodic tick).
        if (mobWeightPanel_->consumeWeightsChanged() && combatDb) {
            combatDb->refreshTargetWeights();
            if (meterPanel1_) meterPanel1_->invalidateStatsCache();
            if (meterPanel2_) meterPanel2_->invalidateStatsCache();
            if (meterPanel3_) meterPanel3_->invalidateStatsCache();
        }
    }

    // The "+" button next to any meter's phase combo opens the editor
    bool phaseEditorRequested = false;
    if (meterPanel1_ && meterPanel1_->consumePhaseEditorRequest()) phaseEditorRequested = true;
    if (meterPanel2_ && meterPanel2_->consumePhaseEditorRequest()) phaseEditorRequested = true;
    if (meterPanel3_ && meterPanel3_->consumePhaseEditorRequest()) phaseEditorRequested = true;
    if (phaseEditorRequested && phaseEditorPanel_) {
        phaseEditorPanel_->toggleVisible();
    }

    if (phaseEditorPanel_ && phaseEditorPanel_->isVisible()) {
        FRAME_PROFILE_SCOPE("PhaseEditor");

        // Feed the editor plain data assembled from the frame's
        // databases so the same panel drives the standalone overlay
        // (which has no FrameDataProvider).
        const auto* spellCastDb = dataProvider ? dataProvider->getSpellCastDatabase() : nullptr;
        const auto* emoteDb = dataProvider ? dataProvider->getEmoteDatabase() : nullptr;

        PhaseEditorData editorData;
        editorData.encounterId = CurrentEncounter::instance().getId();
        editorData.spellNames = dataProvider ? dataProvider->getSpellNameMap() : nullptr;
        editorData.ruleInputs.pullStart_ms = combatDb ? combatDb->getMinTimestamp() : 0;
        editorData.ruleInputs.pullEnd_ms = combatDb ? combatDb->getMaxTimestamp() : 0;
        if (spellCastDb) {
            editorData.ruleInputs.firstCastTime = [spellCastDb](uint32_t spellId) {
                return spellCastDb->firstCastTime(spellId);
            };
        }
        if (emoteDb) {
            editorData.ruleInputs.firstEmoteTime = [emoteDb](std::string_view text) {
                return emoteDb->firstEmoteTime(text);
            };
            for (const auto& e : emoteDb->getEmotes()) {
                editorData.emotes.push_back(
                    {e.timestamp_ms, e.source_name, e.text});
            }
        }
        // Cast rules in the main app come from the spell context menu,
        // so the editor's cast section stays empty here.

        phaseEditorPanel_->render(editorData);
        // The panel already persisted the edit; rebuild the meter
        // phase windows so the change shows up right away
        if (phaseEditorPanel_->consumeRulesChanged()) {
            rebuildPhases(dataProvider);
        }
    }

    if (settingsExportPanel_) {
        settingsExportPanel_->render();
    }

    if (settingsHub_) {
        settingsHub_->render();
    }

    if (raidNotesPanel_) {
        FRAME_PROFILE_SCOPE("RaidNotes");

        const auto* spellCastDb = dataProvider ? dataProvider->getSpellCastDatabase() : nullptr;
        if (spellCastDb) {
            raidNotesPanel_->setFindAllInstancesCallback([spellCastDb](uint32_t spell_id) -> std::vector<int32_t> {
                std::vector<int32_t> timestamps;
                for (const auto& [guid, casts] : spellCastDb->getAllActorCasts()) {
                    for (const auto& cast : casts) {
                        if (cast.spell_id == spell_id) {
                            timestamps.push_back(cast.end_timestamp_ms);
                        }
                    }
                }
                return timestamps;
            });
        }

        raidNotesPanel_->render(spellIconLoader);
    }

    SpellContextMenu::renderPopup();

    auto navRequest = SpellContextMenu::consumePendingNavigation();
    if (navRequest.target == SpellContextMenu::NavigationRequest::Target::SpellSettings) {
        if (navRequest.isHostile) {
            // Boss/enemy spell -> Enemy Cast Filter panel
            if (enemyCastFilterPanel_) {
                enemyCastFilterPanel_->navigateToSpell(navRequest.spellId);
            }
        } else {
            // Player spell -> Spell Assignment panel
            if (spellAssignmentPanel_) {
                spellAssignmentPanel_->setFilterSpellId(navRequest.spellId);

                if (!navRequest.actorGuid.empty() && colorGenerator_) {
                    uint16_t specId = colorGenerator_->getSpecId(navRequest.actorGuid);
                    if (specId != 0) {
                        spellAssignmentPanel_->setSelectedSpec(specId);
                    }
                }
            }
        }
    } else if (navRequest.target == SpellContextMenu::NavigationRequest::Target::AutoAnnotations) {
        if (autoAnnotationPanel_) {
            autoAnnotationPanel_->navigateToSpell(navRequest.spellId);
        }
    }

    if (SpellContextMenu::hasPendingRaidNoteRequest()) {
        RaidNotes::SpellContext context;
        context.spell_id = SpellContextMenu::getPendingSpellId();
        context.spell_name = SpellContextMenu::getPendingSpellName();
        context.timestamp_ms = SpellContextMenu::getPendingTimestampMs();
        context.is_hostile_source = SpellContextMenu::getPendingIsHostile();
        context.grouped_timestamps = SpellContextMenu::getPendingGroupedTimestamps();

        SpellContextMenu::consumeRaidNoteRequest();
        RaidNotes::RaidNoteContextMenu::openWithContext(context);
    }

    if (SpellContextMenu::hasPendingPhaseRequest()) {
        auto phaseRequest = SpellContextMenu::consumePhaseRequest();
        uint32_t encounterId = CurrentEncounter::instance().getId();
        if (encounterId != 0 && phaseRequest.spellId != 0) {
            if (phaseRequest.add) {
                PhaseSettings::instance().addRule(encounterId, phaseRequest.spellId);
            } else {
                PhaseSettings::instance().removeRule(encounterId, phaseRequest.spellId);
            }

            auto& cache = SettingsCache::instance();
            cache.get().phaseSettingsJson = PhaseSettings::instance().toJson();
            cache.markDirty();

            rebuildPhases(dataProvider);
        }
    }

    // Rendered even when the panel is hidden, so right-click on spell icons
    // works regardless of panel visibility
    if (raidNotesPanel_) {
        RaidNotes::RaidNoteContextMenu::renderModal(
            raidNotesPanel_->getDocument(),
            raidNotesPanel_->getFindAllInstancesCallback()
        );
    }

    renderExportSnapshotModal();

    renderWarningPopup();
}

BossTimerSettings& UIWindowManager::getBossTimerSettings() {
    return *bossTimerSettings_;
}

void UIWindowManager::setHostileCreatureSpells(const std::vector<HostileCreatureSpells>& creatureSpells) {
    if (enemyCastFilterPanel_) {
        enemyCastFilterPanel_->setHostileCreatureSpells(creatureSpells);
    }

    if (bossTimerStrip_) {
        bossTimerStrip_->setHostileCreatureSpells(creatureSpells);
    }
}

void UIWindowManager::openExportSnapshotModal(int32_t currentTimeMs, int32_t maxTimeMs) {
    showExportModal_ = true;
    exportMaxTimeMs_ = maxTimeMs;

    // Default to 15 seconds centered on current time, clamped to valid range
    int32_t halfWindow = 7500;  // 7.5 seconds each direction
    exportStartMs_ = std::max(0, currentTimeMs - halfWindow);
    exportEndMs_ = std::min(maxTimeMs, currentTimeMs + halfWindow);

    if (exportEndMs_ - exportStartMs_ < 1000) {
        exportEndMs_ = std::min(exportMaxTimeMs_, exportStartMs_ + 1000);
    }
}

void UIWindowManager::renderExportSnapshotModal() {
    if (!showExportModal_) {
        return;
    }

    ImGui::OpenPopup(L("export.title"));

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Always);

    bool open = true;
    if (ImGui::BeginPopupModal(L("export.title"), &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", L("export.time_range"));
        ImGui::Separator();

        auto formatTime = [](int32_t ms) -> std::string {
            int32_t totalSec = ms / 1000;
            int32_t min = totalSec / 60;
            int32_t sec = totalSec % 60;
            int32_t millis = ms % 1000;
            char buf[32];
            snprintf(buf, sizeof(buf), "%d:%02d.%03d", min, sec, millis);
            return buf;
        };

        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s:", L("export.start_time"));
        ImGui::SameLine(120);
        ImGui::SetNextItemWidth(120);

        float startSec = static_cast<float>(exportStartMs_) / 1000.0f;
        float maxSec = static_cast<float>(exportMaxTimeMs_) / 1000.0f;
        if (ImGui::SliderFloat("##StartTime", &startSec, 0.0f, maxSec, "%.1fs")) {
            exportStartMs_ = static_cast<int32_t>(startSec * 1000.0f);
            if (exportStartMs_ >= exportEndMs_) {
                exportEndMs_ = std::min(exportMaxTimeMs_, exportStartMs_ + 1000);
            }
        }
        ImGui::SameLine();
        ImGui::Text("(%s)", formatTime(exportStartMs_).c_str());

        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s:", L("export.end_time"));
        ImGui::SameLine(120);
        ImGui::SetNextItemWidth(120);

        float endSec = static_cast<float>(exportEndMs_) / 1000.0f;
        float minEndSec = static_cast<float>(exportStartMs_) / 1000.0f + 1.0f;
        if (ImGui::SliderFloat("##EndTime", &endSec, minEndSec, maxSec, "%.1fs")) {
            exportEndMs_ = static_cast<int32_t>(endSec * 1000.0f);
        }
        ImGui::SameLine();
        ImGui::Text("(%s)", formatTime(exportEndMs_).c_str());

        ImGui::Spacing();

        int32_t duration = exportEndMs_ - exportStartMs_;
        ImGui::Text("%s: %s", L("export.duration"), formatTime(duration).c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float buttonWidth = 100.0f;
        float availWidth = ImGui::GetContentRegionAvail().x;
        float startX = (availWidth - buttonWidth * 2 - ImGui::GetStyle().ItemSpacing.x) / 2;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + startX);

        if (ImGui::Button(L("export.export_button"), ImVec2(buttonWidth, 0))) {
            exportRequest_.active = true;
            exportRequest_.startTimeMs = exportStartMs_;
            exportRequest_.endTimeMs = exportEndMs_;
            showExportModal_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button(L("export.cancel_button"), ImVec2(buttonWidth, 0))) {
            showExportModal_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    if (!open) {
        showExportModal_ = false;
    }
}
