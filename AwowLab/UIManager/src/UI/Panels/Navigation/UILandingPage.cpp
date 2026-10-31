#include "UI/Panels/Navigation/UILandingPage.h"
#include "UI/LandingPageColors.h"
#include "UI/AwlUI/Widgets.h"
#include "UI/UISettings.h"
#include "Core/LocalizationManager.h"
#include "Core/ErrorLogger.h"
#include "Core/SpellNameDatabase.h"
#include "Core/UnifiedSettings.h"
#include "Core/BrowserHelper.h"
#include "AwowLabBuildOptions.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <fstream>
#include <cstring>
#include <filesystem>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <shellapi.h>
#endif

UILandingPage::UILandingPage() {
    wclUrlDialog_ = std::make_unique<UIWarcraftLogsUrlDialog>();
    cachedFightsPanel_ = std::make_unique<UICachedFightsPanel>();
    mentorPicker_ = std::make_unique<benchmark_ui::UIMentorPicker>();
}

void UILandingPage::reset() {
    result_ = LandingPageResult{};
    wclDialogOpen_ = false;
    cachedFightsPanelOpen_ = false;
    playAlongPending_ = false;
    // A cancelled prepare would otherwise leave this set, and the next cached
    // fight picked for any reason would be taken as a reference request.
    prepareMentorPending_ = false;
    mentorPickerOpen_ = false;
    if (wclUrlDialog_) {
        wclUrlDialog_->reset();
    }
    if (cachedFightsPanel_) {
        cachedFightsPanel_->reset();
    }
    hoveredCard_ = -1;
}

void UILandingPage::setResponseCache(warcraftlogs::ResponseCache* cache) {
    if (cachedFightsPanel_) {
        cachedFightsPanel_->setCache(cache);
    }
}

size_t UILandingPage::getCachedFightCount() const {
    if (cachedFightsPanel_) {
        return cachedFightsPanel_->getCachedFightCount();
    }
    return 0;
}

void UILandingPage::markWclTokenInvalid(const std::string& errorMessage) {
    if (wclUrlDialog_) {
        wclUrlDialog_->markTokenInvalid(errorMessage);
    }
}

