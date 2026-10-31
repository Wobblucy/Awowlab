#include "UI/Panels/WarcraftLogs/UIWarcraftLogsUrlDialog.h"
#include "UI/AwlUI/Widgets.h"
#include "Auth/UserOAuthClient.h"
#include "Auth/Credentials.h"
#include "Core/UnifiedSettings.h"
#include "Core/Settings/WclSettings.h"
#include "Core/LocalizationManager.h"
#include "Core/BrowserHelper.h"

#include <imgui.h>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <cctype>
#include <mutex>
#include <thread>

namespace {

// Dialog dimensions
constexpr float DIALOG_WIDTH = 1650.0f;
constexpr float DIALOG_HEIGHT = 900.0f;
constexpr float PADDING = 20.0f;
constexpr float INPUT_HEIGHT = 32.0f;
constexpr float BUTTON_HEIGHT = 36.0f;

// Colors matching the app theme
namespace Colors {
    const ImVec4 Background{0.10f, 0.10f, 0.15f, 1.0f};
    const ImVec4 InputBg{0.06f, 0.06f, 0.10f, 1.0f};
    const ImVec4 InputBgHover{0.08f, 0.08f, 0.14f, 1.0f};
    const ImVec4 AuthButton{0.55f, 0.35f, 0.75f, 1.0f};
    const ImVec4 AuthButtonHover{0.65f, 0.45f, 0.85f, 1.0f};
    const ImVec4 AuthButtonDisabled{0.35f, 0.25f, 0.45f, 1.0f};
    const ImVec4 LoadButton{0.20f, 0.55f, 0.35f, 1.0f};
    const ImVec4 LoadButtonHover{0.25f, 0.65f, 0.40f, 1.0f};
    const ImVec4 CancelButton{0.45f, 0.45f, 0.50f, 1.0f};
    const ImVec4 CancelButtonHover{0.55f, 0.55f, 0.60f, 1.0f};
    const ImVec4 TextPrimary{0.95f, 0.95f, 0.98f, 1.0f};
    const ImVec4 TextMuted{0.55f, 0.55f, 0.65f, 1.0f};
    const ImVec4 TextSuccess{0.30f, 0.90f, 0.40f, 1.0f};
    const ImVec4 TextError{1.0f, 0.50f, 0.35f, 1.0f};
    const ImVec4 TextWarning{1.0f, 0.75f, 0.30f, 1.0f};
    const ImVec4 Separator{0.30f, 0.30f, 0.40f, 0.50f};
}

} // anonymous namespace

struct UIWarcraftLogsUrlDialog::AuthTask {
    std::mutex mutex;
    std::string status;
    bool done = false;
    warcraftlogs::AuthResult result;
};


namespace {

// User points per hour by Archon (WarcraftLogs) subscription tier. The tier
// names are Archon's own and stay in English.
struct PointsTier {
    const char* name;  // nullptr for no subscription
    int pointsPerHour;
};
constexpr PointsTier kPointsTiers[] = {
    {nullptr, 100},
    {"Silver", 250},
    {"Gold", 400},
    {"Platinum", 800},
};

// Two-column table of label and number for the points tooltip
bool beginPointsTable(const char* id, const char* labelHeader, const char* valueHeader) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit)) {
        return false;
    }
    ImGui::TableSetupColumn(labelHeader, ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 12.0f);
    ImGui::TableSetupColumn(valueHeader, ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 9.0f);
    ImGui::TableHeadersRow();
    return true;
}

void pointsRow(const char* label, const char* value) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(value);
}

constexpr const char* kArchonSubscribeUrl = "https://www.patreon.com/warcraftlogs";

} // namespace

UIWarcraftLogsUrlDialog::UIWarcraftLogsUrlDialog() {
    loadPersistedToken();
}

UIWarcraftLogsUrlDialog::~UIWarcraftLogsUrlDialog() = default;

