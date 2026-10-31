#pragma once

#include <string>
#include <memory>
#include <chrono>
#include <functional>


/**
 * URL validation result.
 */
enum class UrlValidationStatus {
    Empty,           // No input yet
    TooShort,        // Input too short to be valid
    Invalid,         // Doesn't match expected format
    Valid            // Looks like a valid URL or report code
};

/**
 * Modal dialog for entering WarcraftLogs URL with optional authentication.
 *
 * Features:
 * - Large URL input field for report URL or code
 * - "Authorize Private Access" button for PKCE OAuth flow
 * - Token persistence to file for future sessions
 * - Authentication status display
 */
class UIWarcraftLogsUrlDialog {
public:
    /**
     * Result returned when dialog closes.
     */
    struct Result {
        bool submitted = false;      // User clicked Load Report
        bool cancelled = false;      // User clicked Cancel or closed dialog
        std::string url;             // The entered URL/report code
        bool useUserAuth = false;    // Whether to use user authentication
        std::string accessToken;     // Access token if authenticated
    };

    UIWarcraftLogsUrlDialog();
    ~UIWarcraftLogsUrlDialog();

    // Non-copyable
    UIWarcraftLogsUrlDialog(const UIWarcraftLogsUrlDialog&) = delete;
    UIWarcraftLogsUrlDialog& operator=(const UIWarcraftLogsUrlDialog&) = delete;

    /**
     * Open the dialog.
     */
    void open();

    /**
     * Close the dialog without submitting.
     */
    void close();

    /**
     * Render the dialog. Call each frame.
     * @return true when dialog has closed (check getResult() for outcome)
     */
    bool render();

    /**
     * Check if dialog is currently open.
     */
    [[nodiscard]] bool isOpen() const noexcept { return isOpen_; }

    /**
     * Get the result after dialog closes.
     */
    [[nodiscard]] const Result& getResult() const noexcept { return result_; }

    /**
     * Reset the dialog state for reuse.
     */
    void reset();

    /**
     * Load persisted token from file if available.
     * Called automatically on construction.
     */
    void loadPersistedToken();

    /**
     * Check if currently authenticated with a valid token.
     */
    [[nodiscard]] bool isAuthenticated() const noexcept { return isAuthenticated_; }

    /**
     * Mark the current token as invalid due to a query failure.
     * This will show the reauthorize button next time the dialog opens.
     */
    void markTokenInvalid(const std::string& errorMessage);

    /**
     * Static version that persists an auth error without needing a dialog instance.
     * Call this when a WCL query fails due to authorization.
     */
    static void persistAuthError(const std::string& errorMessage);

    /**
     * Clear any persisted auth error (e.g., after successful query).
     */
    static void clearPersistedAuthError();

private:
    // Dialog state
    bool isOpen_ = false;
    bool shouldClose_ = false;
    char urlBuffer_[512] = {0};

    // URL validation state
    UrlValidationStatus urlValidationStatus_ = UrlValidationStatus::Empty;
    std::string urlValidationMessage_;
    std::string extractedReportCode_;

    // Authentication state
    bool isAuthenticated_ = false;
    bool isAuthenticating_ = false;
    std::string authStatusMessage_;
    std::string accessToken_;
    std::chrono::system_clock::time_point tokenExpiry_;

    // Sign-in in flight on a worker thread. The browser round trip can take
    // up to two minutes, so it must not run on the UI thread. Shared with
    // the worker so the dialog can go away while it is still waiting.
    struct AuthTask;
    std::shared_ptr<AuthTask> authTask_;

    // Result
    Result result_;

    // Rendering helpers
    void renderUrlSection();
    void renderAuthSection();
    void renderButtons();

    // URL validation helpers
    void validateUrl();
    static UrlValidationStatus checkUrlFormat(const std::string& input,
                                               std::string& outReportCode,
                                               std::string& outMessage);

    // Authentication helpers
    void startAuthentication();
    void pollAuthentication();
    void savePersistedToken();
    void clearPersistedToken();
};