bool UILandingPage::render() {
    if (result_.choice != LandingPageChoice::None) {
        return true;  // Already made a selection
    }

    // Auto-open WCL dialog if flagged (e.g., after auth failure)
    if (autoOpenWclDialog_ && wclUrlDialog_ && !wclDialogOpen_) {
        wclUrlDialog_->open();
        wclDialogOpen_ = true;
        autoOpenWclDialog_ = false;  // Clear flag after opening
    }

    animationTime_ += ImGui::GetIO().DeltaTime;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 windowSize = viewport->Size;

    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(windowSize);

    ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    applyLandingTheme();

    ImGui::Begin("##LandingPage", nullptr, windowFlags);

    // Get UI scale factor for proper scaling at different font sizes
    float scale = ImGui::GetIO().FontGlobalScale;

    // Calculate content width (max 900px * scale, centered)
    float contentWidth = std::min(900.0f * scale, windowSize.x - 80.0f * scale);
    float contentStartX = (windowSize.x - contentWidth) * 0.5f;

    ImGui::SetCursorPosX(contentStartX);
    ImGui::BeginGroup();

    // Vertical centering - estimate total content height more accurately
    // Header ~100, Cards ~220, Links ~40, Footer ~30, spacing ~100
    float totalHeight = 490.0f * scale;
    float startY = std::max(20.0f * scale, (windowSize.y - totalHeight) * 0.5f);
    ImGui::SetCursorPosY(startY);

    renderHeader();

    ImGui::Dummy(ImVec2(0, 8.0f * scale));  // Tight spacing

    renderProjectLinks();

    ImGui::Dummy(ImVec2(0, 24.0f * scale));  // Spacing before cards

    renderSourceCards();

    ImGui::Dummy(ImVec2(0, 16.0f * scale));  // Spacing

    // Show language pack download card if current locale needs optional pack and it's missing
    renderLanguagePackCard();

    ImGui::Dummy(ImVec2(0, 16.0f * scale));  // Spacing

    renderFooter();

    ImGui::EndGroup();

    if (wclDialogOpen_ && wclUrlDialog_) {
        if (wclUrlDialog_->render()) {
            // Dialog closed
            wclDialogOpen_ = false;
            const auto& dialogResult = wclUrlDialog_->getResult();

            if (dialogResult.submitted) {
                result_.choice = LandingPageChoice::WarcraftLogs;
                result_.warcraftLogsUrl = dialogResult.url;
                result_.useUserAuth = dialogResult.useUserAuth;
                result_.accessToken = dialogResult.accessToken;
            }
            // If cancelled, just close dialog and stay on landing page
        }
    }

    if (cachedFightsPanelOpen_ && cachedFightsPanel_) {
        if (cachedFightsPanel_->render(classIconCache_)) {
            // Panel closed
            cachedFightsPanelOpen_ = false;
            const auto& selection = cachedFightsPanel_->getSelection();

            if (selection.selected) {
                if (playAlongPending_) {
                    result_.choice = LandingPageChoice::PlayAlong;
                    playAlongPending_ = false;
                } else if (prepareMentorPending_) {
                    // Loads the log, then asks which player to turn into a
                    // reference. A distinct choice from CachedFight, which
                    // would just open the replay and skip the whole flow.
                    result_.choice = LandingPageChoice::PrepareMentor;
                    prepareMentorPending_ = false;
                    AWOW_LOG_INFO("BENCHMARK",
                        "Cached fight picked for PrepareMentor: " +
                        selection.reportCode + " fight " +
                        std::to_string(selection.fightId));
                } else {
                    result_.choice = LandingPageChoice::CachedFight;
                }
                result_.cachedReportCode = selection.reportCode;
                result_.cachedFightId = selection.fightId;
            } else {
                // Cancelled - clear the pending flags
                playAlongPending_ = false;
                prepareMentorPending_ = false;
            }
            // If cancelled, just close panel and stay on landing page
        }
    }

    // Encounter-first picker for the two comparison flows.
    if (mentorPickerOpen_ && mentorPicker_) {
        benchmark_ui::MentorPickerContext pickerContext;

        // Only encounters that actually have a reference. The rest would be
        // dead entries the user could select and get nothing from.
        auto& session = benchmark::BenchmarkSession::instance();
        const auto encounterNames = cachedFightsPanel_
            ? cachedFightsPanel_->getEncounterNames()
            : std::unordered_map<uint32_t, std::string>{};

        std::vector<uint32_t> withMentors;
        for (const auto& entry : session.storedMentors()) {
            if (std::find(withMentors.begin(), withMentors.end(),
                          entry.key.encounterId) == withMentors.end()) {
                withMentors.push_back(entry.key.encounterId);
            }
        }
        for (uint32_t encounterId : withMentors) {
            auto it = encounterNames.find(encounterId);
            pickerContext.encounters.emplace_back(
                encounterId,
                it != encounterNames.end()
                    ? it->second
                    : LF("mentorpicker.unknown_encounter", encounterId));
        }

        if (cachedFightsPanel_) {
            for (const auto& fight : cachedFightsPanel_->getAllFights()) {
                if (fight.encounterID <= 0) continue;
                benchmark_ui::PickerFight entry;
                entry.reportCode = fight.reportCode;
                entry.fightId = fight.fightId;
                entry.label = fight.fightName.empty() ? fight.reportCode
                                                      : fight.fightName;
                entry.encounterId = static_cast<uint32_t>(fight.encounterID);
                entry.durationMs = static_cast<int32_t>(fight.durationMs);
                entry.startTimeMs = fight.startTimeMs;
                entry.difficulty = fight.difficultyName();
                entry.groupLead = fight.firstPlayerName;
                entry.cached = fight.fullyLoaded;
                pickerContext.fights.push_back(std::move(entry));
            }
        }

        if (mentorPicker_->render(pickerContext)) {
            mentorPickerOpen_ = false;
            const auto& picked = mentorPicker_->result();

            if (picked.confirmed) {
                result_.benchmarkEncounterId = picked.mentor.encounterId;
                result_.benchmarkSpecId = picked.mentor.specId;

                if (picked.fightId >= 0) {
                    result_.choice = LandingPageChoice::CompareReports;
                    result_.cachedReportCode = picked.reportCode;
                    result_.cachedFightId = picked.fightId;
                } else {
                    result_.choice = LandingPageChoice::CompareLive;
                }
            }
        }
    }

    ImGui::End();

    return result_.choice != LandingPageChoice::None;
}

