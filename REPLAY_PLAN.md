# Verified scores via deterministic replay

Plan to make the hall of fame trustworthy, and to get watchable replays of
the top runs out of the same machinery.

Status: proposed, not started. Written 2026-07-14 after `mrrobot`/123123
(≈560 pts/sec, versus ≈130 for the best real run) was posted to the board
with a `curl`.

---

## 1. The problem, stated precisely

`POST /api/score` takes a number the *client* computed and stores it. The
browser build calculates the score in WASM and `fetch`es it up; nothing on
the server can distinguish a played run from a typed-in integer.

No secret fixes this. Any key the browser holds, the browser leaks — baked
into the `.wasm`, injected into `index.html`, or fetched from an endpoint,
it ships to every player and comes back out with `strings`. The image is
public too, but that's not even the easy route; the `.wasm` is served to
anyone who loads the page.

The only real fix is to stop trusting the client's number. **The server has
to compute the score itself.**

Note the SSH path already has this property and does not need fixing: the
game binary runs on our server (`server/ssh/main.go` spawns it on a PTY),
and the player can only send keystrokes. SSH scores are trustworthy today.

## 2. The approach

The browser keeps playing locally — instant, offline-capable, no per-key
network round-trip, which is the whole feel of a typing game. But at game
over it submits **the keystroke log instead of a score**. The server re-runs
the identical simulation from the identical seed and computes the score
itself. The client's claimed number is never stored; it isn't even read
(except in shadow mode, §9, to detect divergence).

This works because the simulation is *already* a pure function of
`(seed, grid size, keystrokes, tick count)`. There is no wall-clock read
anywhere in `game.cpp` — `g.elapsed` is just an accumulation of the `dt`
the frontend hands in, and every random draw comes from one global `rng`.

## 3. What it does and does not buy

**Does:** a stored score was genuinely produced by our game logic from a
real sequence of keystrokes. Forgery by arithmetic is dead. Duration can't
be inflated. The RNG seed can't be shopped for.

**Does not:** prove a *human* typed it. Someone can still write a bot that
plays well and emits a valid input log. That is the irreducible ceiling for
any game where the player controls the input, and it is a completely
different class of problem from "type a number into curl". If it ever
becomes real, keystroke-interval plausibility checks raise the bar further.
Out of scope for now.

## 4. Immediate stopgap — independent of everything below

Do these now; they're an hour and don't depend on the replay work.

- [ ] **Delete the forged row.** On app-server-2:
      `docker exec keyboardtd python3 -c "import sqlite3;c=sqlite3.connect('/data/scores.db');c.execute(\"DELETE FROM scores WHERE nick='mrrobot' AND score=123123\");c.commit()"`
- [ ] **Cap points/second.** `server/leaderboard.py:79` currently allows
      `score <= duration * 1000` — at level 12 that's 220,000, which is how
      123123 got in. Replace with a `KTD_MAX_PPS` cap (start at 400; best
      real run is ~130, and the theoretical max-combo ceiling is well above
      130, so leave headroom). Cap `duration` at 7200s to match the SSH
      session cap, not 86400.
      - **Do not** try to bound score by WPM. Gun turrets, nukes, and quakes
        all pay points for zero keystrokes (`game.cpp:1075`, `:1088`,
        `:1026`), so a slow tower build legitimately outscores a fast
        typist — `alonzof1` has 39,025 at 15 WPM on the board right now. A
        WPM-derived bound would delete real scores.
- [ ] **Fix the `X-Real-IP` passthrough.** `server/nginx.conf:9` forwards
      the *client's own* `X-Real-IP` header (`$http_x_real_ip`), and
      `leaderboard.py:108` trusts it for rate limiting. This is only safe
      because the outer nginx-proxy happens to overwrite it. Derive it from
      `$remote_addr` at the container edge so the rate limit can't be
      defeated by rotating a header.
- [ ] **Admin delete.** `DELETE /api/score/<id>` behind a `KTD_ADMIN_TOKEN`
      env var, so the next cleanup isn't a hand-written sqlite incantation.

## 5. Phase 1 — Make the simulation bit-exact (the hard part)

This is the load-bearing work. Everything else is plumbing.

