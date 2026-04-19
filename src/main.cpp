// ============================================================
// chillbot v1.0.19-DEEP-DIAG by Ivanogolik
// Hooks GJBaseGameLayer::update + PlayLayer hooks for tracking
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
#include <Geode/modify/GJBaseGameLayer.hpp>
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

using namespace geode::prelude;

struct BotState {
    bool active = false;
    int totalDeaths = 0;
    int realDeaths = 0;
    float lastX = 0.0f;
    float lastY = 0.0f;
    std::string currentLevelName = "";
    bool seenAnyUpdate = false;
    bool seenBaseUpdate = false;
    bool seenPlayLayerUpdate = false;
};

static BotState g_bot;
static int g_baseTickCount = 0;
static int g_playTickCount = 0;
static int g_keyLogCount = 0;

class $modify(MyKeyboardDispatcher, cocos2d::CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(cocos2d::enumKeyCodes key, bool down, bool repeat, double delta) {
        if (down && !repeat) {
            log::info("KEY: code={} (0x{:X})", (int)key, (int)key);
            if (key == cocos2d::enumKeyCodes::KEY_F5) {
                g_bot.active = !g_bot.active;
                log::info(">>> F5 - Bot {} <<<", g_bot.active ? "ON" : "OFF");
            }
        }
        return cocos2d::CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, delta);
    }
};

// Hook GJBaseGameLayer::update - this MUST be called by the engine
class $modify(BotBaseGameLayer, GJBaseGameLayer) {
    void update(float dt) {
        GJBaseGameLayer::update(dt);
        g_baseTickCount++;
        g_bot.seenBaseUpdate = true;

        if (g_baseTickCount % 60 == 0) {
            float px = this->m_player1 ? this->m_player1->getPositionX() : -999;
            float py = this->m_player1 ? this->m_player1->getPositionY() : -999;
            log::info("BASE_TICK #{} X={:.0f} Y={:.1f} dt={:.4f}",
                      g_baseTickCount, px, py, dt);
        }
    }
};

class $modify(BotPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        if (level) {
            g_bot.currentLevelName = level->m_levelName;
            log::info("Level loaded: {}", g_bot.currentLevelName);
        }
        g_baseTickCount = 0;
        g_playTickCount = 0;
        g_bot.realDeaths = 0;
        g_bot.totalDeaths = 0;
        g_bot.lastX = 0;
        g_bot.lastY = 0;
        g_bot.seenBaseUpdate = false;
        g_bot.seenPlayLayerUpdate = false;
        return true;
    }

    void update(float dt) {
        PlayLayer::update(dt);
        g_playTickCount++;
        g_bot.seenPlayLayerUpdate = true;

        if (g_playTickCount % 60 == 0) {
            float px = this->m_player1 ? this->m_player1->getPositionX() : -999;
            float py = this->m_player1 ? this->m_player1->getPositionY() : -999;
            log::info("PLAY_TICK #{} X={:.0f} Y={:.1f} dt={:.4f}",
                      g_playTickCount, px, py, dt);
        }
    }

    void destroyPlayer(PlayerObject* p, GameObject* o) {
        // Считаем все вызовы destroyPlayer
        g_bot.totalDeaths++;
        // Логируем только каждую 5-ю смерть чтобы не засорять
        if (g_bot.totalDeaths % 5 == 1) {
            int px = this->m_player1 ? (int)this->m_player1->getPositionX() : -1;
            int py = this->m_player1 ? (int)this->m_player1->getPositionY() : -1;
            log::info("destroyPlayer #{} at X={} Y={} object_id={}",
                      g_bot.totalDeaths, px, py,
                      o ? o->m_objectID : -1);
        }
        PlayLayer::destroyPlayer(p, o);
    }

    void resetLevel() {
        log::info(">>> resetLevel() — total destroyPlayer calls so far: {} <<<",
                  g_bot.totalDeaths);
        g_bot.totalDeaths = 0;
        g_baseTickCount = 0;
        g_playTickCount = 0;
        PlayLayer::resetLevel();
    }

    void levelComplete() {
        log::info(">>> LEVEL COMPLETE! <<<");
        PlayLayer::levelComplete();
    }
};

$on_mod(Loaded) {
    log::info("================================================");
    log::info("=== chillbot v1.0.19-DEEP-DIAG loaded ===");
    log::info("=== Hooks: PlayLayer + GJBaseGameLayer ===");
    log::info("=== Press F5 to toggle (no auto) ===");
    log::info("================================================");
}
