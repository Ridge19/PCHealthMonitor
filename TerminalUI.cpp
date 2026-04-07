#include "TerminalUI.h"

#include <algorithm>
#include <iostream>
#include <sstream>

#include <sys/ioctl.h>
#include <unistd.h>

namespace pcm {

TermSize getTermSize() {
    struct winsize w {};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        return {static_cast<int>(w.ws_row), static_cast<int>(w.ws_col)};
    }
    return {24, 80};
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
}

void TerminalSession::disableRawMode() {
    if (rawModeEnabled_) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &origTermios_);
        rawModeEnabled_ = false;
    }
}

void TerminalSession::enterAltScreen() const {
    const ssize_t n = write(STDOUT_FILENO, "\033[?1049h", 8);
    (void)n;
}

void TerminalSession::leaveAltScreen() const {
    const ssize_t n = write(STDOUT_FILENO, "\033[?1049l", 8);
    (void)n;
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
    const ssize_t n = write(STDOUT_FILENO, frame.c_str(), frame.size());
    (void)n;
}

}  // namespace pcm
