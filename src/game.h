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

// Implemented by each frontend: terminal uses highscore.txt, web uses
// localStorage; a browser tab can't quit, so the menu hides the option.
long platformLoadHighScore();
void platformSaveHighScore(long hs);
bool platformCanQuit();

// startingScore > 0 is cheat mode (e.g. for screenshots): the run begins
// with that many points and never touches the high score.
void gameInit(long startingScore = 0);

// Feed one raw key: 'a'..'z', ' ', 27 = Esc, 8/127 = Backspace, 10/13 = Enter.
void gameKey(int ch);

// Advance the simulation by dt seconds and draw the frame into s.
// Returns false once the player has quit (terminal frontend exits then).
bool gameFrame(double dt, Screen &s);

// One-line result summary for the terminal frontend's goodbye message.
std::string gameGoodbye();

}  // namespace ktd
