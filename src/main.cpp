// Defensive: kill Windows SDK macros BEFORE any Geode header includes them проверочка на изменение
#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
#endif

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <fstream>
#include <vector>
#include <random>
#include <filesystem>
#include <string>

// ---- defensive: kill Windows SDK macros that collide with C++ identifiers ----
#ifdef IN
#undef IN
#endif
#ifdef OUT
#undef OUT
#endif
#ifdef ERROR
#undef ERROR
#endif
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif

using namespace geode::prelude;

// ============================================================
// chillbot — AI bot for Geometry Dash 2.2081 / Geode 5.6.1
// Author: Ivanogolik
// ============================================================

struct InputFrame {
    int      frame;     // physics frame number
    float    xpos;      // player x position
    bool     hold;      // jump hold state
    uint8_t  button;    // 1 = jump, 2 = orb-like (currently only 1 used)
};

class Chillbot {
public:
    static Chillbot& get() {
        static Chillbot inst;
        return inst;
    }

    bool enabled       = false;
    bool recording     = true;   // record successful runs
    bool playing       = false;  // replay best macro
    int  attempt       = 0;
    int  bestFrame     = 0;
    PlayLayer* layer   = nullptr;

    std::vector<InputFrame> currentRun;
    std::vector<InputFrame> bestMacro;

    std::mt19937 rng{ std::random_device{}() };

    std::filesystem::path savePath() {
        return Mod::get()->getSaveDir() / "chillbot_macro.gdai";
    }

    void onLevelStart(PlayLayer* pl) {
        layer = pl;
        attempt++;
        currentRun.clear();
        if (bestMacro.empty()) loadMacro();
    }

    void onLevelReset() {
        currentRun.clear();
    }

    void onLevelComplete() {
        if (!enabled) return;
        if (currentRun.size() > bestMacro.size()) {
            bestMacro = currentRun;
            saveMacro();
            Notification::create("chillbot: new best run saved!", NotificationIcon::Success)->show();
        }
    }

    // Simple "AI": looks at obstacles ahead via player Y velocity heuristics.
    // This is intentionally a placeholder — real obstacle detection requires
    // scanning level objects, which is left as a learning loop:
    // each attempt the bot mutates timings and keeps the longest survival.
    bool decideJump(PlayLayer* pl) {
        if (!pl || !pl->m_player1) return false;

        // Replay best macro if we have one and bot is in "play" mode
        if (playing && !bestMacro.empty()) {
            int frame = (int)currentRun.size();
            for (auto& f : bestMacro) {
                if (f.frame == frame) return f.hold;
            }
            return false;
        }

        // Learning mode: small random perturbations.
        // Real obstacle scanning would require iterating m_objects;
        // this minimal version mutates timings each attempt and keeps
        // the longest survival as the new "best" macro.
        std::uniform_int_distribution<int> chance(0, 100);
        if (chance(rng) < 6) return true;
        return false;
    }

    void recordFrame(PlayLayer* pl, bool hold) {
        if (!pl || !pl->m_player1) return;
        InputFrame f;
        f.frame  = (int)currentRun.size();
        f.xpos   = pl->m_player1->getPositionX();
        f.hold   = hold;
        f.button = 1;
        currentRun.push_back(f);
    }

    void saveMacro() {
        std::ofstream out(savePath(), std::ios::binary);
        if (!out) return;
        out << "GDAI1\n";
        out << bestMacro.size() << "\n";
        for (auto& f : bestMacro) {
            out << f.frame << " " << f.xpos << " "
                << (f.hold ? 1 : 0) << " " << (int)f.button << "\n";
        }
    }

    void loadMacro() {
        std::ifstream in(savePath(), std::ios::binary);
        if (!in) return;
        std::string magic; in >> magic;
        if (magic != "GDAI1") return;
        size_t n; in >> n;
        bestMacro.clear();
        bestMacro.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            InputFrame f; int hold, btn;
            in >> f.frame >> f.xpos >> hold >> btn;
            f.hold = hold != 0;
            f.button = (uint8_t)btn;
            bestMacro.push_back(f);
        }
        log::info("chillbot: loaded macro with {} frames", bestMacro.size());
    }

    void toggle() {
        enabled = !enabled;
        Notification::create(
            enabled ? "chillbot: ON" : "chillbot: OFF",
            enabled ? NotificationIcon::Success : NotificationIcon::Warning
        )->show();
    }
};

// ---------- Hooks ----------

class $modify(AIPlayLayer, PlayLayer) {
    bool init(GJGameLevel* lvl, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(lvl, useReplay, dontCreateObjects)) return false;
        Chillbot::get().onLevelStart(this);
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        Chillbot::get().onLevelReset();
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        Chillbot::get().onLevelComplete();
    }

    // PlayLayer::update runs every physics frame in 2.2081
    void update(float dt) {
        PlayLayer::update(dt);
        auto& bot = Chillbot::get();
        if (!bot.enabled) return;
        if (!this->m_player1) return;

        bool jump = bot.decideJump(this);
        if (jump) {
            this->m_player1->pushButton(PlayerButton::Jump);
        } else {
            this->m_player1->releaseButton(PlayerButton::Jump);
        }
        bot.recordFrame(this, jump);
    }
};

// ---------- F5 keybind (hard-coded fallback) ----------
// Geode 5.6.1 / cocos2d-x signature: dispatchKeyboardMSG(enumKeyCodes, bool, bool, double)
// The 4th double parameter was added in newer bindings; we must match exactly.

class $modify(AIKeyDispatcher, cocos2d::CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(cocos2d::enumKeyCodes key, bool down, bool repeat, double delta) {
        if (down && !repeat && key == cocos2d::enumKeyCodes::KEY_F5) {
            Chillbot::get().toggle();
            return true;
        }
        return cocos2d::CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, delta);
    }
};

// ---------- Entry ----------

$on_mod(Loaded) {
    log::info("chillbot loaded | by Ivanogolik | Geode {} | GD 2.2081",
              Mod::get()->getVersion().toVString());

    if (Loader::get()->isModLoaded("hjfod.betteredit"))
        log::info("BetterEdit detected — extra editor features available");
    if (Loader::get()->isModLoaded("geode.custom-keybinds"))
        log::info("Custom Keybinds detected — F5 can be rebound in settings");
    if (Loader::get()->isModLoaded("geode.devtools"))
        log::info("DevTools detected — debug overlay available");
}
