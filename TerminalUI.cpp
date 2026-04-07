#include "TerminalUI.h"

#include <algorithm>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <io.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace pcm {

TermSize getTermSize() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        const int rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        const int cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        return {rows, cols};
    }
    return {24, 80};
#else
    struct winsize w {};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        return {static_cast<int>(w.ws_row), static_cast<int>(w.ws_col)};
    }
    return {24, 80};
#endif
}

TerminalSession::TerminalSession() {
    enableRawMode();
    enterAltScreen();
}

TerminalSession::~TerminalSession() {
    leaveAltScreen();
    disableRawMode();
    std::cout << "\033[?25h\033[0m" << std::flush;
}

void TerminalSession::enableRawMode() {
#ifdef _WIN32
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hIn == INVALID_HANDLE_VALUE || hOut == INVALID_HANDLE_VALUE) {
        return;
    }

    if (!GetConsoleMode(hIn, &origInMode_) || !GetConsoleMode(hOut, &origOutMode_)) {
        return;
    }

    DWORD inMode = origInMode_;
    inMode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
    inMode |= ENABLE_VIRTUAL_TERMINAL_INPUT;

    DWORD outMode = origOutMode_;
    outMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;

    if (SetConsoleMode(hIn, inMode) && SetConsoleMode(hOut, outMode)) {
        rawModeEnabled_ = true;
    }
#else
    if (tcgetattr(STDIN_FILENO, &origTermios_) != 0) {
        return;
    }

    struct termios raw = origTermios_;
    raw.c_lflag &= static_cast<unsigned long>(~(ICANON | ECHO));
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0) {
        rawModeEnabled_ = true;
    }
#endif
}

void TerminalSession::disableRawMode() {
#ifdef _WIN32
    if (rawModeEnabled_) {
        SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), origInMode_);
        SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), origOutMode_);
        rawModeEnabled_ = false;
    }
#else
    if (rawModeEnabled_) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &origTermios_);
        rawModeEnabled_ = false;
    }
#endif
}

void TerminalSession::enterAltScreen() const {
#ifdef _WIN32
    const int n = _write(_fileno(stdout), "\033[?1049h", 8);
    (void)n;
#else
    const ssize_t n = write(STDOUT_FILENO, "\033[?1049h", 8);
    (void)n;
#endif
}

void TerminalSession::leaveAltScreen() const {
#ifdef _WIN32
    const int n = _write(_fileno(stdout), "\033[?1049l", 8);
    (void)n;
#else
    const ssize_t n = write(STDOUT_FILENO, "\033[?1049l", 8);
    (void)n;
#endif
}

void Viewport::setFrame(const std::vector<std::string>& lines) {
    frameLines_ = lines;
}

int Viewport::totalLines() const {
    return static_cast<int>(frameLines_.size());
}

void Viewport::render(int scrollOffset) {
    const TermSize ts = getTermSize();

    int viewHeight = ts.rows - 2;
    if (viewHeight < 1) {
        viewHeight = 1;
    }

    const int maxScroll = std::max(0, static_cast<int>(frameLines_.size()) - viewHeight);
    scrollOffset = std::clamp(scrollOffset, 0, maxScroll);

    std::ostringstream out;
    out << "\033[?25l";
    out << "\033[H";

    int linesRendered = 0;
    for (int i = 0; i < viewHeight; ++i) {
        const int lineIdx = scrollOffset + i;
        out << "\033[" << (i + 1) << ";1H";
        if (lineIdx < static_cast<int>(frameLines_.size())) {
            out << frameLines_[lineIdx];
        }
        out << "\033[K";
        ++linesRendered;
    }

    for (int i = linesRendered; i < prevRenderedLines_; ++i) {
        out << "\033[" << (i + 1) << ";1H\033[K";
    }
    prevRenderedLines_ = linesRendered;

    const int scrollBarRow = ts.rows - 1;
    out << "\033[" << scrollBarRow << ";1H";

    int barWidth = ts.cols - 30;
    if (barWidth < 10) {
        barWidth = 10;
    }

    const double scrollFraction = (maxScroll > 0) ? static_cast<double>(scrollOffset) / maxScroll : 0.0;
    int thumbPos = static_cast<int>(scrollFraction * (barWidth - 1));
    thumbPos = std::clamp(thumbPos, 0, barWidth - 1);

    out << Color::DIM << "  ";
    if (maxScroll > 0) {
        out << Color::CYAN;
        for (int i = 0; i < barWidth; ++i) {
            if (i == thumbPos) {
                out << Color::BOLD << "\u2588" << Color::RESET << Color::DIM << Color::CYAN;
            } else {
                out << "\u2500";
            }
        }
        out << Color::RESET << Color::DIM;
        out << "  " << (scrollOffset + 1) << "-" << std::min(scrollOffset + viewHeight, static_cast<int>(frameLines_.size()))
            << "/" << frameLines_.size();
    } else {
        out << Color::GREEN << "All content visible" << Color::RESET << Color::DIM;
    }
    out << "\033[K" << Color::RESET;

    out << "\033[" << ts.rows << ";1H";
    out << Color::INVERSE << " \u2191\u2193 Scroll  PgUp/PgDn  Home/End  q Quit " << Color::RESET << "\033[K";

    const std::string frame = out.str();
#ifdef _WIN32
    const int n = _write(_fileno(stdout), frame.c_str(), static_cast<unsigned int>(frame.size()));
#else
    const ssize_t n = write(STDOUT_FILENO, frame.c_str(), frame.size());
#endif
    (void)n;
}

}  // namespace pcm
