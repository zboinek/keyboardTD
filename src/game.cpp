// keyboardTD — a typing tower defense. Game logic and drawing, frontend-free.
//
// A tower sits in the middle of the screen. Enemies spawn on the screen
// border and walk straight at it, each carrying a word. Type the word to
// kill the enemy — its strength is its letter count. Spawn rate and walk
// speed ramp up over time; survive as long as you can and beat your record.
//
// Specials:
//   power-ups  magenta words (freeze / nuke / heal / earthquake) — type
//              them to trigger
//   bosses     red enemies carrying several words in a row; the bar above
//              them shows words remaining. They hit the tower for 3.
//
// Economy (Space opens the build menu; score is your spendable currency, but
// records track your cumulative TOTAL score, so spending never hurts it):
//   build   -> wall                    ring that absorbs enemies
//           -> tower -> north/south/   auto-turret; its bullets strip
//                       east/west      untyped letters off enemies
//   repair  -> wall                    restore the wall to full
//   upgrade -> tower -> direction      more letters per shot, faster fire
//
// The menu is predictive but still a typing exercise: each level's options
// are shown grayed out with prices on the final choices, letters filter
// them as you type, and each word is typed in full — it commits the moment
// it's complete, so `Space buildtowernorth` flows as one sequence.
// Only currently possible actions are offered.

#include "game.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <random>

namespace ktd {
namespace {

// Rows are roughly twice as tall as columns are wide, so all distance math
// happens in "physical" space (y doubled) to keep speeds and the spawn ring
// visually circular.
constexpr double kAspect = 2.0;

constexpr int kTowerMaxHp = 10;
constexpr double kTowerRadius = 2.5;  // physical units; enemy hits inside this
constexpr int kBossDamage = 3;
constexpr double kFreezeDuration = 5.0;
constexpr int kHealAmount = 3;
constexpr double kWpmWindow = 15.0;  // seconds of typing history for live WPM
constexpr int kRecentWordsCap = 20;  // words to avoid repeating right away

constexpr long kWallCost = 10000;
constexpr long kWallRepairCost = 2500;
constexpr int kWallMaxHp = 10;
constexpr long kTurretCost = 8000;
constexpr long kUpgradeCostPerLvl = 6000;  // upgrade = 6000 x current level
constexpr int kMaxTurretLvl = 4;
constexpr long kStripPoints = 5;  // passive income per letter a bullet strips
constexpr double kBulletSpeed = 45.0;  // physical units per second

const char *kDirNames[4] = {"north", "south", "east", "west"};

const std::vector<std::string> kShortWords = {
    "ace",  "bay",  "cat",  "dew",  "elf",  "fog",  "gem",  "hex",  "ivy",
    "jab",  "key",  "lap",  "mud",  "nap",  "oak",  "paw",  "rug",  "sky",
    "tar",  "urn",  "vow",  "wax",  "yak",  "zip",  "elm",  "fox",  "owl",
    "ram",  "ash",  "bat",  "cub",  "den",  "fin",  "hop",  "imp",  "jig",
    "keg",  "log",  "net",  "orb",  "pod",  "rat",  "sod",  "tug",  "vex",
    "web",  "yew",  "ebb",  "ale",  "bud",  "cog",  "dot",  "ewe",  "fig",
    "gum",  "hay",  "ink",  "jaw",  "kit",  "lug",  "mug",  "nag",  "oat",
    "pit",  "rye",  "sap",  "tan",  "van",  "wig",  "yam",
    "bolt", "cave", "dusk", "echo", "fern", "gale", "husk", "iron", "jolt",
    "kelp", "lava", "mist", "nova", "opal", "peak", "quip", "rune", "sage",
    "tide", "veil", "wisp", "yarn", "zeal", "jade", "onyx", "ruby", "coal",
    "gold", "sand", "silt", "clay", "wind", "rain", "snow", "dune", "reef",
    "moss", "glow", "palm", "leaf", "root", "seed", "vine", "herb", "drip",
    "gust", "mire", "peat", "cove", "isle", "reed", "weed", "moth", "wasp",
    "newt", "crow", "wolf", "bear", "lynx", "hawk", "wren", "dove", "swan",
    "mole", "toad", "frog", "worm", "slug",
};

const std::vector<std::string> kMediumWords = {
    "anchor",  "breeze",  "cinder",  "dagger",  "ember",   "falcon",
    "goblin",  "hollow",  "jungle",  "kernel",  "lantern", "marble",
    "nectar",  "orchid",  "python",  "quiver",  "raider",  "shadow",
    "temple",  "umber",   "vortex",  "walnut",  "zephyr",  "bastion",
    "citadel", "drought", "eclipse", "fortune", "granite", "harvest",
    "javelin", "kingdom", "monsoon", "nomadic", "obsidian", "phantom",
    "rampart", "serpent", "thunder", "vulture", "warden",  "wyvern",
    "armor",   "banner",  "beacon",  "bishop",  "bounty",  "bramble",
    "brigand", "canyon",  "castle",  "cavern",  "chalice", "charger",
    "chisel",  "cipher",  "cleric",  "cobalt",  "compass", "condor",
    "corsair", "cougar",  "crimson", "crystal", "dervish", "diamond",
    "dragon",  "dungeon", "dwarven", "emerald", "ferrous", "forsake",
    "fortify", "foundry", "galleon", "gambit",  "garrison", "glacier",
    "goblet",  "gorgon",  "granary", "griffin", "grotto",  "hamlet",
    "harbor",  "hazard",  "healer",  "hermit",  "hunter",  "imperil",
    "inferno", "jackal",  "jaguar",  "keeper",  "ladder",  "legion",
    "mallet",  "mammoth", "mantle",  "marrow",  "mentor",  "mirage",
    "morsel",  "muster",  "mystic",  "needle",  "oracle",  "outlaw",
    "palace",  "pauper",  "pillar",  "pirate",  "plague",  "plunder",
    "poison",  "potion",  "quarry",  "quartz",  "ranger",  "ravine",
    "reaper",  "rescue",  "ripple",  "ritual",  "rubble",  "saddle",
    "savage",  "sentry",  "shield",  "silver",  "sliver",  "sludge",
    "smithy",  "sorrow",  "specter", "sphinx",  "spider",  "statue",
    "sultan",  "summit",  "tavern",  "thicket", "thrall",  "tremor",
    "trinket", "tundra",  "tunnel",  "turret",  "tyrant",  "vandal",
    "vellum",  "venture", "verdant", "vessel",  "violet",  "warlock",
    "wither",  "wizard",  "wraith",  "zealot",
};

// NB: "earthquake" is reserved for the power-up of the same name.
const std::vector<std::string> kLongWords = {
    "avalanche", "blacksmith", "cataclysm", "dreadnought",
    "firestorm", "gargantuan", "hurricane", "juggernaut", "labyrinth",
    "maelstrom", "nightshade", "onslaught", "pestilence", "quicksilver",
    "revenant", "stronghold", "trebuchet", "underworld", "vanquisher",
    "wilderness", "executioner", "fortification", "impenetrable",
    "thunderclap", "annihilator",
    "alchemist", "apocalypse", "barricade", "battlefield", "catastrophe",
    "centurion", "conjuration", "crossroads", "demolition", "desolation",
    "devastator", "enchanted", "enchanter", "excavation", "expedition",
    "gatekeeper", "graveyard", "guillotine", "gunpowder", "harbinger",
    "leviathan", "merciless", "mountainside", "nightmare", "obliterate",
    "plunderer", "reckoning", "sacrifice", "scavenger", "shipwreck",
    "skirmisher", "stormbringer", "subjugate", "swordsman", "thunderous",
    "tormentor", "tributary", "vengeance", "warhammer", "whirlwind",
};

enum class Kind { Normal, Boss, Freeze, Nuke, Heal, Quake };

bool isPowerUp(Kind k) {
    return k == Kind::Freeze || k == Kind::Nuke || k == Kind::Heal ||
           k == Kind::Quake;
}

struct Enemy {
    double x, y;  // screen coords (columns, rows)
    int id = 0;   // stable handle for bullets (indices shift on erase)
    Kind kind = Kind::Normal;
    std::vector<std::string> words;  // bosses carry several, others one
    int wordIdx = 0;
    int progress = 0;  // letters typed into the current word
    bool forgiven = false;  // steady-hands upgrade already excused one typo
    double speed;      // physical units per second