void UIWarcraftLogsUrlDialog::open() {
    isOpen_ = true;
    shouldClose_ = false;
    result_ = Result{};
    memset(urlBuffer_, 0, sizeof(urlBuffer_));

    // If the clipboard already holds a WarcraftLogs link (fresh copy
    // from the browser), drop it straight into the field so the user
    // doesn't have to paste by hand.
    if (ImGui::GetCurrentContext()) {
        const char* clip = ImGui::GetClipboardText();
        if (clip && strstr(clip, "warcraftlogs.com")) {
            std::string trimmed(clip);
            while (!trimmed.empty() &&
                   (trimmed.back() == '\n' || trimmed.back() == '\r' ||
                    trimmed.back() == ' ' || trimmed.back() == '\t')) {
                trimmed.pop_back();
            }
            size_t start = trimmed.find_first_not_of(" \t");
            if (start != std::string::npos && start > 0) {
                trimmed.erase(0, start);
            }
            if (!trimmed.empty() && trimmed.size() < sizeof(urlBuffer_)) {
                memcpy(urlBuffer_, trimmed.c_str(), trimmed.size() + 1);
                validateUrl();
            }
        }
    }

    // Check for a stored auth error (from failed queries)
    if (!awow::wclAuthError().empty()) {
        authStatusMessage_ = awow::wclAuthError();
        // Clear the token since it's invalid
        isAuthenticated_ = false;
        accessToken_.clear();
    }
    // No expiry check - we only invalidate on query failure
}

void UIWarcraftLogsUrlDialog::close() {
    shouldClose_ = true;
    // A points notice has been seen; it shouldn't reopen the dialog later
    awow::wclNotice().clear();
}

void UIWarcraftLogsUrlDialog::reset() {
    isOpen_ = false;
    shouldClose_ = false;
    result_ = Result{};
    memset(urlBuffer_, 0, sizeof(urlBuffer_));
    isAuthenticating_ = (authTask_ != nullptr);
}

bool UIWarcraftLogsUrlDialog::render() {
    pollAuthentication();

    if (!isOpen_) {
        return false;
    }

    // OpenPopup has to come before BeginPopupModal
    if (!ImGui::IsPopupOpen("WarcraftLogs Report")) {
        ImGui::OpenPopup("WarcraftLogs Report");
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(DIALOG_WIDTH, DIALOG_HEIGHT), ImGuiCond_Appearing);

    // Style the window
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Colors::Background);
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.12f, 0.12f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.15f, 0.15f, 0.22f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(PADDING, PADDING));

    bool isWindowOpen = true;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

    if (ImGui::BeginPopupModal("WarcraftLogs Report", &isWindowOpen, flags)) {
        renderUrlSection();
        renderAuthSection();
        renderButtons();

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    if (!isWindowOpen) {
        result_.cancelled = true;
        shouldClose_ = true;
    }

    if (shouldClose_) {
        isOpen_ = false;
        ImGui::CloseCurrentPopup();
        return true;  // Dialog closed
    }

    return false;  // Dialog still open
}

void UIWarcraftLogsUrlDialog::renderUrlSection() {
    ImGui::TextColored(Colors::TextPrimary, "%s", L("wcl.paste_url"));
    ImGui::Spacing();

    // Determine border color based on validation status
    ImVec4 borderColor = Colors::InputBg;
    if (urlBuffer_[0] != '\0') {
        switch (urlValidationStatus_) {
            case UrlValidationStatus::Valid:
                borderColor = ImVec4(0.2f, 0.6f, 0.3f, 1.0f);  // Green
                break;
            case UrlValidationStatus::Invalid:
                borderColor = ImVec4(0.8f, 0.3f, 0.3f, 1.0f);  // Red
                break;
            case UrlValidationStatus::TooShort:
                borderColor = ImVec4(0.7f, 0.5f, 0.2f, 1.0f);  // Orange
                break;
            default:
                break;
        }
    }

    // URL input field
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Colors::InputBg);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Colors::InputBgHover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Colors::InputBgHover);
    ImGui::PushStyleColor(ImGuiCol_Border, borderColor);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);

    float inputWidth = DIALOG_WIDTH - (PADDING * 2);
    ImGui::PushItemWidth(inputWidth);

    // Auto-focus on first frame
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }

    std::string previousInput = urlBuffer_;

    ImGui::InputTextWithHint(
        "##WclUrlInput",
        L("wcl.url_hint"),
        urlBuffer_,
        sizeof(urlBuffer_)
    );

    // Validate on change
    if (previousInput != urlBuffer_) {
        validateUrl();
    }

    ImGui::PopItemWidth();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(4);

    ImGui::Spacing();
    if (urlBuffer_[0] != '\0') {
        switch (urlValidationStatus_) {
            case UrlValidationStatus::Valid:
                ImGui::TextColored(Colors::TextSuccess, L("wcl.valid_code"), extractedReportCode_.c_str());
                break;
            case UrlValidationStatus::Invalid:
                ImGui::TextColored(Colors::TextError, "%s", urlValidationMessage_.c_str());
                break;
            case UrlValidationStatus::TooShort:
                ImGui::TextColored(Colors::TextWarning, "%s", urlValidationMessage_.c_str());
                break;
            default:
                break;
        }
    } else {
        ImGui::TextColored(Colors::TextMuted, "%s", L("wcl.example_hint"));
    }
}