void UILandingPage::renderHeader() {
    // Get UI scale factor for proper scaling at different font sizes
    float scale = ImGui::GetIO().FontGlobalScale;

    // App title with animated glow effect
    [[maybe_unused]] float glowPulse = 0.5f + 0.5f * std::sin(animationTime_ * 2.0f);

    // Title: "AwowLab" - use header font (Fonts[1]) for crisp rendering
    {
        ImGui::PushStyleColor(ImGuiCol_Text, LandingColors::TextPrimary);

        // Use header font if available (loaded at 36px), otherwise fall back to scaling
        ImFont* headerFont = (ImGui::GetIO().Fonts->Fonts.Size > 1) ? ImGui::GetIO().Fonts->Fonts[1] : nullptr;

        const char* title = L("landing.title");
        float titleWidth;

        if (headerFont) {
            ImGui::PushFont(headerFont);
            titleWidth = ImGui::CalcTextSize(title).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (900.0f * scale - titleWidth) * 0.5f);
            ImGui::Text("%s", title);
            ImGui::PopFont();
        } else {
            // Fallback: scale default font (will be blurry)
            titleWidth = ImGui::CalcTextSize(title).x * 2.5f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (900.0f * scale - titleWidth) * 0.5f);
            ImGui::SetWindowFontScale(2.5f);
            ImGui::Text("%s", title);
            ImGui::SetWindowFontScale(1.0f);
        }

        ImGui::PopStyleColor();
    }

    // Subtitle
    {
        ImGui::PushStyleColor(ImGuiCol_Text, LandingColors::TextSecondary);

        const char* subtitle = L("landing.subtitle");
        float subtitleWidth = ImGui::CalcTextSize(subtitle).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (900.0f * scale - subtitleWidth) * 0.5f);

        ImGui::Text("%s", subtitle);

        ImGui::PopStyleColor();
    }

    // Check for Updates button (no version number)
    {
        ImGui::Dummy(ImVec2(0, 8.0f * scale));

#if AWOWLAB_ENABLE_UPDATER
        const char* updateText = L("btn.check_updates");
        float updateBtnWidth = ImGui::CalcTextSize(updateText).x + 24.0f * scale;

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (900.0f * scale - updateBtnWidth) * 0.5f);

        if (awlui::Button(updateText, awlui::ButtonVariant::Primary,
                          awlui::ButtonSize::Md)) {
            // Launch updater in update mode
            launchUpdater("--update");
        }
#else
        // Built without the updater - say so where the button would be
        const char* updateOffText = L("update.disabled_build");
        float updateOffWidth = ImGui::CalcTextSize(updateOffText).x;

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (900.0f * scale - updateOffWidth) * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, LandingColors::TextMuted);
        ImGui::Text("%s", updateOffText);
        ImGui::PopStyleColor();
#endif
    }

    // Language selector
    {
        ImGui::Dummy(ImVec2(0, 12.0f * scale));

        auto& locMgr = LocalizationManager::instance();
        Locale currentLocale = locMgr.getLocale();

        float comboWidth = 180.0f * scale;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (900.0f * scale - comboWidth) * 0.5f);

        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.20f, 0.20f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, LandingColors::TextSecondary);
        ImGui::PushItemWidth(comboWidth);

        const char* currentName = LocalizationManager::getLocaleDisplayName(currentLocale);
        if (ImGui::BeginCombo("##language", currentName, ImGuiComboFlags_HeightLarge)) {
            for (uint8_t i = 0; i < static_cast<uint8_t>(Locale::COUNT); ++i) {
                Locale locale = static_cast<Locale>(i);
                bool isSelected = (currentLocale == locale);
                if (ImGui::Selectable(LocalizationManager::getLocaleDisplayName(locale), isSelected)) {
                    locMgr.setLocale(locale);
                    // Also update spell name locale to match
                    SpellNameDatabase::instance().setSpellNameLocale(locale);

                    auto& cache = SettingsCache::instance();
                    cache.get().locale = LocalizationManager::getLocaleCode(locale);
                    cache.markDirty();
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::PopItemWidth();
        ImGui::PopStyleColor(3);
    }
}

void UILandingPage::renderFooter() {
    // Get UI scale factor for proper scaling at different font sizes
    float scale = ImGui::GetIO().FontGlobalScale;

    // Footer with links/info
    ImGui::PushStyleColor(ImGuiCol_Text, LandingColors::TextMuted);

    // Centered footer - press ESC to exit
    const char* footer = L("landing.press_esc");
    float footerWidth = ImGui::CalcTextSize(footer).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (900.0f * scale - footerWidth) * 0.5f);
    ImGui::Text("%s", footer);

    ImGui::PopStyleColor();  // TextMuted

    // ESC exits only from the bare landing page. While a dialog, popup or
    // text field is up, ESC belongs to that and closing it must not quit.
    const bool landingDialogOpen = wclDialogOpen_ || cachedFightsPanelOpen_ ||
                                   mentorPickerOpen_;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !landingDialogOpen &&
        !ImGui::IsAnyItemActive() &&
        !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        result_.choice = LandingPageChoice::Exit;
    }
}

