// ============================================================
// chillbot v1.0.12-FINAL by Ivanogolik
// AI bot for Geometry Dash 2.2081 / Geode 5.6.1
// Auto-start on level enter, F5 or B to toggle, key logger
// ============================================================

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
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GameObject.hpp>
#include <fstream>
#include <vector>
#include <set>
#include <filesystem>
#include <cmath>

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

struct BotState {
    bool active = false;
    bool autoStart = true;
    bool isHoldingJump = false;
    int jumpHoldFrames = 0;
    std::set<int> deathPositions;
    int totalDeaths = 0;
    float bestPercent = 0.0f;
    std::string currentLevelName = "";

    bool shouldJump(PlayLayer* pl) {
        if (!pl || !pl->m_player1) return false;
        auto player = pl->m_player1;
        float px = player->getPositionX();
        float py = player->getPositionY();

        for (int dx : deathPositions) {
            float dist = (float)dx - px;
            if (dist > 0 && dist < 60) return true;
        }

        if (pl->m_objects) {
            int count = pl->m_objects->count();
            for (int i = 0; i < count; ++i) {
                auto obj = static_cast<GameObject*>(pl->m_objects->objectAtIndex(i));
                if (!obj) continue;
                float ox = obj->getPositionX();
                float oy = obj->getPositionY();
                float dx = ox - px;
                float dy = std::abs(oy - py);
                if (dx < 5.0f || dx > 180.0f) continue;
                if (dy > 100.0f) continue;

                int id = obj->m_objectID;
                if (id == 8 || id == 9 || id == 39 || id == 103 ||
                    id == 216 || id == 217 || id == 218 || id == 219 ||
                    id == 392 || id == 397 || id == 458 || id == 459) {
                    if (dx < 90.0f) return true;
                }
                if (id == 40 || id == 41 || id == 42 || id == 43 ||
                    id == 44 || id == 467 || id == 468) {
                    if (dx < 60.0f) return true;
                }
                if (id == 36 || id == 84 || id == 141 || id == 1022 ||
                    id == 1330 || id == 1333 || id == 1594 || id == 1751) {
                    if (dx < 50.0f && dx > 10.0f) return true;
                }
            }
        }
        return false;
    }

    void onDeath(PlayLayer* pl) {
        if (!pl || !pl->m_player1) return;
        int px = (int)pl->m_player1->getPositionX();
        deathPositions.insert(px - 30);
        totalDeaths++;
        log::info("Death at X={}, total deaths={}", px, totalDeaths);
        saveMacro();
    }

    void onLevelInit(PlayLayer* pl) {
        if (!pl || !pl->m_level) return;
        currentLevelName = pl->m_level->m_levelName;
        log::info("Level loaded: {}", currentLevelName);
        loadMacro();
        if (autoStart) {
            active = true;
            log::info(">>> AUTO-ENABLED on level enter <<<");
        }
    }

    void onLevelComplete(PlayLayer* pl) {
        if (!pl) return;
        log::info("LEVEL COMPLETE!");
        bestPercent = 100.0f;
        saveMacro();
    }

    std::filesystem::path getMacroPath() {
        auto dir = Mod::get()->getSaveDir();
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir / "chillbot_macro.gdai";
    }

    void saveMacro() {
        auto path = getMacroPath();
        std::ofstream out(path);
        if (!out.is_open()) return;
        out << "GDAI1\n";
        out << "LEVEL " << currentLevelName << "\n";
        out << "BEST " << bestPercent << "\n";
        out << "DEATHS " << totalDeaths << "\n";
        for (int dx : deathPositions) out << "DEATH " << dx << "\n";
        out.close();
    }

    void loadMacro() {
        auto path = getMacroPath();
        std::ifstream in(path);
        if (!in.is_open()) {
            log::info("No saved macro yet (first run on this level)");
            return;
        }
        deathPositions.clear();
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("DEATH ", 0) == 0) {
                deathPositions.insert(std::atoi(line.c_str() + 6));
            } else if (line.rfind("BEST ", 0) == 0) {
                bestPercent = (float)std::atof(line.c_str() + 5);
            } else if (line.rfind("DEATHS ", 0) == 0) {
                totalDeaths = std::atoi(line.c_str() + 7);
            }
        }
        log::info("Macro loaded: {} deaths, best={}%", deathPositions.size(), bestPercent);
    }
};

static BotState g_bot;
static int g_tickCount = 0;
static int g_keyLogCount = 0;

class $modify(MyKeyboardDispatcher, cocos2d::CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(cocos2d::enumKeyCodes key, bool down, bool repeat, double delta) {
        if (down && !repeat && g_keyLogCount < 50) {
            log::info("KEY: code={} (0x{:X})", (int)key, (int)key);
            g_keyLogCount++;
        }
        if (down && !repeat && key == cocos2d::enumKeyCodes::KEY_F5) {
            g_bot.active = !g_bot.active;
            log::info(">>> F5 pressed - Bot {} <<<", g_bot.active ? "ENABLED" : "DISABLED");
            Notification::create(
                g_bot.active ? "chillbot: ENABLED" : "chillbot: DISABLED",
                g_bot.active ? NotificationIcon::Success : NotificationIcon::Info,
                1.5f
            )->show();
        }
        if (down && !repeat && key == cocos2d::enumKeyCodes::KEY_B) {
            g_bot.active = !g_bot.active;
            log::info(">>> B pressed - Bot {} <<<", g_bot.active ? "ENABLED" : "DISABLED");
        }
        return cocos2d::CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, delta);
    }
};

class $modify(BotPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        g_bot.onLevelInit(this);
        g_tickCount = 0;
        return true;
    }

    void update(float dt) {
        PlayLayer::update(dt);

        g_tickCount++;
        if (g_tickCount % 60 == 0) {
            log::info("tick #{} (active={}, deaths={})",
                      g_tickCount, g_bot.active ? 1 : 0, g_bot.totalDeaths);
        }

        if (!g_bot.active) return;
        if (!this->m_player1) return;

        bool shouldJump = g_bot.shouldJump(this);

        if (shouldJump && !g_bot.isHoldingJump) {
            this->handleButton(true, 1, true);
            this->m_player1->pushButton(PlayerButton::Jump);
            g_bot.isHoldingJump = true;
            g_bot.jumpHoldFrames = 0;
            log::info("JUMP! at X={}", (int)this->m_player1->getPositionX());
        } else if (g_bot.isHoldingJump) {
            g_bot.jumpHoldFrames++;
            if (g_bot.jumpHoldFrames > 6 || !shouldJump) {
                this->handleButton(false, 1, true);
                this->m_player1->releaseButton(PlayerButton::Jump);
                g_bot.isHoldingJump = false;
            }
        }
    }

    void destroyPlayer(PlayerObject* p, GameObject* o) {
        if (g_bot.active) g_bot.onDeath(this);
        PlayLayer::destroyPlayer(p, o);
    }

    void levelComplete() {
        g_bot.onLevelComplete(this);
        PlayLayer::levelComplete();
    }
};

$on_mod(Loaded) {
    log::info("================================================");
    log::info("=== chillbot v1.0.12-FINAL loaded ===");
    log::info("=== AUTO-START enabled (no F5 needed) ===");
    log::info("=== F5 or B key toggles bot manually ===");
    log::info("================================================");
}