    const std::string &word() const { return words[wordIdx]; }
    int wordsLeft() const { return static_cast<int>(words.size()) - wordIdx; }
};

struct Bullet {
    double x, y;   // screen coords
    int targetId;  // enemy it homes in on
    int dmg;       // letters stripped on hit
};

struct Turret {
    bool built = false;
    int lvl = 1;
    double cooldown = 0;
};

// ---- level-up drafts ---------------------------------------------------------
// Every 2nd level the game fully pauses and offers one of three upgrades,
// picked with the number keys — numbers can't collide with enemy words, so
// there's no misclick even mid-swarm.

enum Upg {
    U_FREEZE,   // freeze power-up lasts longer
    U_NUKE,     // nuke strips more letters
    U_QUAKE,    // earthquake crushes more enemies
    U_HP,       // tower max HP up (and heals a little)
    U_GUN,      // the center tower fires like a turret; more tiers = level up
    U_COMBO,    // combo tier needs fewer kills
    U_STEADY,   // first typo on each enemy doesn't break the combo
    U_MASONRY,  // walls cost less
    U_SCRAP,    // turret strips pay more per letter
    U_RAPID,    // turrets fire faster
    U_MAGNET,   // power-ups spawn more often
    U_BOUNTY,   // bosses pay a bigger bounty
    U_COUNT
};

struct UpgDef {
    const char *name;
    int maxTier;
};

const UpgDef kUpgDefs[U_COUNT] = {
    {"longer freeze", 4},       {"bigger nuke", 2},
    {"stronger earthquake", 4}, {"reinforced tower", 3},
    {"tower gun", 4},           {"combo endurance", 2},
    {"steady hands", 1},        {"masonry", 2},
    {"scrap value", 2},         {"rapid turrets", 2},
    {"power magnet", 2},        {"bounty hunter", 2},
};

struct Game {
    std::vector<Enemy> enemies;
    int target = -1;  // index into enemies, -1 = none
    int nextId = 0;
    int towerHp = kTowerMaxHp;
    long score = 0;      // spendable balance
    long totalScore = 0;  // cumulative points earned this run; the record
    int combo = 1;       // multiplier, grows with flawless kills
    int killStreak = 0;  // kills since last mistake
    int bestCombo = 1;
    int bestStreak = 0;
    int kills = 0;
    long lettersTyped = 0;          // correct letters, whole run
    std::deque<double> typeTimes;   // timestamps of recent correct letters
    std::deque<std::string> recentWords;  // avoids picking the same word again too soon
    double elapsed = 0;
    double spawnTimer = 0;
    double bossTimer = 40;   // first boss at 40s
    double powerTimer = 15;  // first power-up at 15s
    double freezeTimer = 0;  // >0 while the freeze power-up is active

    int wallHp = 0;  // 0 = no wall standing
    Turret turrets[4];  // N, S, E, W
    Turret towerGun;    // center tower's own gun (U_GUN draft unlock)
    std::vector<Bullet> bullets;

    int upg[U_COUNT] = {};      // draft upgrade tiers
    int towerMaxHp = kTowerMaxHp;  // grows with U_HP
    bool draftOpen = false;     // full pause; 1-3 picks an upgrade
    int draftIds[3] = {-1, -1, -1};
    int draftCount = 0;
    int prevLevel = 1;  // level seen last frame, to detect level-ups

    bool cmdMode = false;
    bool quitReq = false;              // set by the `quit` menu entry
    std::vector<std::string> cmdPath;  // committed menu tokens (build, tower)
    std::string cmdBuf;                // letters typed toward the next token
    std::string msg;  // transient feedback line
    double msgTimer = 0;

    // Last known view size, for key handling between frames.
    int viewRows = 24, viewCols = 80;