void UIWarcraftLogsUrlDialog::renderAuthSection() {
    ImGui::Spacing();

    // Separator with label
    ImGui::PushStyleColor(ImGuiCol_Separator, Colors::Separator);
    float separatorWidth = (DIALOG_WIDTH - (PADDING * 2) - 120.0f) / 2.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));

    // Left separator line
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(cursorPos.x, cursorPos.y + 8.0f),
        ImVec2(cursorPos.x + separatorWidth, cursorPos.y + 8.0f),
        ImGui::ColorConvertFloat4ToU32(Colors::Separator),
        1.0f
    );

    ImGui::SetCursorPosX(PADDING + separatorWidth + 8.0f);
    ImGui::TextColored(Colors::TextMuted, "%s", L("wcl.private_logs"));
    ImGui::SameLine();

    // Right separator line
    cursorPos = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(cursorPos.x + 8.0f, cursorPos.y + 8.0f),
        ImVec2(cursorPos.x + 8.0f + separatorWidth, cursorPos.y + 8.0f),
        ImGui::ColorConvertFloat4ToU32(Colors::Separator),
        1.0f
    );

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::Spacing();

    // Determine if we need to show the auth button:
    // - Show if not authenticated (no cached token)
    // - Show if there's an error message (last query failed, token expired, etc.)
    // - Hide if authenticated with no errors (token is valid and working)
    bool hasAuthError = !authStatusMessage_.empty() &&
        (authStatusMessage_.find("expired") != std::string::npos ||
         authStatusMessage_.find("failed") != std::string::npos ||
         authStatusMessage_.find("error") != std::string::npos);
    bool showAuthButton = !isAuthenticated_ || hasAuthError || isAuthenticating_;

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

    float authButtonHeight = ImGui::GetTextLineHeight() + ImGui::GetStyle().FramePadding.y * 2.0f + 8.0f;

    if (showAuthButton) {
        if (isAuthenticating_) {
            ImGui::BeginDisabled();
            awlui::Button(L("wcl.authorizing"), awlui::ButtonVariant::Secondary,
                          awlui::ButtonSize::Md, ImVec2(0, authButtonHeight));
            ImGui::EndDisabled();
        } else {
            const char* buttonText = hasAuthError ? L("btn.reauthorize") : L("btn.authorize");
            if (awlui::Button(buttonText, awlui::ButtonVariant::Primary,
                              awlui::ButtonSize::Md, ImVec2(0, authButtonHeight))) {
                startAuthentication();
            }
        }
    }

    ImGui::PopStyleVar();

    // Status text
    if (showAuthButton) {
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.0f);  // Vertical align with button
    }

    if (isAuthenticating_) {
        ImGui::TextColored(Colors::TextWarning, "%s", L("wcl.waiting_browser"));
    } else if (isAuthenticated_ && !hasAuthError) {
        ImGui::TextColored(Colors::TextSuccess, "%s", L("wcl.authenticated"));
    } else if (!authStatusMessage_.empty()) {
        if (hasAuthError) {
            ImGui::TextColored(Colors::TextError, "%s", authStatusMessage_.c_str());
        } else {
            ImGui::TextColored(Colors::TextMuted, "%s", authStatusMessage_.c_str());
        }
    } else {
        ImGui::TextColored(Colors::TextMuted, "%s", L("wcl.public_only"));
    }

    // What a load costs from the user's own WarcraftLogs points
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
        ImGui::TextUnformatted(L("wcl.points_intro"));
        ImGui::Spacing();

        char value[32];
        if (beginPointsTable("##PointsCost", L("wcl.points_col_content"), L("wcl.points_col_per_minute"))) {
            std::snprintf(value, sizeof(value), "~%.0f", awow::kRaidPointsPerMinute);
            pointsRow(L("wcl.points_raid"), value);
            std::snprintf(value, sizeof(value), "~%.1f", awow::kMythicPlusPointsPerMinute);
            pointsRow(L("wcl.points_mplus"), value);
            ImGui::EndTable();
        }
        ImGui::Spacing();

        if (beginPointsTable("##PointsTiers", L("wcl.points_col_tier"), L("wcl.points_col_per_hour"))) {
            for (const auto& tier : kPointsTiers) {
                std::snprintf(value, sizeof(value), "%d", tier.pointsPerHour);
                pointsRow(tier.name ? tier.name : L("wcl.points_tier_none"), value);
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();

        ImGui::TextUnformatted(L("wcl.points_shared"));
        ImGui::Spacing();
        ImGui::TextDisabled("%s", L("wcl.points_third_party"));
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::SameLine();
    if (awlui::Button(L("wcl.points_more"), awlui::ButtonVariant::Ghost, awlui::ButtonSize::Sm)) {
        BrowserHelper::openUrl(kArchonSubscribeUrl);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s\n%s", L("wcl.points_third_party"), kArchonSubscribeUrl);
    }

    if (!awow::wclNotice().empty()) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(Colors::TextWarning, "%s", awow::wclNotice().c_str());
        ImGui::PopTextWrapPos();
    }

    ImGui::Spacing();
}