The browser build compiles under Emscripten (**libc++**, musl libm). The
server verifier compiles under Alpine g++ (**libstdc++**, musl libm), for
**both amd64 and arm64**. Same `mt19937` state must yield the same game.

`std::mt19937` itself is fine — the standard pins its output exactly. The
things layered on top of it are not.

### 5a. Replace every implementation-defined RNG consumer

These produce **different sequences from identical engine state** across
libc++ and libstdc++. This alone guarantees divergence and must be fixed
first.

| Site | Current | Replace with |
|---|---|---|
| `game.cpp:354` | `std::uniform_real_distribution` | `(rng() >> 11) * 0x1.0p-53` |
| `game.cpp:546`, `:596`, `:633`, `:1041` | `std::uniform_int_distribution` | hand-rolled `randInt(lo,hi)`, explicit rejection sampling |
| `game.cpp:527`, `:668` | `std::shuffle` | explicit Fisher-Yates over `randInt` |

Small, self-contained, and it makes the game's RNG portable in general —
worth doing on its own merits.

### 5b. Remove libm from the simulation

Last-bit differences here compound into a diverged run.

- `std::pow` at `game.cpp:361`, `:435`, `:439` — **all three have integer
  exponents**. Replace with a multiply loop. Exact, identical everywhere.
- `std::hypot` at `game.cpp:922`, `:985` — replace with
  `std::sqrt(dx*dx + dy*dy)`. `sqrt` is IEEE-754 correctly-rounded and
  therefore bit-identical on every target; `hypot` is not. (The overflow
  behaviour `hypot` protects against is irrelevant at these magnitudes.)

Leave drawing code alone: `std::cos`/`sin`/`lround` at `game.cpp:1400`+ are
render-only and never feed simulation state.

`std::sort` at `game.cpp:1036` is safe as-is — it sorts `pair<double,int>`
where the `int` is a unique enemy id, so the comparator is a total order and
the result is implementation-independent despite introsort being unstable.

### 5c. Pin floating-point codegen

- Compile the game with **`-ffp-contract=off`** on every target, and never
  `-ffast-math`. Without this, arm64 will happily fuse `a*b+c` into an FMA
  and produce a different (more accurate!) result than amd64 or WASM.
- After 5a–5c, all remaining simulation math is `+ - * /`, `sqrt`, and
  comparisons on IEEE-754 doubles, which are exact and identical on every
  target.

### 5d. Fixed timestep — in BOTH frontends

Both frontends feed wall-clock `dt` straight into `gameFrame`:
`platform_web.cpp:174` and `platform_ncurses.cpp:204`. Replace with an
accumulator driving fixed 1/60s ticks, in both — the SSH runs are the
*trusted* ones and their replays are the ones most worth watching, so the
terminal build records logs too. Keystrokes are then recorded as
`(tick_index, key)`.

Cap the accumulator's catch-up (e.g. max 6 ticks per frame, drop the rest).
Browsers throttle hidden tabs hard; without the cap, returning to the tab
fast-forwards the game through thousands of ticks in one frame. The
existing `dt = std::min(dt, 0.1)` clamp at `game.cpp:1735` is this same
idea and becomes dead once dt is constant.

This also makes the game frame-rate independent, which is a genuine bug fix:
today a 144Hz monitor and a 60Hz monitor are running measurably different
games.

### 5e. Seed the RNG explicitly — per RUN, not per boot

Today: `std::mt19937 rng(std::random_device{}())` at `game.cpp:352` — the
only nondeterminism in the entire simulation. But note `resetGame`
(`game.cpp:343`) does NOT touch the rng: the play-again path continues the
same RNG stream. So seeding is per-run plumbing: `resetGame` takes a seed
and reseeds `rng` at every run start (first run and every restart alike),
and the frontend supplies a fresh server-issued seed each time — otherwise
run #2 of a session replays run #1's seed and dies on the single-use check
in §7.

### 5f. Pin the grid

The sim reads `g.viewRows`/`g.viewCols` (set at `game.cpp:1734`, consumed by
quake targeting at `:1029` and `:1172`), so grid size is part of the run.
Freeze it at run start and ignore mid-run resizes, or the browser window
becomes a gameplay input we'd have to log.

### 5g. Prove it — the differential test (do not skip)