    bool over = false;
    bool shake = false;  // one-frame flash on a typo
    bool cheat = false;  // started with bonus points; never touches the high score
};

Game G;
long sHighScore = 0;
long sCheatStartScore = 0;  // >0 when launched with a cheat starting score
bool sQuit = false;
bool sWelcome = true;          // showing the welcome screen (process start only)
std::string sWelcomeBuf;       // letters typed toward a welcome menu word

// Applies the process-wide cheat starting score (if any) to a fresh game;
// shared by gameInit and the play-again path so the cheat persists across
// restarts within the same run.
void resetGame(Game &g) {
    g = Game{};
    if (sCheatStartScore > 0) {
        g.score = sCheatStartScore;
        g.totalScore = sCheatStartScore;
        g.cheat = true;
    }
}

std::mt19937 rng(std::random_device{}());

double frand() { return std::uniform_real_distribution<>(0.0, 1.0)(rng); }

int level(const Game &g) { return 1 + static_cast<int>(g.elapsed / 20.0); }

double spawnInterval(const Game &g) {
    // Gentle decay: the throughput the game demands (word length / interval)
    // should roughly double over ~10 minutes, not triple in 4.
    return std::max(0.65, 2.8 * std::pow(0.95, level(g) - 1));
}

double enemySpeed(const Game &g) {
    double base = 1.6 + 0.3 * (level(g) - 1);
    return base * (0.85 + 0.3 * frand());  // per-enemy variance
}

void earn(Game &g, long points) {
    g.score += points;
    g.totalScore += points;  // pure running sum: spending never sets it back
}

void say(Game &g, const std::string &m) {
    g.msg = m;
    g.msgTimer = 2.5;
}

// Shortest physical distance from screen center to a border; the wall and
// turret rings scale off this so layouts work on any terminal size.
double arenaRadius(int rows, int cols) {
    return std::min(cols / 2.0, rows * kAspect / 2.0);
}

double wallRadius(int rows, int cols) { return arenaRadius(rows, cols) * 0.55; }

void turretPos(int dir, int rows, int cols, double &x, double &y) {
    double off = arenaRadius(rows, cols) * 0.32;
    double cx = cols / 2.0, cy = rows / 2.0;
    switch (dir) {
        case 0: x = cx; y = cy - off / kAspect; break;  // north
        case 1: x = cx; y = cy + off / kAspect; break;  // south
        case 2: x = cx + off; y = cy; break;            // east
        default: x = cx - off; y = cy; break;           // west
    }
}

double turretInterval(int lvl) { return 2.6 - 0.25 * (lvl - 1); }

// ---- draft upgrade effects ---------------------------------------------------
// Every tunable an upgrade can touch goes through one of these, so the base
// game and the drafted build never disagree.

double freezeDuration(const Game &g) {
    return kFreezeDuration + 1.5 * g.upg[U_FREEZE];
}

int nukeCut(const Game &g) { return 2 + g.upg[U_NUKE]; }

// Tiers raise the floor as well as the ceiling — a bigger minimum is what
// makes an RNG spell feel upgraded.
void quakeRange(const Game &g, int &lo, int &hi) {
    static const int kLo[5] = {1, 2, 3, 3, 4};
    static const int kHi[5] = {5, 5, 6, 7, 8};
    lo = kLo[g.upg[U_QUAKE]];
    hi = kHi[g.upg[U_QUAKE]];
}

int comboEvery(const Game &g) { return 5 - g.upg[U_COMBO]; }  // 5, 4, 3

long wallCost(const Game &g) {
    return kWallCost * (4 - g.upg[U_MASONRY]) / 4;  // -25% per tier
}

long wallRepairCost(const Game &g) {
    return kWallRepairCost * (4 - g.upg[U_MASONRY]) / 4;
}

long stripPoints(const Game &g) {
    static const long kPts[3] = {kStripPoints, 8, 12};
    return kPts[g.upg[U_SCRAP]];
}

double turretIntervalFor(const Game &g, int lvl) {
    return turretInterval(lvl) * std::pow(0.85, g.upg[U_RAPID]);
}

double powerRespawn(const Game &g) {
    return (16 + frand() * 14) * std::pow(0.75, g.upg[U_MAGNET]);
}

long bossBounty(const Game &g) { return 250 + 125 * g.upg[U_BOUNTY]; }

// What the next tier of an upgrade will do, phrased as the state after
// picking it — shown on the draft overlay.
std::string upgDesc(const Game &g, int id) {
    char buf[64];
    int t = g.upg[id];  // current tier; the offer is tier t+1
    switch (id) {
        case U_FREEZE:
            snprintf(buf, sizeof buf, "freeze lasts %.1fs",
                     kFreezeDuration + 1.5 * (t + 1));
            break;
        case U_NUKE:
            snprintf(buf, sizeof buf, "nuke strips %d letters", 2 + t + 1);
            break;
        case U_QUAKE: {
            static const int kLo[5] = {1, 2, 3, 3, 4};
            static const int kHi[5] = {5, 5, 6, 7, 8};
            snprintf(buf, sizeof buf, "earthquake crushes %d-%d", kLo[t + 1],
                     kHi[t + 1]);
            break;
        }
        case U_HP:
            snprintf(buf, sizeof buf, "+2 max tower hp, heals 2");
            break;
        case U_GUN:
            if (t == 0)
                snprintf(buf, sizeof buf, "the tower itself opens fire");
            else
                snprintf(buf, sizeof buf, "tower gun -> L%d", t + 1);
            break;
        case U_COMBO:
            snprintf(buf, sizeof buf, "combo tier every %d kills", 5 - t - 1);
            break;
        case U_STEADY:
            snprintf(buf, sizeof buf, "first typo per enemy forgiven");
            break;
        case U_MASONRY:
            snprintf(buf, sizeof buf, "walls cost %d%% less", 25 * (t + 1));
            break;
        case U_SCRAP: {
            static const long kPts[3] = {kStripPoints, 8, 12};
            snprintf(buf, sizeof buf, "turret strips pay %ld/letter",
                     kPts[t + 1]);
            break;
        }
        case U_RAPID:
            snprintf(buf, sizeof buf, "turrets fire %d%% faster",
                     t == 0 ? 15 : 28);
            break;
        case U_MAGNET:
            snprintf(buf, sizeof buf, "power-ups appear %d%% more often",
                     t == 0 ? 33 : 78);
            break;
        case U_BOUNTY:
            snprintf(buf, sizeof buf, "boss bounty %ld", 250 + 125L * (t + 1));
            break;
        default:
            buf[0] = '\0';
    }
    return buf;
}

void applyUpgrade(Game &g, int id) {
    ++g.upg[id];
    if (id == U_HP) {
        g.towerMaxHp += 2;
        g.towerHp = std::min(g.towerMaxHp, g.towerHp + 2);
    } else if (id == U_GUN) {
        if (!g.towerGun.built) {
            g.towerGun.built = true;
            g.towerGun.lvl = 1;
        } else {
            ++g.towerGun.lvl;
        }
    }
    say(g, std::string(kUpgDefs[id].name) + "!");
}

// Deal three distinct upgrades that still have tiers left. If fewer remain,
// offer what's there; with nothing left the draft silently skips.
void openDraft(Game &g) {
    std::vector<int> pool;
    for (int i = 0; i < U_COUNT; ++i)
        if (g.upg[i] < kUpgDefs[i].maxTier) pool.push_back(i);
    std::shuffle(pool.begin(), pool.end(), rng);
    g.draftCount = std::min(3, static_cast<int>(pool.size()));
    if (g.draftCount == 0) return;
    for (int k = 0; k < g.draftCount; ++k) g.draftIds[k] = pool[k];
    g.draftOpen = true;
    // Don't fight the build menu for keys while paused.
    g.cmdMode = false;
    g.cmdPath.clear();
    g.cmdBuf.clear();
}

const std::string &pickWord(const Game &g) {
    // Higher levels mix in more medium/long words.
    double roll = frand();
    double longShare = std::min(0.35, 0.02 * level(g));
    double medShare = std::min(0.55, 0.25 + 0.025 * level(g));
    const auto &pool = roll < longShare              ? kLongWords
                       : roll < longShare + medShare ? kMediumWords
                                                     : kShortWords;
    return pool[std::uniform_int_distribution<size_t>(0, pool.size() - 1)(rng)];
}

// A word already in play must not share a first letter with a new spawn,
// otherwise the first keystroke would be ambiguous.
bool firstLetterTaken(const Game &g, char c) {
    for (const auto &e : g.enemies)
        if (e.progress == 0 && e.word()[0] == c) return true;
    return false;
}

bool recentlyUsed(const Game &g, const std::string &w) {
    return std::find(g.recentWords.begin(), g.recentWords.end(), w) !=
           g.recentWords.end();
}

// Remembers a spawned word so pickWord avoids it again until it ages out.
void markUsed(Game &g, const std::string &w) {
    g.recentWords.push_back(w);
    if (static_cast<int>(g.recentWords.size()) > kRecentWordsCap)
        g.recentWords.pop_front();
}

// Random point on the screen border.
void borderPoint(int rows, int cols, double &x, double &y) {
    int side = std::uniform_int_distribution<>(0, 3)(rng);
    switch (side) {
        case 0: x = frand() * cols; y = 1; break;           // top
        case 1: x = frand() * cols; y = rows - 2; break;    // bottom
        case 2: x = 1; y = 1 + frand() * (rows - 2); break; // left
        default: x = cols - 2; y = 1 + frand() * (rows - 2); break;
    }
}

void spawnEnemy(Game &g, int rows, int cols) {
    std::string word;
    for (int tries = 0; tries < 24; ++tries) {
        word = pickWord(g);
        if (!firstLetterTaken(g, word[0]) &&
            (tries >= 12 || !recentlyUsed(g, word)))
            break;
    }
    markUsed(g, word);
    Enemy e;
    e.id = g.nextId++;
    borderPoint(rows, cols, e.x, e.y);
    e.words = {word};
    e.speed = enemySpeed(g);
    g.enemies.push_back(std::move(e));
}

void spawnBoss(Game &g, int rows, int cols) {
    Enemy e;
    e.id = g.nextId++;
    e.kind = Kind::Boss;
    borderPoint(rows, cols, e.x, e.y);
    int count = std::min(2 + level(g) / 3, 5);
    for (int i = 0; i < count; ++i) {
        for (int tries = 0; tries < 24; ++tries) {
            const std::string &w = kMediumWords[std::uniform_int_distribution<
                size_t>(0, kMediumWords.size() - 1)(rng)];
            // First word must be unambiguous; later ones just avoid repeats.
            if (i == 0 && firstLetterTaken(g, w[0])) continue;
            if (std::find(e.words.begin(), e.words.end(), w) != e.words.end())
                continue;
            if (tries < 12 && recentlyUsed(g, w)) continue;
            e.words.push_back(w);
            markUsed(g, w);
            break;
        }
    }
    e.speed = enemySpeed(g) * 0.55;  // slow, but tanky and hits for 3
    g.enemies.push_back(std::move(e));
}

void spawnPowerUp(Game &g, int rows, int cols) {
    // Only one power-up on screen at a time.
    for (const auto &e : g.enemies)
        if (isPowerUp(e.kind)) return;

    std::vector<std::pair<Kind, std::string>> choices = {
        {Kind::Freeze, "freeze"},
        {Kind::Nuke, "nuke"},
        {Kind::Quake, "earthquake"}};
    if (g.towerHp < g.towerMaxHp) choices.push_back({Kind::Heal, "heal"});

    // Shuffle so we can fall through to another choice on a letter clash.
    std::shuffle(choices.begin(), choices.end(), rng);
    for (const auto &choice : choices) {
        const std::string &word = choice.second;
        if (firstLetterTaken(g, word[0])) continue;
        Enemy e;
        e.id = g.nextId++;
        e.kind = choice.first;
        borderPoint(rows, cols, e.x, e.y);
        e.words = {word};
        e.speed = enemySpeed(g) * 0.8;
        g.enemies.push_back(std::move(e));
        return;
    }
}

void removeEnemyAt(Game &g, size_t i) {
    if (static_cast<int>(i) == g.target) g.target = -1;
    else if (static_cast<int>(i) < g.target) --g.target;
    g.enemies.erase(g.enemies.begin() + i);
}

int enemyIndexById(const Game &g, int id) {
    for (size_t i = 0; i < g.enemies.size(); ++i)
        if (g.enemies[i].id == id) return static_cast<int>(i);
    return -1;
}

// Strip up to n untyped letters off the end of an enemy's current word.
// Words emptied this way kill the enemy (bosses skip to their next word).
// Returns true if the enemy was removed.
bool stripLetters(Game &g, size_t i, int n) {
    Enemy &e = g.enemies[i];
    std::string &w = e.words[e.wordIdx];
    int cut = std::min(n, static_cast<int>(w.size()) - e.progress);
    if (cut <= 0) return false;
    w.resize(w.size() - cut);
    earn(g, stripPoints(g) * cut);
    if (e.progress < static_cast<int>(w.size())) return false;
    if (e.kind == Kind::Boss && e.wordsLeft() > 1) {
        ++e.wordIdx;
        e.progress = 0;
        return false;
    }
    ++g.kills;
    removeEnemyAt(g, i);
    return true;
}

// ---- economy: the predictive build menu -------------------------------------

int dirIndex(const std::string &s) {
    for (int i = 0; i < 4; ++i)
        if (s == kDirNames[i]) return i;
    return -1;
}

bool spend(Game &g, long cost) {
    if (g.score < cost) {
        say(g, "need " + std::to_string(cost) + " points (have " +
               std::to_string(g.score) + ")");
        return false;
    }
    g.score -= cost;
    return true;
}

struct CmdOpt {
    std::string name;
    long price;  // shown on final choices; -1 = group, opens more choices
    bool leaf;   // committing this executes the command
};

// The menu is built from live game state: only currently possible actions
// are offered, and upgrade prices track each turret's level.
std::vector<CmdOpt> cmdOptions(const Game &g) {
    std::vector<CmdOpt> out;
    const auto &p = g.cmdPath;
    bool anyUnbuilt = false, anyUpgradable = false;
    for (const auto &t : g.turrets) {
        anyUnbuilt = anyUnbuilt || !t.built;
        anyUpgradable = anyUpgradable || (t.built && t.lvl < kMaxTurretLvl);
    }

    if (p.empty()) {
        if (g.wallHp == 0 || anyUnbuilt) out.push_back({"build", -1, false});
        if (g.wallHp > 0 && g.wallHp < kWallMaxHp)
            out.push_back({"repair", -1, false});
        if (anyUpgradable) out.push_back({"upgrade", -1, false});
        if (platformCanQuit()) out.push_back({"quit", -1, true});
    } else if (p[0] == "build" && p.size() == 1) {
        if (g.wallHp == 0) out.push_back({"wall", wallCost(g), true});
        if (anyUnbuilt) out.push_back({"tower", -1, false});
    } else if (p[0] == "build" && p[1] == "tower") {
        for (int d = 0; d < 4; ++d)
            if (!g.turrets[d].built)
                out.push_back({kDirNames[d], kTurretCost, true});
    } else if (p[0] == "repair") {
        out.push_back({"wall", wallRepairCost(g), true});
    } else if (p[0] == "upgrade" && p.size() == 1) {
        out.push_back({"tower", -1, false});
    } else if (p[0] == "upgrade" && p[1] == "tower") {
        for (int d = 0; d < 4; ++d) {
            const Turret &t = g.turrets[d];
            if (t.built && t.lvl < kMaxTurretLvl)
                out.push_back({kDirNames[d], kUpgradeCostPerLvl * t.lvl, true});
        }
    }
    return out;
}

void execCommand(Game &g) {
    const auto &p = g.cmdPath;
    if (p[0] == "quit") {
        g.quitReq = true;
    } else if (p[0] == "build" && p[1] == "wall") {
        if (!spend(g, wallCost(g))) return;
        g.wallHp = kWallMaxHp;
        say(g, "wall raised!");
    } else if (p[0] == "repair") {
        if (!spend(g, wallRepairCost(g))) return;
        g.wallHp = kWallMaxHp;
        say(g, "wall repaired!");
    } else if (p[0] == "build") {
        if (!spend(g, kTurretCost)) return;
        Turret &t = g.turrets[dirIndex(p[2])];
        t.built = true;
        t.lvl = 1;
        say(g, p[2] + " tower online!");
    } else if (p[0] == "upgrade") {
        Turret &t = g.turrets[dirIndex(p[2])];
        if (!spend(g, kUpgradeCostPerLvl * t.lvl)) return;
        ++t.lvl;
        say(g, p[2] + " tower -> L" + std::to_string(t.lvl));
    }
}

// This is a typing game, so menu words are typed IN FULL: letters filter
// the current level's options as you go (so you always see what's legal),
// and a token commits the moment its whole word is complete — no Space or
// Enter between tokens, `buildtowernorth` flows as one typed sequence.
void cmdType(Game &g, char c) {
    auto opts = cmdOptions(g);
    std::string probe = g.cmdBuf + c;
    const CmdOpt *exact = nullptr;
    bool anyPrefix = false;
    for (const auto &o : opts) {
        if (o.name == probe) exact = &o;
        else if (o.name.rfind(probe, 0) == 0) anyPrefix = true;
    }
    if (exact) {
        g.cmdPath.push_back(exact->name);
        g.cmdBuf.clear();
        if (exact->leaf) {
            execCommand(g);
            g.cmdMode = false;
            g.cmdPath.clear();
        }
        return;
    }
    if (!anyPrefix) { g.shake = true; return; }  // no option continues that way
    g.cmdBuf = probe;
}

// Backspace steps back: first the typed letters, then up a menu level, and
// from the top level it closes the menu.
void cmdBackspace(Game &g) {
    if (!g.cmdBuf.empty()) g.cmdBuf.pop_back();
    else if (!g.cmdPath.empty()) g.cmdPath.pop_back();
    else g.cmdMode = false;
}

// ---- simulation ------------------------------------------------------------

// Index of the nearest strippable (non-powerup, still has untyped letters)
// enemy to (tx,ty), or -1. Shared by turret firing and the "who would this
// turret shoot next" highlight, so both always agree.
int nearestStrippable(const Game &g, double tx, double ty) {
    int best = -1;
    double bestDist = 1e18;
    for (size_t i = 0; i < g.enemies.size(); ++i) {
        const Enemy &e = g.enemies[i];
        if (isPowerUp(e.kind)) continue;  // don't shoot the goodies
        if (static_cast<int>(e.word().size()) <= e.progress) continue;
        double dx = e.x - tx, dy = (e.y - ty) * kAspect;
        double dist = dx * dx + dy * dy;
        if (dist < bestDist) { bestDist = dist; best = static_cast<int>(i); }
    }
    return best;
}

void turretFire(Game &g, Turret &t, double tx, double ty, double dt) {
    t.cooldown -= dt;
    if (t.cooldown > 0) return;
    int best = nearestStrippable(g, tx, ty);
    if (best == -1) return;
    g.bullets.push_back({tx, ty, g.enemies[best].id, t.lvl});
    t.cooldown = turretIntervalFor(g, t.lvl);
}

void updateTurrets(Game &g, double dt, int rows, int cols) {
    for (int d = 0; d < 4; ++d) {
        Turret &t = g.turrets[d];
        if (!t.built) continue;
        double tx, ty;
        turretPos(d, rows, cols, tx, ty);
        turretFire(g, t, tx, ty, dt);
    }
    // The tower gun (draft unlock) is a fifth turret at the center.
    if (g.towerGun.built)
        turretFire(g, g.towerGun, cols / 2.0, rows / 2.0, dt);
}

// Ids of enemies at least one built turret is currently locked onto — i.e.
// would fire at right now, cooldown notwithstanding. Recomputed fresh every
// frame (not tied to bullets in flight) so the highlight stays on an enemy
// the whole time a turret is working on it, not just during the brief
// travel of each bullet.
std::vector<int> lockedTurretTargets(const Game &g, int rows, int cols) {
    std::vector<int> ids;
    for (int d = 0; d < 4; ++d) {
        if (!g.turrets[d].built) continue;
        double tx, ty;
        turretPos(d, rows, cols, tx, ty);
        int best = nearestStrippable(g, tx, ty);
        if (best != -1) ids.push_back(g.enemies[best].id);
    }
    if (g.towerGun.built) {
        int best = nearestStrippable(g, cols / 2.0, rows / 2.0);
        if (best != -1) ids.push_back(g.enemies[best].id);
    }
    return ids;
}

void updateBullets(Game &g, double dt) {
    for (size_t i = 0; i < g.bullets.size();) {
        Bullet &b = g.bullets[i];
        int ti = enemyIndexById(g, b.targetId);
        if (ti == -1) {  // target already died; bullet fizzles
            g.bullets.erase(g.bullets.begin() + i);
            continue;
        }
        const Enemy &e = g.enemies[ti];
        double dx = e.x - b.x, dy = (e.y - b.y) * kAspect;
        double dist = std::hypot(dx, dy);
        double step = kBulletSpeed * dt;
        if (dist <= std::max(step, 1.2)) {  // hit
            stripLetters(g, ti, b.dmg);
            g.bullets.erase(g.bullets.begin() + i);
            continue;
        }
        b.x += dx / dist * step;
        b.y += dy / dist * step / kAspect;
        ++i;
    }
}

void update(Game &g, double dt, int rows, int cols) {
    // A draft is a FULL pause: no movement, spawns, turrets, or clocks — so
    // even mid-swarm the player can read three options in peace.
    if (g.draftOpen) return;

    g.elapsed += dt;
    if (g.msgTimer > 0) g.msgTimer -= dt;

    // Every 2nd level, deal a level-up draft.
    int lvl = level(g);
    if (lvl != g.prevLevel) {
        g.prevLevel = lvl;
        if (lvl % 2 == 0) openDraft(g);
    }

    // Drop typing history that fell out of the live-WPM window.
    while (!g.typeTimes.empty() && g.typeTimes.front() < g.elapsed - kWpmWindow)
        g.typeTimes.pop_front();

    // Turrets keep firing while everything is frozen — free damage.
    updateTurrets(g, dt, rows, cols);
    updateBullets(g, dt);

    if (g.freezeTimer > 0) {
        // Freeze pauses movement AND all spawning — a real breather.
        g.freezeTimer -= dt;
        return;
    }

    g.spawnTimer -= dt;
    if (g.spawnTimer <= 0) {
        spawnEnemy(g, rows, cols);
        g.spawnTimer = spawnInterval(g);
    }
    g.bossTimer -= dt;
    if (g.bossTimer <= 0) {
        spawnBoss(g, rows, cols);
        g.bossTimer = std::max(25.0, 45.0 - 2.0 * level(g));
    }
    g.powerTimer -= dt;
    if (g.powerTimer <= 0) {
        spawnPowerUp(g, rows, cols);
        g.powerTimer = powerRespawn(g);
    }

    double cx = cols / 2.0, cy = rows / 2.0;
    double wallR = wallRadius(rows, cols);
    for (size_t i = 0; i < g.enemies.size();) {
        Enemy &e = g.enemies[i];
        double dx = cx - e.x, dy = (cy - e.y) * kAspect;  // physical space
        double dist = std::hypot(dx, dy);

        // The wall absorbs hostiles that touch it, 1 HP each (3 for a boss).
        // Power-ups walk right through.
        if (g.wallHp > 0 && !isPowerUp(e.kind) && dist <= wallR) {
            g.wallHp = std::max(0, g.wallHp -
                                       (e.kind == Kind::Boss ? kBossDamage : 1));
            if (g.wallHp == 0) say(g, "the wall has fallen!");
            removeEnemyAt(g, i);
            continue;
        }

        if (dist <= kTowerRadius) {
            if (!isPowerUp(e.kind)) {  // a wasted power-up just despawns
                g.towerHp -= e.kind == Kind::Boss ? kBossDamage : 1;
                g.combo = 1;
                g.killStreak = 0;
            }
            removeEnemyAt(g, i);
            if (g.towerHp <= 0) { g.over = true; return; }
            continue;
        }
        double step = e.speed * dt / dist;
        e.x += dx * step;
        e.y += dy * step / kAspect;
        ++i;
    }
}

void bumpCombo(Game &g) {
    ++g.killStreak;
    // x2 at 5 flawless kills, x3 at 10, ... (fewer with combo endurance)
    g.combo = 1 + g.killStreak / comboEvery(g);
    g.bestCombo = std::max(g.bestCombo, g.combo);
    g.bestStreak = std::max(g.bestStreak, g.killStreak);
}

// Earthquake: instantly crushes the 1-5 hostiles closest to the tower —
// closest first, so it's a true panic button rather than a dice roll on
// fresh spawns at the border. A struck boss loses its current word instead
// of dying (unless it was on its last one). Collateral pays the word's
// points but doesn't grow the combo, same as the nuke.
void quakeStrike(Game &g) {
    std::vector<int> ids;  // hostile ids, nearest to the tower first
    double cx = g.viewCols / 2.0, cy = g.viewRows / 2.0;
    std::vector<std::pair<double, int>> byDist;
    for (const auto &e : g.enemies) {
        if (isPowerUp(e.kind)) continue;
        double dx = e.x - cx, dy = (e.y - cy) * kAspect;
        byDist.push_back({dx * dx + dy * dy, e.id});
    }
    std::sort(byDist.begin(), byDist.end());
    for (const auto &p : byDist) ids.push_back(p.second);

    int lo, hi;
    quakeRange(g, lo, hi);
    int count = std::uniform_int_distribution<>(lo, hi)(rng);
    count = std::min(count, static_cast<int>(ids.size()));
    if (count == 0) {
        say(g, "the earthquake rumbles at nothing");
        return;
    }
    int crushed = 0;
    for (int k = 0; k < count; ++k) {
        int i = enemyIndexById(g, ids[k]);
        if (i == -1) continue;
        Enemy &v = g.enemies[static_cast<size_t>(i)];
        earn(g, static_cast<long>(v.word().size()) * 10);
        if (v.kind == Kind::Boss && v.wordsLeft() > 1) {
            ++v.wordIdx;  // shaken, not destroyed: loses its current word
            v.progress = 0;
        } else {
            ++g.kills;
            removeEnemyAt(g, static_cast<size_t>(i));
        }
        ++crushed;
    }
    say(g, "earthquake! " + std::to_string(crushed) + " crushed");
}

// The targeted enemy's final word was completed: remove it and apply effects.
void killTarget(Game &g) {
    Enemy e = std::move(g.enemies[g.target]);
    g.enemies.erase(g.enemies.begin() + g.target);
    g.target = -1;
    bumpCombo(g);

    switch (e.kind) {
        case Kind::Normal:
            ++g.kills;
            earn(g, static_cast<long>(e.word().size()) * 10 * g.combo);
            break;
        case Kind::Boss:
            ++g.kills;
            // Final word's points plus a flat boss bounty.
            earn(g, (static_cast<long>(e.word().size()) * 10 + bossBounty(g)) *
                        g.combo);
            break;
        case Kind::Freeze:
            g.freezeTimer = freezeDuration(g);
            break;
        case Kind::Nuke: {
            // Cut the last two untyped letters from every enemy's current
            // word. Anything left with nothing to type dies (no combo growth
            // for the collateral); a boss skips ahead to its next word.
            for (size_t i = 0; i < g.enemies.size();) {
                Enemy &v = g.enemies[i];
                if (isPowerUp(v.kind)) { ++i; continue; }
                std::string &w = v.words[v.wordIdx];
                long origLen = static_cast<long>(w.size());
                int cut = std::min(nukeCut(g),
                                   static_cast<int>(w.size()) - v.progress);
                w.resize(w.size() - cut);
                if (v.progress < static_cast<int>(w.size())) { ++i; continue; }
                if (v.kind == Kind::Boss && v.wordsLeft() > 1) {
                    ++v.wordIdx;
                    v.progress = 0;
                    ++i;
                    continue;
                }
                ++g.kills;
                earn(g, origLen * 10);
                removeEnemyAt(g, i);
            }
            break;
        }
        case Kind::Heal:
            g.towerHp = std::min(g.towerMaxHp, g.towerHp + kHealAmount);
            break;
        case Kind::Quake:
            quakeStrike(g);
            break;
    }
}

void onCorrectLetter(Game &g) {
    ++g.lettersTyped;
    g.typeTimes.push_back(g.elapsed);
}

// A full word on the target was typed; advance bosses, kill everything else.
void completeWord(Game &g) {
    Enemy &e = g.enemies[g.target];
    if (e.kind == Kind::Boss && e.wordsLeft() > 1) {
        earn(g, static_cast<long>(e.word().size()) * 10 * g.combo);
        ++e.wordIdx;
        e.progress = 0;  // next word starts fresh; boss stays targeted
        return;
    }
    killTarget(g);
}

void handleKey(Game &g, int ch) {
    // Esc drops the target too — no panic-quit key in the heat of a fight.
    if (ch == 127 || ch == 8 || ch == 27) {
        if (g.target != -1) {
            Enemy &e = g.enemies[g.target];
            e.progress = 0;
            e.wordIdx = 0;  // dropping a boss resets it to its first word
            g.target = -1;
        }
        return;
    }
    if (ch < 'a' || ch > 'z') return;
    char c = static_cast<char>(ch);

    if (g.target != -1) {
        Enemy &e = g.enemies[g.target];
        if (e.word()[e.progress] == c) {
            onCorrectLetter(g);
            if (++e.progress == static_cast<int>(e.word().size()))
                completeWord(g);
        } else if (g.upg[U_STEADY] && !e.forgiven) {
            // Steady hands: the first typo on each enemy doesn't break the
            // combo — just the flash, as a warning.
            e.forgiven = true;
            g.shake = true;
        } else {
            g.killStreak = 0;
            g.combo = 1;
            g.shake = true;
        }
        return;
    }

    // No target yet: pick the enemy closest to the tower whose word starts
    // with this letter.
    double cx = g.viewCols / 2.0, cy = g.viewRows / 2.0;
    int best = -1;
    double bestDist = 1e18;
    for (size_t i = 0; i < g.enemies.size(); ++i) {
        if (g.enemies[i].word()[0] != c) continue;
        double dx = cx - g.enemies[i].x;
        double dy = (cy - g.enemies[i].y) * kAspect;
        double d = dx * dx + dy * dy;
        if (d < bestDist) { bestDist = d; best = static_cast<int>(i); }
    }
    if (best != -1) {
        g.target = best;
        g.enemies[best].progress = 1;
        onCorrectLetter(g);
        if (g.enemies[best].word().size() == 1) completeWord(g);
    } else {
        g.killStreak = 0;
        g.combo = 1;
        g.shake = true;
    }
}

// ---- welcome screen ----------------------------------------------------------

// The welcome menu is typed like everything else: `start` begins a run,
// `quit` exits (terminal only). Enter is a shortcut for start so first-time
// players aren't stuck reading with no obvious way in.
std::vector<std::string> welcomeOptions() {
    std::vector<std::string> out = {"start"};
    if (platformCanQuit()) out.push_back("quit");
    return out;
}

void welcomeCommit(const std::string &word) {
    if (word == "start") {
        sWelcome = false;
        resetGame(G);
    } else if (word == "quit") {
        sQuit = true;
    }
}

void welcomeKey(int ch) {
    if (ch == '\n' || ch == '\r') { welcomeCommit("start"); return; }
    if (ch == 127 || ch == 8) {
        if (!sWelcomeBuf.empty()) sWelcomeBuf.pop_back();
        return;
    }
    if (ch < 'a' || ch > 'z') return;
    std::string probe = sWelcomeBuf + static_cast<char>(ch);
    bool anyPrefix = false;
    for (const auto &o : welcomeOptions()) {
        if (o == probe) { welcomeCommit(o); sWelcomeBuf.clear(); return; }
        if (o.rfind(probe, 0) == 0) anyPrefix = true;
    }
    if (anyPrefix) sWelcomeBuf = probe;
}

void drawWelcome(long highScore, Screen &s) {
    int cy = s.rows() / 2, cx = s.cols() / 2;
    char buf[96];

    const char *title = "K E Y B O A R D   T D";
    s.print(cy - 8, cx - static_cast<int>(strlen(title)) / 2, title, C_CYAN,
            true);
    const char *sub = "a typing tower defense";
    s.print(cy - 7, cx - static_cast<int>(strlen(sub)) / 2, sub, C_WHITE,
            false, true);

    const char *tips[] = {
        "type an enemy's word to kill it before it reaches your tower",
        "the first letter targets the closest match; Esc drops a target",
        "magenta words are power-ups -- type them to trigger",
        "red enemies are bosses: several words, and they hit for 3",
        "Space opens the build menu: walls and turrets, paid with points",
        "flawless kills grow your combo -- typos reset it",
    };
    for (int i = 0; i < 6; ++i)
        s.print(cy - 4 + i, cx - static_cast<int>(strlen(tips[i])) / 2,
                tips[i]);

    if (highScore > 0) {
        snprintf(buf, sizeof buf, "best score: %ld", highScore);
        s.print(cy + 3, cx - static_cast<int>(strlen(buf)) / 2, buf, C_YELLOW);
    }

    // Options row, styled like the in-game command menu: dim words, typed
    // letters lit up. Enter is offered as a beginner-friendly alternative.
    auto opts = welcomeOptions();
    int width = 0;
    for (const auto &o : opts) width += static_cast<int>(o.size()) + 3;
    int x = cx - width / 2;
    for (const auto &o : opts) {
        bool match = o.rfind(sWelcomeBuf, 0) == 0;
        for (size_t i = 0; i < o.size(); ++i) {
            bool hit = match && i < sWelcomeBuf.size();
            if (hit) s.put(cy + 5, x++, o[i], C_YELLOW, true);
            else s.put(cy + 5, x++, o[i], C_WHITE, false, true);
        }
        x += 3;
    }
    const char *hint = "type a word (or press Enter to start)";
    s.print(cy + 7, cx - static_cast<int>(strlen(hint)) / 2, hint, C_WHITE,
            false, true);
}

// Live WPM over the last kWpmWindow seconds (5 chars = 1 word).
int liveWpm(const Game &g) {
    double window = std::min(kWpmWindow, std::max(5.0, g.elapsed));
    return static_cast<int>(
        std::lround(g.typeTimes.size() / 5.0 * (60.0 / window)));
}

int overallWpm(const Game &g) {
    if (g.elapsed < 1) return 0;
    return static_cast<int>(
        std::lround(g.lettersTyped / 5.0 * (60.0 / g.elapsed)));
}

// ---- rendering -------------------------------------------------------------

void drawTower(const Game &g, Screen &s) {
    int cy = s.rows() / 2, cx = s.cols() / 2;
    Color c = g.towerHp <= g.towerMaxHp / 3 ? C_RED : C_CYAN;
    s.print(cy - 1, cx - 2, "/MMM\\", c, true);
    s.print(cy,     cx - 2, "|###|", c, true);
    s.print(cy + 1, cx - 2, "|_#_|", c, true);
}

// Score-to-spend and combo, right above the tower — the player's eyes stay
// centered during play, so this is where "can I afford it" and "do I have a
// combo running" need to be readable without glancing at the corners.
void drawCenterStats(const Game &g, Screen &s) {
    int cy = s.rows() / 2, cx = s.cols() / 2;
    char buf[32];
    snprintf(buf, sizeof buf, "%ld | x%d", g.score, g.combo);
    Color c = g.combo > 1 ? C_YELLOW : C_WHITE;
    s.print(cy - 2, cx - static_cast<int>(strlen(buf)) / 2, buf, c,
            g.combo > 1);
}

void drawWall(const Game &g, Screen &s) {
    if (g.wallHp <= 0) return;
    int rows = s.rows(), cols = s.cols();
    double cx = cols / 2.0, cy = rows / 2.0;
    double r = wallRadius(rows, cols);
    Color c = g.wallHp > kWallMaxHp * 2 / 3 ? C_GREEN
              : g.wallHp > kWallMaxHp / 3   ? C_YELLOW
                                            : C_RED;
    constexpr double kTau = 6.283185307179586;  // M_PI isn't standard C++
    for (double a = 0; a < kTau; a += 0.035) {
        int x = static_cast<int>(std::lround(cx + r * std::cos(a)));
        int y = static_cast<int>(std::lround(cy + r * std::sin(a) / kAspect));
        if (y >= 1 && y < rows - 1) s.put(y, x, '#', c);
    }
}

void drawTurrets(const Game &g, Screen &s) {
    int rows = s.rows(), cols = s.cols();
    for (int d = 0; d < 4; ++d) {
        if (!g.turrets[d].built) continue;
        double tx, ty;
        turretPos(d, rows, cols, tx, ty);
        char buf[8];
        snprintf(buf, sizeof buf, "[%c%d]", toupper(kDirNames[d][0]),
                 g.turrets[d].lvl);
        s.print(static_cast<int>(std::lround(ty)),
                static_cast<int>(std::lround(tx)) - 2, buf, C_CYAN, true);
    }
    for (const auto &b : g.bullets) {
        int y = static_cast<int>(std::lround(b.y));
        int x = static_cast<int>(std::lround(b.x));
        if (y >= 1 && y < rows - 1) s.put(y, x, '*', C_YELLOW, true);
    }
}

// turretTargeted: a turret is currently locked onto this enemy — it's the
// nearest strippable target for at least one built turret, regardless of
// whether a bullet happens to be mid-flight right now. Stays highlighted
// the whole time a turret is working on it, distinct from isTarget (the
// word the player is actually typing), so a player can tell at a glance
// "a turret's got this one" and type elsewhere.
void drawEnemy(const Enemy &e, bool isTarget, bool turretTargeted,
                bool frozen, Screen &s) {
    int y = static_cast<int>(std::lround(e.y));
    int x = static_cast<int>(std::lround(e.x)) -
            static_cast<int>(e.word().size()) / 2;
    if (y < 1 || y >= s.rows() - 1) return;

    for (size_t i = 0; i < e.word().size(); ++i) {
        bool typed = static_cast<int>(i) < e.progress;
        Color c;
        if (typed) c = C_GREEN;
        else if (isTarget) c = C_YELLOW;
        else if (isPowerUp(e.kind)) c = C_MAGENTA;
        else if (e.kind == Kind::Boss) c = C_RED;
        else if (turretTargeted || frozen) c = C_CYAN;
        else c = C_WHITE;
        bool bold = typed || isTarget || turretTargeted ||
                    e.kind != Kind::Normal;
        s.put(y, x + static_cast<int>(i), e.word()[i], c, bold);
    }

    // Boss health bar: one segment per word still to type.
    if (e.kind == Kind::Boss && y - 1 >= 1) {
        int total = static_cast<int>(e.words.size());
        std::string bar = "[";
        for (int i = 0; i < total; ++i)
            bar += i < e.wordsLeft() ? '#' : '-';
        bar += ']';
        int bx = static_cast<int>(std::lround(e.x)) -
                 static_cast<int>(bar.size()) / 2;
        s.print(y - 1, bx, bar, C_RED, true);
    }
}

void drawHud(const Game &g, long highScore, Screen &s) {
    int rows = s.rows(), cols = s.cols();
    char buf[128];

    for (int x = 0; x < cols; ++x) s.put(0, x, ' ', C_HUD);
    // PTS = cumulative points earned this run (only ever grows, spending
    // doesn't touch it); SPEND = the balance actually available right now.
    snprintf(buf, sizeof buf, " PTS %ld  SPEND %ld  x%d  WPM %d ",
             g.totalScore, g.score, g.combo, liveWpm(g));
    s.print(0, 1, buf, C_HUD);
    snprintf(buf, sizeof buf, " LVL %d  KILLS %d  BEST %ld ", level(g),
             g.kills, std::max(highScore, g.totalScore));
    s.print(0, cols - static_cast<int>(strlen(buf)) - 1, buf, C_HUD);

    if (g.freezeTimer > 0) {
        snprintf(buf, sizeof buf, " FROZEN %.1fs ", g.freezeTimer);
        s.print(0, cols / 2 - 7, buf, C_CYAN, true);
    }

    // HP bar bottom-left, wall status next to it.
    std::string hp = "TOWER [";
    for (int i = 0; i < g.towerMaxHp; ++i) hp += i < g.towerHp ? '#' : ' ';
    hp += ']';
    s.print(rows - 1, 1, hp, g.towerHp <= g.towerMaxHp / 3 ? C_RED : C_HUD);
    if (g.wallHp > 0) {
        snprintf(buf, sizeof buf, "  WALL %d/%d", g.wallHp, kWallMaxHp);
        s.print(rows - 1, 1 + static_cast<int>(hp.size()), buf);
    }

    if (g.cmdMode) {
        // Prompt with the committed path, e.g. "> build tower n_".
        std::string typed = ">";
        for (const auto &t : g.cmdPath) typed += " " + t;
        typed += " " + g.cmdBuf + "_";
        s.print(rows - 1, cols / 2 - 10, typed, C_YELLOW, true);

        // The row above lists this level's options, grayed out; typing
        // filters them away. Final choices show their price (red when you
        // can't afford it), and the letters you've typed light up.
        auto opts = cmdOptions(g);
        int width = 0;
        std::vector<const CmdOpt *> shown;
        for (const auto &o : opts) {
            if (!g.cmdBuf.empty() && o.name.rfind(g.cmdBuf, 0) != 0) continue;
            shown.push_back(&o);
            width += static_cast<int>(o.name.size()) + 3;
            if (o.price >= 0)
                width += static_cast<int>(std::to_string(o.price).size()) + 1;
        }
        int x = std::max(1, cols / 2 - width / 2);
        for (const CmdOpt *o : shown) {
            for (size_t i = 0; i < o->name.size(); ++i) {
                bool hit = i < g.cmdBuf.size();
                if (hit) s.put(rows - 2, x++, o->name[i], C_YELLOW, true);
                else s.put(rows - 2, x++, o->name[i], C_WHITE, false, true);
            }
            if (o->price >= 0) {
                std::string pr = " " + std::to_string(o->price);
                s.print(rows - 2, x, pr,
                        g.score >= o->price ? C_YELLOW : C_RED);
                x += static_cast<int>(pr.size());
            }
            x += 3;
        }
        if (shown.empty())
            s.print(rows - 2, cols / 2 - 8, "(no options here)", C_WHITE,
                    false, true);
    } else {
        std::string hint =
            platformCanQuit()
                ? "Space: build menu ('quit' exits) | Esc/Backspace: drop target"
                : "Space: build menu | Esc/Backspace: drop target";
        s.print(rows - 1, cols - static_cast<int>(hint.size()) - 1, hint);

        if (g.msgTimer > 0)
            s.print(rows - 2, cols / 2 - static_cast<int>(g.msg.size()) / 2,
                    g.msg, C_YELLOW, true);
    }
}

// The level-up draft: a centered overlay on top of the (paused) battlefield.
// Numbers pick — they can't collide with enemy words, so no misclicks even
// in a full swarm.
void drawDraft(const Game &g, Screen &s) {
    int cy = s.rows() / 2, cx = s.cols() / 2;
    int top = cy - 3;
    // Blank a backdrop wide enough for the option lines.
    for (int y = top - 1; y <= top + 3 + g.draftCount; ++y)
        for (int x = cx - 30; x <= cx + 30; ++x) s.put(y, x, ' ');

    const char *title = "L E V E L   U P  --  choose:";
    s.print(top, cx - static_cast<int>(strlen(title)) / 2, title, C_YELLOW,
            true);
    for (int k = 0; k < g.draftCount; ++k) {
        int id = g.draftIds[k];
        char line[96];
        snprintf(line, sizeof line, "%d) %-20s %s", k + 1, kUpgDefs[id].name,
                 upgDesc(g, id).c_str());
        s.print(top + 2 + k, cx - 28, line, C_WHITE);
    }
    const char *hint = "(game is paused)";
    s.print(top + 3 + g.draftCount, cx - static_cast<int>(strlen(hint)) / 2,
            hint, C_WHITE, false, true);
}

void drawGameOver(const Game &g, long highScore, Screen &s) {
    bool record = !g.cheat && g.totalScore > highScore;
    int cy = s.rows() / 2, cx = s.cols() / 2;
    char buf[96];
    // Blank a backdrop so the stats are readable over the battlefield.
    for (int y = cy - 5; y <= cy + 6; ++y)
        for (int x = cx - 22; x <= cx + 22; ++x) s.put(y, x, ' ');
    const char *title = "  T O W E R   F E L L  ";
    s.print(cy - 4, cx - static_cast<int>(strlen(title)) / 2, title, C_RED,
            true);
    snprintf(buf, sizeof buf, "total score %-8ld kills %d", g.totalScore,
             g.kills);
    s.print(cy - 2, cx - 12, buf);
    snprintf(buf, sizeof buf, "level %-8d time  %ds", level(g),
             static_cast<int>(g.elapsed));
    s.print(cy - 1, cx - 12, buf);
    snprintf(buf, sizeof buf, "best combo x%-4d (%d kill streak)", g.bestCombo,
             g.bestStreak);
    s.print(cy, cx - 12, buf);
    snprintf(buf, sizeof buf, "avg speed %d wpm", overallWpm(g));
    s.print(cy + 1, cx - 12, buf);
    if (record) {
        s.print(cy + 3, cx - 9, "*** NEW RECORD ***", C_YELLOW, true);
    } else {
        snprintf(buf, sizeof buf, "best: %ld", highScore);
        s.print(cy + 3, cx - 9, buf);
    }
    s.print(cy + 5, cx - 15,
            platformCanQuit() ? "Enter: play again    Q: quit"
                              : "Enter: play again",
            C_WHITE, false, true);
}

void drawShakeBorder(Screen &s) {
    int rows = s.rows(), cols = s.cols();
    for (int x = 0; x < cols; ++x) {
        s.put(0, x, '-', C_RED);
        s.put(rows - 1, x, '-', C_RED);
    }
    for (int y = 0; y < rows; ++y) {
        s.put(y, 0, '|', C_RED);
        s.put(y, cols - 1, '|', C_RED);
    }
}

}  // namespace

// ---- public API -------------------------------------------------------------

void gameInit(long startingScore) {
    sHighScore = platformLoadHighScore();
    sCheatStartScore = startingScore;
    sQuit = false;
    sWelcome = true;
    sWelcomeBuf.clear();
    resetGame(G);
}

void gameKey(int ch) {
    Game &g = G;
    if (sWelcome) {
        welcomeKey(ch);
        return;
    }
    if (g.draftOpen && !g.over) {
        // Full pause: only the number keys do anything. Letters are
        // swallowed so the pause can't be exploited for free typing.
        if (ch >= '1' && ch <= '3') {
            int k = ch - '1';
            if (k < g.draftCount) {
                applyUpgrade(g, g.draftIds[k]);
                g.draftOpen = false;
            }
        }
        return;
    }
    if (g.cmdMode) {
        // The menu swallows input; the game keeps running under it.
        if (ch == 27) {
            g.cmdMode = false;
            g.cmdPath.clear();
            g.cmdBuf.clear();
        } else if (ch == 127 || ch == 8) {
            cmdBackspace(g);
        } else if (ch >= 'a' && ch <= 'z') {
            cmdType(g, static_cast<char>(ch));
        }
        return;
    }
    if (g.over) {
        if (ch == '\n' || ch == '\r') {
            if (!g.cheat && g.totalScore > sHighScore) sHighScore = g.totalScore;
            resetGame(g);
        } else if ((ch == 'q' || ch == 27) && platformCanQuit()) {
            sQuit = true;
        }
        return;
    }
    if (ch == ' ') {
        g.cmdMode = true;
        return;
    }
    handleKey(g, ch);
}

bool gameFrame(double dt, Screen &s) {
    Game &g = G;
    dt = std::min(dt, 0.1);  // don't lurch after a stall
    g.viewRows = s.rows();
    g.viewCols = s.cols();

    if (sWelcome && !sQuit) {
        s.clear();
        drawWelcome(sHighScore, s);
        return true;
    }

    if (g.quitReq) sQuit = true;
    if (sQuit) {
        if (!g.cheat && g.totalScore > sHighScore) {
            platformSaveHighScore(g.totalScore);
            sHighScore = g.totalScore;
        }
        return false;
    }

    if (!g.over) {
        update(g, dt, s.rows(), s.cols());
        if (g.over) {
            g.cmdMode = false;  // don't let the menu eat the restart key
            g.cmdPath.clear();
            g.cmdBuf.clear();
            if (!g.cheat && g.totalScore > sHighScore)
                platformSaveHighScore(g.totalScore);
        }
    }

    s.clear();
    if (g.shake) {  // one-frame red flash on a typo
        drawShakeBorder(s);
        g.shake = false;
    }
    drawWall(g, s);
    drawTower(g, s);
    drawTurrets(g, s);
    std::vector<int> turretTargets = lockedTurretTargets(g, s.rows(), s.cols());
    for (size_t i = 0; i < g.enemies.size(); ++i) {
        bool locked = std::find(turretTargets.begin(), turretTargets.end(),
                                 g.enemies[i].id) != turretTargets.end();
        drawEnemy(g.enemies[i], static_cast<int>(i) == g.target, locked,
                  g.freezeTimer > 0, s);
    }
    drawCenterStats(g, s);
    drawHud(g, sHighScore, s);
    if (g.draftOpen && !g.over) drawDraft(g, s);
    if (g.over) drawGameOver(g, sHighScore, s);
    return true;
}

std::string gameGoodbye() {
    char buf[96];
    long best = G.cheat ? sHighScore : std::max(sHighScore, G.totalScore);
    snprintf(buf, sizeof buf, "final score: %ld (total %ld)   best: %ld",
             G.score, G.totalScore, best);
    return buf;
}

}  // namespace ktd