void UIWarcraftLogsUrlDialog::renderButtons() {
    ImGui::Spacing();
    ImGui::Spacing();

    // Calculate button sizes based on text content with proper scaling
    const char* loadText = L("btn.load_report");
    const char* cancelText = L("btn.cancel");

    // Use frame padding for consistent sizing - buttons auto-center text
    float horizontalPadding = ImGui::GetStyle().FramePadding.x * 2.0f + 24.0f;
    float loadWidth = ImGui::CalcTextSize(loadText).x + horizontalPadding;
    float cancelWidth = ImGui::CalcTextSize(cancelText).x + horizontalPadding;

    // Scale button height based on text height + padding
    float textHeight = ImGui::GetTextLineHeight();
    float buttonHeight = textHeight + ImGui::GetStyle().FramePadding.y * 2.0f + 8.0f;

    float spacing = 10.0f;
    float totalWidth = loadWidth + cancelWidth + spacing;
    float startX = (DIALOG_WIDTH - totalWidth) / 2.0f;

    ImGui::SetCursorPosX(startX);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

    // Load Report button - only enabled when URL is valid
    bool canLoad = (urlValidationStatus_ == UrlValidationStatus::Valid);
    if (!canLoad) {
        ImGui::BeginDisabled();
    }

    if (awlui::Button(loadText, awlui::ButtonVariant::Primary,
                      awlui::ButtonSize::Md, ImVec2(loadWidth, buttonHeight))) {
        result_.submitted = true;
        result_.url = urlBuffer_;
        result_.useUserAuth = isAuthenticated_;
        result_.accessToken = accessToken_;
        shouldClose_ = true;
    }

    if (!canLoad) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine(0.0f, spacing);

    if (awlui::Button(cancelText, awlui::ButtonVariant::Ghost,
                      awlui::ButtonSize::Md, ImVec2(cancelWidth, buttonHeight))) {
        result_.cancelled = true;
        shouldClose_ = true;
    }

    ImGui::PopStyleVar();
}

