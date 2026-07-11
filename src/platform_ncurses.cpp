// keyboardTD — terminal frontend: blits the game's Screen grid via curses.
// On macOS/Linux that's ncurses; on Windows it's PDCursesMod (same API, no
// terminfo dependency, so the .exe runs standalone in any console).

#ifdef _WIN32
#include <curses.h>
#else
#include <ncurses.h>
#endif

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include "game.h"

namespace ktd {

const char *kHighScoreFile = "highscore.txt";

long platformLoadHighScore() {
    std::ifstream f(kHighScoreFile);
    long hs = 0;
    f >> hs;
    return hs;
}

void platformSaveHighScore(long hs) {
    std::ofstream f(kHighScoreFile);
    f << hs << "\n";
}

bool platformCanQuit() { return true; }

}  // namespace ktd

using namespace ktd;

int main(int argc, char **argv) {
    // --cheat: start with a 100k point cushion for taking screenshots. Runs
    // started this way never touch the high score file.
    long startingScore = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--cheat") == 0) startingScore = 100000;
    }

    // Esc is a gameplay key (drop target); don't sit on it waiting for an
    // escape sequence. Must be set before initscr().
#ifdef _WIN32
    _putenv_s("ESCDELAY", "25");
#else
    setenv("ESCDELAY", "25", 0);
#endif
    initscr();
    cbreak();
    noecho();
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);
    curs_set(0);
    if (has_colors()) {
        start_color();
        use_default_colors();
        // Color pair index == the ktd::Color enum value (pair 0 stays the
        // terminal default, matching C_DEFAULT).
        init_pair(C_CYAN, COLOR_CYAN, -1);
        init_pair(C_WHITE, COLOR_WHITE, -1);
        init_pair(C_GREEN, COLOR_GREEN, -1);
        init_pair(C_YELLOW, COLOR_YELLOW, -1);
        init_pair(C_RED, COLOR_RED, -1);
        init_pair(C_MAGENTA, COLOR_MAGENTA, -1);
        init_pair(C_HUD, COLOR_BLACK, COLOR_CYAN);
    }

    gameInit(startingScore);
    Screen s;
    auto last = std::chrono::steady_clock::now();
    bool running = true;

    while (running) {
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;

        int rows, cols;
        getmaxyx(stdscr, rows, cols);
        s.resize(rows, cols);

        int ch;
        while ((ch = getch()) != ERR) {
            if (ch == KEY_RESIZE) continue;
            if (ch == KEY_BACKSPACE) ch = 127;
            if (ch == KEY_ENTER) ch = '\n';
            gameKey(ch);
        }

        running = gameFrame(dt, s);

        erase();
        for (int y = 0; y < s.rows(); ++y) {
            for (int x = 0; x < s.cols(); ++x) {
                const Cell &c = s.at(y, x);
                int attr = COLOR_PAIR(c.color) | (c.bold ? A_BOLD : 0) |
                           (c.dim ? A_DIM : 0);
                attron(attr);
                mvaddch(y, x, c.ch);
                attroff(attr);
            }
        }
        refresh();
        napms(16);  // ~60 fps
    }

    endwin();
    printf("%s\n", gameGoodbye().c_str());
    return 0;
}