void UILandingPage::renderProjectLinks() {
    float scale = ImGui::GetIO().FontGlobalScale;

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Issues and releases on the project page this build was configured
    // with (project_url in awowlab.config.json), so a fork links to itself
    {
        static const std::string issuesUrl = std::string(AWOWLAB_PROJECT_URL) + "/issues";
        static const std::string releasesUrl = std::string(AWOWLAB_PROJECT_URL) + "/releases";
        const char* issuesText = L("landing.report_issue");
        const char* releasesText = L("landing.releases");

        // Small awlui buttons pad the label by 10px on each side
        const float padX = 20.0f;
        float linkSpacing = 16.0f * scale;
        float linksWidth = ImGui::CalcTextSize(issuesText).x + ImGui::CalcTextSize(releasesText).x +
                           padX * 2.0f + linkSpacing;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (900.0f * scale - linksWidth) * 0.5f);

        auto projectLink = [&](const char* text, const std::string& url) {
            if (awlui::Button(text, awlui::ButtonVariant::Ghost, awlui::ButtonSize::Sm)) {
                BrowserHelper::openUrl(url);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", url.c_str());
            }
        };
        projectLink(issuesText, issuesUrl);
        ImGui::SameLine(0, linkSpacing);
        projectLink(releasesText, releasesUrl);
    }

    // Translations are the CSV files under AwowLab/data/lang, changed by pull
    // request; the contributing guide on the project page explains how.
    // English readers have nothing to translate, so they don't see the link.
    if (LocalizationManager::instance().getLocale() != Locale::en_US) {
        static const std::string translateUrl =
            std::string(AWOWLAB_PROJECT_URL) + "/blob/HEAD/CONTRIBUTING.md#translations";

        ImGui::Dummy(ImVec2(0, 16.0f * scale));

        const char* helpText = L("menu.help_improve_localization");
        float helpWidth = ImGui::CalcTextSize(helpText).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (900.0f * scale - helpWidth) * 0.5f);

        // Ghost variant reads as a subtle chrome affordance; keep
        // the manual underline-on-hover so users clearly see this
        // is a link rather than an inert button.
        if (awlui::Button(helpText, awlui::ButtonVariant::Ghost)) {
            BrowserHelper::openUrl(translateUrl);
        }

        if (ImGui::IsItemHovered()) {
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            drawList->AddLine(
                ImVec2(min.x, max.y - 1), ImVec2(max.x, max.y - 1),
                IM_COL32(200, 200, 210, 200), 1.0f);
            ImGui::SetTooltip("%s\n%s", L("landing.translate_hint"), translateUrl.c_str());
        }
    }
}

void UILandingPage::launchUpdater(const std::string& args) {
#if !AWOWLAB_ENABLE_UPDATER
    // Built without the updater; the buttons that call this are hidden too
    (void)args;
#elif defined(_WIN32)
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();

    // Construct path to updater
    std::filesystem::path updaterPath = exeDir / "updater" / "AwowLabUpdater.exe";

    if (!std::filesystem::exists(updaterPath)) {
        // Updater not found - show a message
        MessageBoxA(nullptr,
            "Updater not found.\n\nThe updater (updater/AwowLabUpdater.exe) is missing.\n"
            "Please download the latest version from GitHub.",
            "Update Check", MB_ICONINFORMATION);
        return;
    }

    // Launch updater with specified arguments
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    std::string cmdLine = "\"" + updaterPath.string() + "\" " + args;

    if (CreateProcessA(
        nullptr,
        cmdLine.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        exeDir.string().c_str(),
        &si,
        &pi
    )) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        // Signal to exit the application for the update
        result_.choice = LandingPageChoice::ExitForUpdate;
    }
#else
    // Linux/macOS: Not implemented yet
    (void)args;
#endif
}
