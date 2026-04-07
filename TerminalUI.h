#pragma once

#include <string>
#include <termios.h>
#include <vector>

namespace pcm {

namespace Color {
inline const std::string RESET = "\033[0m";
inline const std::string RED = "\033[91m";
inline const std::string GREEN = "\033[92m";
inline const std::string YELLOW = "\033[93m";
inline const std::string BLUE = "\033[94m";
inline const std::string MAGENTA = "\033[95m";
inline const std::string CYAN = "\033[96m";
inline const std::string WHITE = "\033[97m";
inline const std::string BOLD = "\033[1m";
inline const std::string DIM = "\033[2m";
inline const std::string INVERSE = "\033[7m";
}  // namespace Color

struct TermSize {
    int rows = 24;
    int cols = 80;
};

TermSize getTermSize();

class TerminalSession {
public:
    TerminalSession();
    ~TerminalSession();

    TerminalSession(const TerminalSession&) = delete;
    TerminalSession& operator=(const TerminalSession&) = delete;

private:
    void enableRawMode();
    void disableRawMode();
    void enterAltScreen() const;
    void leaveAltScreen() const;

    termios origTermios_{};
    bool rawModeEnabled_ = false;
};

class Viewport {
public:
    void setFrame(const std::vector<std::string>& lines);
    int totalLines() const;
    void render(int scrollOffset);

private:
    std::vector<std::string> frameLines_;
    int prevRenderedLines_ = 0;
};

}  // namespace pcm
