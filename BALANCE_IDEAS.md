# Balance backlog

Playtest findings (2026-07): 20–25 WPM players got swarmed around 6k points
without ever building anything; 30–35 WPM players managed one turret at most.
Everyone reported "no midgame" — the game jumps from slow start to swarm.

## Root-cause math (v0.1.1 numbers)

Pressure on the player is `avg word length / spawn interval`:

| Time | Level | Avg word | Spawn interval | WPM to break even |
|------|-------|----------|----------------|-------------------|
| 0:00 | 1     | ~4.8     | 2.8s           | ~21               |
| 1:40 | 5     | ~6.4     | 2.0s           | ~38               |
| 2:40 | 9     | ~7.4     | 1.44s          | ~62               |
| 4:00 | 13    | ~7.4     | 1.03s          | ~86               |

Meanwhile income is ~10 pts/letter × combo (25 WPM ≈ 1,250/min at x1) versus
a 8,000 turret / 10,000 wall — priced past most players' life expectancy.
Combo reset on any typo makes income roughly quadratic in skill.

## Done in v0.2.0

- **Flatten the ramp** (root cause): spawn-interval decay 0.92 → 0.95 per
  level; word-length ramp halved (long share 0.04 → 0.02/lvl, medium
  0.05 → 0.025/lvl). Re-playtest before touching anything below — the
  economy complaints may shrink once players survive long enough to earn.

## Deferred ideas, in rough priority order

1. **Escalating turret pricing.** First turret ~3,000 (reachable in the calm
   first 90s), each subsequent one pricier, e.g. `kTurretCost * (1 + built)`.
   The first purchase teaches "spending is the game"; flat cheapness would be
   shallow, an escalating curve is progression.
2. **Reprice the wall.** 10,000 for 10 absorbed enemies ≈ 1,000 pts per
   block vs ~50–150 earned per kill — worst deal in the shop and the most
   expensive item. Try ~4,000 build / ~1,200 repair: the natural midgame
   panic purchase.
3. **Softer combo break.** On typo, drop one tier (`killStreak -= 5`, floor
   0) instead of resetting to x1. Keep the full reset when an enemy reaches
   the tower. Roughly doubles average-typist income without moving the top.
4. **Boss bounty scales with size.** Flat 250 ignores that bosses are up to
   5 words (~30 letters of committed typing). Try `150 * words.size() *
   combo`. Bosses become the midgame paycheck that funds the next turret:
   survive → cash out → build.
5. **Turrets as farms.** `kStripPoints = 5` means a L1 turret earns
   ~115 pts/min — a 70-minute payback on 8,000. Raise strip income so a
   turret pays back in ~3–4 min and "turret as investment" is real without
   any new UI. (Supersedes the dedicated farm-building idea: a farm is a
   fine strategic fork later, but adds menu words + tuning surface now.)
6. **Adaptive ramp (speculative).** `liveWpm()` is already measured — cap
   spawn pressure at a fraction of demonstrated throughput (~80% early,
   drifting past 100%) so every player gets a personalized curve and the
   swarm still eventually comes. Watch for rubber-banding feel.
