# keyboardTD

A terminal typing tower defense. A tower sits in the middle of the screen;
enemies spawn on the screen border and walk straight at it, each carrying a
word. Type the word to kill the enemy before it reaches the tower — an
enemy's strength is its letter count. Spawn rate, walk speed, and word
length all ramp up over time. Your best score is saved to `highscore.txt`.

## Build & run

Terminal (needs a C++17 compiler and ncurses, both stock on macOS/Linux):

```sh
make run
```

### Browser version

The same game compiles to WebAssembly and runs client-side in an xterm.js
terminal — zero input latency, high scores in `localStorage`. The easiest
build is Docker (compiles inside the Emscripten image, serves with nginx):

```sh
docker build -t keyboardtd .
docker run --rm -p 8080:80 keyboardtd
# then open http://localhost:8080 — deploy the image anywhere to share a link
```

If you have the Emscripten SDK installed locally, `make web` builds the
static site into `dist/web/` instead; serve it with any static host
(GitHub Pages works — it's just three files).

### Releases & CI

Pushing a version tag (e.g. `git tag v1.0.0 && git push --tags`) triggers
two GitHub Actions workflows:

- **docker** — builds the web image and pushes it to GitHub Container
  Registry as `ghcr.io/<owner>/<repo>:v1.0.0` **and** `:latest`
  (amd64 + arm64). Run anywhere with
  `docker run -p 8080:80 ghcr.io/<owner>/<repo>:latest`.
- **release** — builds the terminal game for **Linux x64**, **macOS
  arm64**, and **Windows x64** and attaches the binaries to a GitHub
  Release. The Windows build uses PDCursesMod instead of ncurses, so the
  `.exe` runs standalone in any Windows console.

### Code layout

```
src/game.h               shared interface: Screen cell grid + game API
src/game.cpp             all game logic and drawing (frontend-free)
src/platform_ncurses.cpp terminal frontend
src/platform_web.cpp     browser frontend (ANSI frames into xterm.js)
web/index.html           page shell for the browser build
```

## How to play

- Type the **first letter** of an enemy's word to target it (the closest
  matching enemy is picked); its word turns yellow.
- Keep typing the rest of the word. The last letter kills it.
- A typo flashes the screen red and resets your combo (but not your typing
  progress).
- **Esc** or **Backspace** drops the current target and resets its typed
  progress — there is no panic-quit key mid-game.
- To quit, type `quit` in the command prompt (Space), or press **q** on the
  game-over screen.

### Power-ups (magenta words)

A power-up walks in from the border every ~15–30 seconds. Type its word to
trigger it before it reaches the tower (it does no damage, it just despawns
wasted):

- `freeze` — all enemies and spawning stop for 5 seconds.
- `nuke` — cuts the last 2 letters off every enemy's current word. Enemies
  left with nothing to type die (no combo growth for the collateral);
  bosses skip ahead to their next word instead.
- `heal` — restores 3 tower HP (only offered when you're damaged).

### Building (typing to manage)

Press **Space** to open the build menu — the game keeps running while it's
open, so pick your moment. The current level's options appear grayed out
above the prompt with prices on the final choices, and typing letters
filters them, so you always see what's legal next. This is a typing game,
so each word is typed **in full**: the moment it's complete it commits and
the next level's options appear — no Enter or spaces between words.
`Space` then `buildtowernorth` flows as one typed sequence.

- **Backspace** steps back (typed letters, then up a menu level).
- **Esc** closes the menu.
- The menu only offers what's currently possible: no `repair` without a
  damaged wall, no `upgrade` for unbuilt or maxed towers. Prices you can't
  afford show in red (you can still see them, just not buy).
- Letters that don't continue any option are rejected (red flash).

Buildings are paid for with score points, but your record tracks your
**peak** score, so spending never hurts your high score.

| Menu path                              | Cost           | Effect |
| -------------------------------------- | -------------- | ------ |
| `build > wall`                         | 10000          | Ring around the tower with 10 HP; absorbs any enemy that touches it (bosses cost it 3 HP). |
| `repair > wall`                        | 2500           | Restore the wall to full HP. |
| `build > tower > north/south/east/west`| 8000           | Auto-turret that fires at the nearest enemy, stripping 1 untyped letter per hit. |
| `upgrade > tower > direction`          | 6000 x level   | Strips 2 / 3 / 4 letters per hit and fires faster (max level 4). |

Turret bullets never touch power-up words, they earn 5 points per letter
stripped, and enemies whose word is fully stripped die (bosses skip to
their next word). Turrets keep firing while a `freeze` is active.

### Bosses (red, with a health bar)

Every ~30–45 seconds a slow boss spawns carrying **several words in a row**
(more at higher levels, up to 5). The `[###--]` bar above it shows words
remaining. Finish all of them to collect a 250-point bounty (times combo).
A boss that reaches the tower hits for **3 damage**, and dropping it with
Backspace resets it to its first word.

## Scoring

- Kill = `word length x 10 x combo`.
- Combo grows every 5 flawless kills (x2, x3, ...); a typo or an enemy
  reaching the tower resets it.
- The tower has 10 HP; each enemy that reaches it deals 1 damage.