A CI job that runs the same `(seed, grid, log)` through **the WASM build
under node** and **the native verifier**, and asserts the final game state
is identical field-for-field. Run it over a corpus of recorded runs plus
fuzzed random logs.

This test is what actually guarantees the property. If it ever goes red,
the fallback is to ship the verifier *as WASM* and run it server-side under
`wasmtime` — literally the same binary as the browser, which sidesteps all
of §5a–5c at the cost of a runtime dependency in the image. Keep that in the
back pocket.

## 6. Phase 2 — The run log

Recorded in `game.cpp` (shared, so both frontends get it for free) between
`welcomeCommit("start")` and `g.over`.

Note the nick is typed on the *game-over* screen (`game.cpp:1707`), so those
keystrokes fall outside the run and must not be recorded.

```
{
  "v": 1,
  "seed": "<server-issued, signed>",
  "rows": 28, "cols": 100,
  "ticks": 21600,                 // total fixed ticks the run lasted
  "keys": [[12,115],[19,116],...] // (tick, keycode), delta-encoded
}
```

Size: a 6-minute run is ~1,500 keystrokes — a few KB raw, ~1-2 KB gzipped.
Raise the 1024-byte body cap at `leaderboard.py:132` accordingly.

Draft pauses need no special handling: they already freeze the sim while
ticks continue (`game.cpp:936`), and that logic is deterministic, so the tick
stream reproduces them exactly. It does mean `ticks` ≠ `elapsed`, which is
fine — **the server derives `elapsed`, `wpm`, and `level` from the replay**
rather than trusting any of them.

Log lifecycle is per run: recording starts when a run starts (first run or
the play-again Enter at `game.cpp:1718`), ends at `g.over`, and each run
gets its own log AND its own seed (§5e). One page session can produce many
runs; each is an independent submission.

Cheat mode (`gameInit(startingScore)`) already refuses to touch the
leaderboard; keep it that way and don't record logs for cheat runs.

## 7. Phase 3 — Server-issued seeds

`GET /api/seed` → `{seed, issued_at, sig}`, HMAC'd with a server-side key.

Three properties, all necessary:

1. **Authentic** — the sig proves we issued it, so a player can't pick a
   seed they've pre-explored offline for a favourable spawn sequence.
2. **Time-bound, from BOTH sides** —
   - lower: `submitted_at - issued_at ≥ elapsed`. You can't claim a
     6-minute run that finished 2 minutes after the seed was issued.
   - upper: `submitted_at - issued_at ≤ elapsed + slack` (slack ~10 min).
     Without this the whole scheme leaks: grind the seed offline for three
     hours, then submit one perfect 6-minute run — the lower bound alone
     happily passes it. The upper bound caps pre-exploration at `slack`,
     which is not enough time to learn a spawn sequence and exploit it.
3. **Single-use** — `UNIQUE` index on `seed` in the scores table. Kills
   double-submission of the same log outright. (Note this alone does NOT
   prevent grinding — retries before the one submission never hit the
   server. That's what the upper time bound is for.)

The upper bound dictates the client-side seed lifecycle: a seed must be
fetched *near run start*, not at page load (a player can idle on the
welcome screen indefinitely). Concretely: prefetch a seed on page load and
re-fetch whenever the held seed is older than ~5 min while on the welcome /
game-over screens; apply it in `resetGame` (§5e). Every run consumes a
fresh seed — including play-again restarts. The fetch is async and cheap;
rate-limit `/api/seed` per IP like everything else.

The SSH path doesn't need issued seeds (the server already controls that
process); it can generate its own seed and submit it alongside the log,
authenticated by a key that never leaves the container.

## 8. Phase 4 — The verifier

`src/platform_replay.cpp`: a headless frontend that feeds the log through
`gameFrame` at fixed dt with no rendering, and prints the final
`totalScore / wpm / level / duration` as JSON.

`leaderboard.py` shells out to it with a hard timeout and a memory cap, and
stores **what the verifier returns**, ignoring the client entirely. A
6-minute run is ~21,600 ticks of pure simulation — milliseconds of CPU. The
existing per-IP rate limit already bounds how often this can be triggered.

Reject: bad signature on the seed, reused seed, log longer than the
time-bound allows, verifier timeout, verifier crash.

## 9. Phase 5 — Rollout (shadow mode first)

