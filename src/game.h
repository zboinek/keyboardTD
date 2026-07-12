// keyboardTD — shared game interface.
//
// The game logic and all drawing live in game.cpp and know nothing about
// ncurses or the browser: drawing targets the Screen cell grid below, and
// each frontend (platform_ncurses.cpp, platform_web.cpp) blits that grid
// out however it likes and feeds raw key codes back in.

#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace ktd {

// Arrow keys, used by the theme picker. Frontends map their native codes
// (curses KEY_UP, xterm escape sequences) onto these before calling gameKey.
constexpr int kKeyUp = 1000;
constexpr int kKeyDown = 1001;

enum Color : unsigned char {
    C_DEFAULT = 0,  // terminal default fg/bg
    C_CYAN,
    C_WHITE,
    C_GREEN,
    C_YELLOW,
    C_RED,
    C_MAGENTA,
    C_HUD,  // black on cyan, the HUD chip style
};

struct Cell {
    char ch = ' ';
    unsigned char color = C_DEFAULT;
    bool bold = false;
    bool dim = false;
};

class Screen {
public:
    void resize(int rows, int cols) {
        if (rows == rows_ && cols == cols_) return;
        rows_ = rows;
        cols_ = cols;
        cells_.assign(static_cast<size_t>(rows) * cols, Cell{});
    }
    void clear() { std::fill(cells_.begin(), cells_.end(), Cell{}); }
    int rows() const { return rows_; }
    int cols() const { return cols_; }
    void put(int y, int x, char ch, Color c = C_DEFAULT, bool bold = false,
             bool dim = false) {
        if (y < 0 || y >= rows_ || x < 0 || x >= cols_) return;
        cells_[static_cast<size_t>(y) * cols_ + x] = {ch, c, bold, dim};
    }
    void print(int y, int x, const std::string &s, Color c = C_DEFAULT,
               bool bold = false, bool dim = false) {
        for (size_t i = 0; i < s.size(); ++i)
            put(y, x + static_cast<int>(i), s[i], c, bold, dim);
    }
    const Cell &at(int y, int x) const {
        return cells_[static_cast<size_t>(y) * cols_ + x];
    }

private:
    int rows_ = 0, cols_ = 0;
    std::vector<Cell> cells_;
};

// A color scheme: hex "#rrggbb" strings for the background, the default
// foreground, and the 16 ANSI colors (0-7 normal, 8-15 bright). The game
// ships a fixed set (see kThemes in game.cpp); the welcome menu's `theme`
// entry picks one and the frontend applies it however it can.
struct Theme {
    const char *name;
    const char *bg;
    const char *fg;
    const char *ansi[16];
};

// Implemented by each frontend: terminal uses highscore.txt, web uses
// localStorage; a browser tab can't quit, so the menu hides the option.
long platformLoadHighScore();
void platformSaveHighScore(long hs);
bool platformCanQuit();

// Theme support. Apply repaints the frontend's palette immediately (the
// picker previews live); load/save persist the chosen theme's name next to
// the high score. A frontend that can't recolor may make apply a no-op —
// the picker still works, it just changes nothing visible.
void platformApplyTheme(const Theme &t);
std::string platformLoadThemeName();
void platformSaveThemeName(const std::string &name);

// Online hall of fame (web build only; the terminal build stubs these out).
// Fetch and submit are fire-and-forget async: results arrive later via
// gameSetScores, one "nick score" pair per line, best first.
bool platformHasLeaderboard();
void platformFetchScores();
void platformSubmitScore(const std::string &nick, long score, int wpm,
                         int level, int duration);
void gameSetScores(const std::string &data);

// startingScore > 0 is cheat mode (e.g. for screenshots): the run begins
// with that many points and never touches the high score.
void gameInit(long startingScore = 0);

// Feed one raw key: 'a'..'z', ' ', 27 = Esc, 8/127 = Backspace, 10/13 = Enter,
// plus kKeyUp/kKeyDown for the theme picker.
void gameKey(int ch);

// Advance the simulation by dt seconds and draw the frame into s.
// Returns false once the player has quit (terminal frontend exits then).
bool gameFrame(double dt, Screen &s);

// One-line result summary for the terminal frontend's goodbye message.
std::string gameGoodbye();

}  // namespace ktd