void UIWarcraftLogsUrlDialog::startAuthentication() {
    if (isAuthenticating_ || authTask_) {
        return;  // Already in progress
    }

    isAuthenticating_ = true;
    authStatusMessage_ = "Opening browser...";

    // The worker owns its own client and only touches the shared task, so
    // nothing here has to outlive the dialog. The client has no way to
    // abort the wait, so closing the dialog just stops listening for it.
    auto task = std::make_shared<AuthTask>();
    authTask_ = task;
    std::thread([task]() {
        warcraftlogs::UserOAuthClient client;
        auto result = client.authenticate(
            [task](const std::string& message) {
                std::lock_guard<std::mutex> lock(task->mutex);
                task->status = message;
            },
            120  // 2 minute timeout
        );
        std::lock_guard<std::mutex> lock(task->mutex);
        task->result = std::move(result);
        task->done = true;
    }).detach();
}

void UIWarcraftLogsUrlDialog::pollAuthentication() {
    if (!authTask_) {
        return;
    }

    warcraftlogs::AuthResult result;
    {
        std::lock_guard<std::mutex> lock(authTask_->mutex);
        if (!authTask_->done) {
            if (!authTask_->status.empty()) {
                authStatusMessage_ = authTask_->status;
            }
            return;
        }
        result = std::move(authTask_->result);
    }
    authTask_.reset();

    isAuthenticating_ = false;

    if (result.success) {
        isAuthenticated_ = true;
        accessToken_ = result.accessToken;
        tokenExpiry_ = result.expiresAt;
        authStatusMessage_.clear();
        savePersistedToken();

        awow::wclAuthError().clear();
    } else {
        isAuthenticated_ = false;
        accessToken_.clear();
        authStatusMessage_ = result.errorMessage.empty() ? "Authentication failed" : result.errorMessage;
    }
}

void UIWarcraftLogsUrlDialog::loadPersistedToken() {
    auto wclSettings = WclSettings::readFrom(SettingsCache::instance().get());

    if (wclSettings.accessToken.empty()) {
        authStatusMessage_ = "";
        return;
    }

    // No expiry check - we only invalidate on query failure
    // Token is valid until a query fails
    accessToken_ = wclSettings.accessToken;
    isAuthenticated_ = true;
    authStatusMessage_.clear();
}

void UIWarcraftLogsUrlDialog::savePersistedToken() {
    if (accessToken_.empty()) {
        return;
    }

    auto& cache = SettingsCache::instance();
    auto wclSettings = WclSettings::readFrom(cache.get());
    wclSettings.accessToken = accessToken_;
    wclSettings.tokenExpiresAt = static_cast<int64_t>(
        std::chrono::system_clock::to_time_t(tokenExpiry_)
    );
    wclSettings.writeTo(cache.get());
    cache.markDirty();

    // Force immediate save for auth tokens (important not to lose)
    cache.forceSave();
}

void UIWarcraftLogsUrlDialog::clearPersistedToken() {
    auto& cache = SettingsCache::instance();
    auto wclSettings = WclSettings::readFrom(cache.get());
    wclSettings.accessToken.clear();
    wclSettings.tokenExpiresAt = 0;
    wclSettings.writeTo(cache.get());
    cache.markDirty();

    isAuthenticated_ = false;
    accessToken_.clear();
}

void UIWarcraftLogsUrlDialog::markTokenInvalid(const std::string& errorMessage) {
    // Clear the persisted token since it's no longer valid
    clearPersistedToken();

    // Set error message so the reauthorize button will show
    std::string error = errorMessage.empty() ? "Authorization failed - please re-authorize" : errorMessage;
    authStatusMessage_ = error;

    // Persist the error so it shows next time the dialog opens
    persistAuthError(error);
}