Do **not** flip to server-computed scores on day one. If determinism is
subtly broken, real players silently get wrong scores.

1. **Shadow.** Clients submit log *and* claimed score. Server stores the
   claimed score (with §4's bounds still enforced), replays in the
   background, and logs any divergence.
2. **Watch** until divergence across real traffic is zero. This is the
   empirical proof that §5 actually worked, on real browsers and real
   hardware, which no test suite fully substitutes for.
3. **Enforce.** Flip `KTD_ENFORCE_REPLAY=1`: store the server-computed
   score, reject submissions without a valid log.
4. **Legacy clients.** Browsers hold cached `.wasm` for a while, so old
   unsigned submissions keep arriving after deploy. Keep accepting them
   (bounds-checked) through the shadow period, then reject.

## 10. Phase 6 — The payoff: watchable replays

Once verified logs are stored, this is nearly free — we already have a
deterministic simulator and an ANSI renderer. And because §5d puts log
recording in both frontends, the trusted SSH runs (usually the best ones)
are watchable too, not just web runs.

- Store the log next to the score (BLOB in a `replays` table, or a file
  under `/data/replays/<id>`). Keep them for the top N plus recent runs so
  storage stays bounded; a few KB each means this is a non-issue.
- `GET /api/replay/<id>` → `{v, seed, rows, cols, ticks, keys}` plus
  display metadata: nick, final score, sim version, recorded-at.
- **Web:** a watch mode that runs the existing sim from the log, feeding
  keys at their tick indices and rendering to the xterm.js instance already
  on the page. Real-time, plus fast-forward. Pick a run from the hall of
  fame and watch it.
- **SSH:** a `watch` entry on the welcome menu — same sim, same renderer,
  straight to the PTY. Costs almost nothing given the above.
- **Grid mismatch:** a replay must be simulated at its recorded rows×cols —
  grid size is a gameplay input (§5f), so it can't be re-flowed. If the
  viewer's terminal is at least that big, letterbox; if smaller, say
  "resize to at least 100×28 to watch this replay" rather than rendering a
  cropped, misleading view. Web can also just scale the xterm.js font down
  to fit, which is the nicer default there.

This is the strongest argument for Option A over "run the game server-side
and stream it": the anti-cheat artifact and the replay artifact are the same
object, so verification pays for a feature instead of just taxing us.

## 11. Risks

- **Cross-platform float divergence** — the whole plan rests on §5. Mitigated
  by §5g's differential test; escape hatch is the WASM verifier under
  wasmtime.
- **Determinism regressions later.** Any future `game.cpp` change that adds a
  `pow`, a `std::` distribution, or a wall-clock read silently breaks
  verification. The CI differential test is what catches this; it must be a
  required check.
- **Replay-format versioning.** Once logs are stored, a gameplay balance
  change invalidates every stored replay — the same log through new rules
  yields a different score. Version the sim (`"v"` in the payload), keep
  stored replays pinned to the version they were recorded under, and either
  keep old sim versions around for playback or accept that a balance patch
  retires the old board. **Decide this before storing the first log.**
- **Bots** — see §3. Accepted, out of scope.

## 12. Open questions

- Does a balance patch reset the hall of fame? (See §11 — this is a product
  decision, and it's the one thing here that can't be deferred.)
- Do we keep web submissions during the shadow period, or go SSH-only
  temporarily to guarantee a clean board while this is built?
- Replay retention: top 10 only, or top 10 + last N runs?

## 13. Rough sequencing

| Phase | Work | Est. |
|---|---|---|
| 0 | §4 stopgap: delete row, bounds, nginx, admin delete | ~1h |
| 1 | §5 determinism: RNG, libm, fp-contract, fixed timestep, seed, grid | ~3-4h |
| 1b | §5g differential test in CI | ~2h |
| 2 | §6 run log recording | ~1-2h |
| 3 | §7 seed issuance | ~1-2h |
| 4 | §8 verifier binary + API wiring | ~2-3h |
| 5 | §9 shadow rollout, then enforce | ~1h + soak |
| 6 | §10 replay viewing (web, then SSH) | ~3-4h |

Phases 0 and 1 are worth doing regardless of whether the rest ships — the
stopgap bounds the current damage, and the determinism work fixes a real
frame-rate-dependence bug and makes the RNG portable.
