// Follow the operating system's light/dark mode.
#pragma once

class QApplication;

namespace theme {

// Special items stand out with the same amber in both themes.
inline constexpr char SPECIAL_ACCENT[] = "#e0a100";

bool isDark();
void apply(QApplication &app);
// Apply the current scheme now and whenever the OS setting changes.
void followSystem(QApplication &app);

}  // namespace theme
