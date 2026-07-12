// keyboardTD — terminal frontend: blits the game's Screen grid via curses.
// On macOS/Linux that's ncurses; on Windows it's PDCursesMod (same API, no
// terminfo dependency, so the .exe runs standalone in any console).

#ifdef _WIN32
#include <curses.h>
#else
#include <ncurses.h>
#endif

#ifndef _WIN32
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
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

const char *kThemeFile = "theme.txt";

std::string platformLoadThemeName() {
    std::ifstream f(kThemeFile);
    std::string name;
    std::getline(f, name);
    return name;
}

void platformSaveThemeName(const std::string &name) {
    std::ofstream f(kThemeFile);
    f << name << "\n";
}

// Recolor the terminal's palette in place. Only works where the terminal
// lets curses redefine colors (can_change_color); elsewhere the picker is
// a no-op and the terminal keeps its native scheme — the web build is the
// primary audience for themes. Custom indices 16/17 carry the theme's
// bg/fg so assume_default_colors can restyle everything drawn in
// C_DEFAULT, without clobbering the standard eight.
void platformApplyTheme(const Theme &t) {
    if (!has_colors() || !can_change_color()) return;
    auto set = [](short idx, const char *hex) {
        int r, g, b;
        if (std::sscanf(hex, "#%2x%2x%2x", &r, &g, &b) != 3) return;
        init_color(idx, static_cast<short>(r * 1000 / 255),
                   static_cast<short>(g * 1000 / 255),
                   static_cast<short>(b * 1000 / 255));
    };
    for (short i = 0; i < 8; ++i) set(i, t.ansi[i]);
    if (COLORS > 17) {
        set(16, t.bg);
        set(17, t.fg);
        assume_default_colors(17, 16);
    }
}

// Online hall of fame. A plain terminal build has none — but under the SSH
// wrapper (server/ssh) the game joins the same board as the web build. The
// wrapper sets KTD_LEADERBOARD=1 and passes two pipes: fd 3 carries line
// commands from the game ("FETCH", "SUBMIT <nick> <score> <wpm> <level>
// <duration>"), fd 4 brings back the board as "nick score" lines terminated
// by a lone "." line. The wrapper does the actual HTTP; the game never
// touches the network.
#ifndef _WIN32

namespace {
constexpr int kLbCmdFd = 3;   // game -> wrapper
constexpr int kLbDataFd = 4;  // wrapper -> game
bool sLbEnabled = false;
std::string sLbBuf;
}  // namespace

void lbInit() {
    if (!std::getenv("KTD_LEADERBOARD")) return;
    if (fcntl(kLbCmdFd, F_GETFD) == -1 || fcntl(kLbDataFd, F_GETFD) == -1)
        return;
    fcntl(kLbDataFd, F_SETFL, fcntl(kLbDataFd, F_GETFL) | O_NONBLOCK);
    signal(SIGPIPE, SIG_IGN);  // a dead wrapper must not kill the game
    sLbEnabled = true;
}

namespace {
void lbSend(const char *line) {
    if (!sLbEnabled) return;
    std::string msg = std::string(line) + "\n";
    if (write(kLbCmdFd, msg.data(), msg.size()) < 0) sLbEnabled = false;
}
}  // namespace

// Drain fd 4 and hand each "."-terminated block to gameSetScores.
void lbPoll() {
    if (!sLbEnabled) return;
    char buf[1024];
    ssize_t n;
    while ((n = read(kLbDataFd, buf, sizeof buf)) > 0)
        sLbBuf.append(buf, static_cast<size_t>(n));
    for (;;) {
        size_t end;
        if (sLbBuf.compare(0, 2, ".\n") == 0) {
            end = 0;
        } else {
            size_t p = sLbBuf.find("\n.\n");
            if (p == std::string::npos) break;
            end = p + 1;
        }
        gameSetScores(sLbBuf.substr(0, end));
        sLbBuf.erase(0, end + 2);
    }
}

bool platformHasLeaderboard() { return sLbEnabled; }
void platformFetchScores() { lbSend("FETCH"); }
void platformSubmitScore(const std::string &nick, long score, int wpm,
                         int level, int duration) {
    char cmd[128];
    std::snprintf(cmd, sizeof cmd, "SUBMIT %s %ld %d %d %d", nick.c_str(),
                  score, wpm, level, duration);
    lbSend(cmd);
}

#else  // _WIN32: no wrapper, no board.

void lbInit() {}
void lbPoll() {}
bool platformHasLeaderboard() { return false; }
void platformFetchScores() {}
void platformSubmitScore(const std::string &, long, int, int, int) {}

#endif

}  // namespace ktd

using namespace ktd;

int main(int argc, char **argv) {
    // --cheat: start with a 100k point cushion for taking screenshots. Runs
    // started this way never touch the high score file.
    long startingScore = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--cheat") == 0) startingScore = 100000;
    }

    // Must run before gameInit: it decides platformHasLeaderboard and
    // gameInit fires the initial board fetch.
    lbInit();

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

        lbPoll();  // async hall-of-fame data from the SSH wrapper, if any

        int ch;
        while ((ch = getch()) != ERR) {
            if (ch == KEY_RESIZE) continue;
            if (ch == KEY_BACKSPACE) ch = 127;
            if (ch == KEY_ENTER) ch = '\n';
            if (ch == KEY_UP) ch = kKeyUp;
            if (ch == KEY_DOWN) ch = kKeyDown;
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
