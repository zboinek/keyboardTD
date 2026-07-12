// keyboardTD — browser frontend: the game compiles to WebAssembly and each
// frame is rendered as one ANSI string written into an xterm.js terminal
// (see web/index.html). Input arrives via web_key(), called from JS.

#include <emscripten.h>

#include <string>

#include "game.h"

namespace {
// Grid size follows the browser window (see web_resize below); these are
// just the defaults until the page reports its real dimensions.
int sRows = 28;
int sCols = 100;
}  // namespace

// clang-format off
EM_JS(void, js_term_write, (const char *p), {
    Module.term.write(UTF8ToString(p));
});
EM_JS(int, js_load_highscore, (), {
    try { return parseInt(localStorage.getItem('ktd_highscore') || '0', 10) | 0; }
    catch (e) { return 0; }
});
EM_JS(void, js_save_highscore, (int v), {
    try { localStorage.setItem('ktd_highscore', String(v)); } catch (e) {}
});
// Theme spec is one space-separated string: bg fg then the 16 ANSI colors.
// xterm.js repaints the whole grid the moment options.theme changes, which
// is what makes the picker's live preview instant.
EM_JS(void, js_apply_theme, (const char *spec), {
    const p = UTF8ToString(spec).split(' ');
    document.body.style.background = p[0];
    Module.term.options.theme = {
        background: p[0], foreground: p[1],
        black: p[2], red: p[3], green: p[4], yellow: p[5],
        blue: p[6], magenta: p[7], cyan: p[8], white: p[9],
        brightBlack: p[10], brightRed: p[11], brightGreen: p[12],
        brightYellow: p[13], brightBlue: p[14], brightMagenta: p[15],
        brightCyan: p[16], brightWhite: p[17],
    };
});
EM_JS(void, js_load_theme, (char *buf, int len), {
    let v = '';
    try { v = localStorage.getItem('ktd_theme') || ''; } catch (e) {}
    stringToUTF8(v, buf, len);
});
EM_JS(void, js_save_theme, (const char *name), {
    try { localStorage.setItem('ktd_theme', UTF8ToString(name)); }
    catch (e) {}
});
// The hall-of-fame API lives on the same origin (nginx proxies /api/ to a
// sidecar process), so plain fetch works with no CORS setup. Both calls
// are fire-and-forget; whenever a fresh top-10 arrives it's pushed back
// into the game as "nick score" lines via the exported web_scores().
EM_JS(void, js_fetch_scores, (), {
    fetch('/api/top10').then((r) => r.json()).then((list) => {
        let s = '';
        for (const e of list) s += e.nick + ' ' + e.score + '\n';
        Module.ccall('web_scores', null, ['string'], [s]);
    }).catch((e) => {});
});
EM_JS(void, js_submit_score,
      (const char *nick, int score, int wpm, int level, int duration), {
    fetch('/api/score', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({
            nick: UTF8ToString(nick), score: score, wpm: wpm,
            level: level, duration: duration
        })
    }).then((r) => r.json()).then((d) => {
        if (d && d.top10) {
            let s = '';
            for (const e of d.top10) s += e.nick + ' ' + e.score + '\n';
            Module.ccall('web_scores', null, ['string'], [s]);
        }
    }).catch((e) => {});
});
// clang-format on

extern "C" EMSCRIPTEN_KEEPALIVE void web_scores(const char *data) {
    ktd::gameSetScores(data);
}

namespace ktd {

long platformLoadHighScore() { return js_load_highscore(); }
void platformSaveHighScore(long hs) {
    js_save_highscore(static_cast<int>(hs));
}
bool platformCanQuit() { return false; }  // it's a browser tab — just close it

void platformApplyTheme(const Theme &t) {
    std::string spec = std::string(t.bg) + " " + t.fg;
    for (const char *c : t.ansi) spec += std::string(" ") + c;
    js_apply_theme(spec.c_str());
}
std::string platformLoadThemeName() {
    char buf[64];
    js_load_theme(buf, sizeof buf);
    return buf;
}
void platformSaveThemeName(const std::string &name) {
    js_save_theme(name.c_str());
}

bool platformHasLeaderboard() { return true; }
void platformFetchScores() { js_fetch_scores(); }
void platformSubmitScore(const std::string &nick, long score, int wpm,
                         int level, int duration) {
    js_submit_score(nick.c_str(), static_cast<int>(score), wpm, level,
                    duration);
}

}  // namespace ktd

using namespace ktd;

static Screen sScreen;
static std::string sFrame;
static double sLastMs = 0;

extern "C" EMSCRIPTEN_KEEPALIVE void web_key(int ch) { gameKey(ch); }

// Called from JS whenever xterm.js is re-fit to the window: the play field
// grows with the screen instead of scaling — more cells, more reaction time.
extern "C" EMSCRIPTEN_KEEPALIVE void web_resize(int rows, int cols) {
    if (rows >= 12 && cols >= 40) {
        sRows = rows;
        sCols = cols;
    }
}

static const char *sgrFor(const Cell &c) {
    switch (c.color) {
        case C_CYAN: return "36";
        case C_WHITE: return "37";
        case C_GREEN: return "32";
        case C_YELLOW: return "33";
        case C_RED: return "31";
        case C_MAGENTA: return "35";
        case C_HUD: return "30;46";
        default: return "39;49";
    }
}

// Full-frame redraw: home the cursor and repaint every cell, emitting an SGR
// escape only when the attributes change. No clear-screen, so no flicker.
static void buildFrame(const Screen &s, std::string &out) {
    out.clear();
    out += "\x1b[H";
    const Cell *prev = nullptr;
    for (int y = 0; y < s.rows(); ++y) {
        if (y > 0) out += "\r\n";
        for (int x = 0; x < s.cols(); ++x) {
            const Cell &c = s.at(y, x);
            if (!prev || prev->color != c.color || prev->bold != c.bold ||
                prev->dim != c.dim) {
                out += "\x1b[0;";
                out += sgrFor(c);
                if (c.bold) out += ";1";
                if (c.dim) out += ";2";
                out += 'm';
            }
            out += c.ch;
            prev = &c;
        }
    }
    out += "\x1b[0m";
}

static void tick() {
    double nowMs = emscripten_get_now();
    double dt = (nowMs - sLastMs) / 1000.0;
    sLastMs = nowMs;

    sScreen.resize(sRows, sCols);
    gameFrame(dt, sScreen);  // quit isn't offered on the web build
    buildFrame(sScreen, sFrame);
    js_term_write(sFrame.c_str());
}

int main() {
    gameInit();
    sLastMs = emscripten_get_now();
    js_term_write("\x1b[?25l");  // hide the cursor
    emscripten_set_main_loop(tick, 60, 1);
    return 0;
}