void UIWarcraftLogsUrlDialog::persistAuthError(const std::string& errorMessage) {
    std::string error = errorMessage.empty() ? "Authorization failed - please re-authorize" : errorMessage;
    awow::wclAuthError() = error;

    // Also clear the token since it's invalid
    auto& cache = SettingsCache::instance();
    auto wclSettings = WclSettings::readFrom(cache.get());
    wclSettings.accessToken.clear();
    wclSettings.tokenExpiresAt = 0;
    wclSettings.writeTo(cache.get());
    cache.markDirty();
}

void UIWarcraftLogsUrlDialog::clearPersistedAuthError() {
    awow::wclAuthError().clear();
}

void UIWarcraftLogsUrlDialog::validateUrl() {
    std::string input = urlBuffer_;
    urlValidationStatus_ = checkUrlFormat(input, extractedReportCode_, urlValidationMessage_);
}

UrlValidationStatus UIWarcraftLogsUrlDialog::checkUrlFormat(
    const std::string& input,
    std::string& outReportCode,
    std::string& outMessage
) {
    outReportCode.clear();
    outMessage.clear();

    // Trim whitespace
    std::string trimmed = input;
    size_t start = trimmed.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) {
        return UrlValidationStatus::Empty;
    }
    size_t end = trimmed.find_last_not_of(" \t\n\r");
    trimmed = trimmed.substr(start, end - start + 1);

    if (trimmed.empty()) {
        return UrlValidationStatus::Empty;
    }

    // Check if it's a URL
    bool isUrl = (trimmed.find("warcraftlogs.com") != std::string::npos ||
                  trimmed.find("http://") == 0 ||
                  trimmed.find("https://") == 0);

    if (isUrl) {
        // Try to extract report code from URL
        // Format: .../reports/CODE or .../reports/CODE?... or .../reports/CODE#...

        size_t reportsPos = trimmed.find("/reports/");
        if (reportsPos == std::string::npos) {
            outMessage = "URL must contain /reports/ path";
            return UrlValidationStatus::Invalid;
        }

        size_t codeStart = reportsPos + 9;  // Length of "/reports/"
        if (codeStart >= trimmed.length()) {
            outMessage = "Report code missing after /reports/";
            return UrlValidationStatus::Invalid;
        }

        // Find end of code (before ? or # or /)
        size_t codeEnd = trimmed.find_first_of("?#/", codeStart);
        if (codeEnd == std::string::npos) {
            codeEnd = trimmed.length();
        }

        std::string code = trimmed.substr(codeStart, codeEnd - codeStart);

        if (code.length() < 16) {
            outMessage = "Report code too short (need 16 characters, got " +
                        std::to_string(code.length()) + ")";
            return UrlValidationStatus::TooShort;
        }

        if (code.length() > 16) {
            outMessage = "Report code too long (expected 16 characters)";
            return UrlValidationStatus::Invalid;
        }

        // Validate alphanumeric
        for (char c : code) {
            if (!std::isalnum(static_cast<unsigned char>(c))) {
                outMessage = "Report code contains invalid character: '" + std::string(1, c) + "'";
                return UrlValidationStatus::Invalid;
            }
        }

        outReportCode = code;
        return UrlValidationStatus::Valid;

    } else {
        // Assume it's a report code directly
        if (trimmed.length() < 16) {
            if (trimmed.length() < 4) {
                outMessage = "Keep typing... (need 16 characters)";
            } else {
                outMessage = "Report code too short (" + std::to_string(trimmed.length()) + "/16 characters)";
            }
            return UrlValidationStatus::TooShort;
        }

        if (trimmed.length() > 16) {
            outMessage = "Report code too long (expected 16 characters, got " +
                        std::to_string(trimmed.length()) + ")";
            return UrlValidationStatus::Invalid;
        }

        // Validate alphanumeric
        for (char c : trimmed) {
            if (!std::isalnum(static_cast<unsigned char>(c))) {
                outMessage = "Invalid character in report code: '" + std::string(1, c) + "'";
                return UrlValidationStatus::Invalid;
            }
        }

        outReportCode = trimmed;
        return UrlValidationStatus::Valid;
    }
}
