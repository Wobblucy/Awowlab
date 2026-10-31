#include "UI/AwlUI/Theme.h"

namespace awlui { namespace theme {

void CaptureFonts(ImFont* medium, ImFont* mono) {
    // Null is allowed; PushFont(nullptr) keeps the default font.
    gFontMedium = medium;
    gFontMono   = mono;
}

}}  // namespace awlui::theme
