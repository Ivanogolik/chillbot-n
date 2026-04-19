// ============================================================
// chillbot v1.0.18-OBSERVE by Ivanogolik
// PASSIVE: just observe what game does, no jumps
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
#include <algorithm>

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
    bool autoStart = false;  // ВЫКЛЮЧЕНО! Нажми F5 если хочешь
    int totalDeaths = 0;
    float lastX = 0.0f;
    float lastY = 0.0f;
    std::string currentLevelName = "";

    void onLevelInit(PlayLayer* pl) {
        if (!pl || !pl->m_level) return;
        currentLevelName = pl->m_level->m_levelName;
        log::info("Level loaded: {}", currentLevelName);
        if (autoStart) {
            active = true;
            log::info(">>> AUTO-ENABLED on level enter <<<");
        } else {
            log::info(">>> Bot is OFF - press F5 to enable observer mode <<<");
        }
    }

    void onDeath(PlayLayer* pl) {
        if (!pl || !pl->m_player1) return;
        int px = (int)pl->m_player1->getPositionX();
        int py = (int)pl->m_player1->getPositionY();
        totalDeaths++;
        log::info("Death at X={} Y={} (total deaths={})", px, py, totalDeaths);
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
            log::info(">>> F5 - Observer {} <<<", g_bot.active ? "ON" : "OFF");
        }
        return cocos2d::CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, delta);
    }
};

class $modify(BotPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        g_bot.onLevelInit(this);
        g_tickCount = 0;
        g_bot.lastX = 0;
        g_bot.lastY = 0;
        return true;
    }

    void update(float dt) {
        // СНАЧАЛА вызываем оригинал — НИЧЕГО НЕ ДЕЛАЕМ!
        PlayLayer::update(dt);

        g_tickCount++;

        // Каждый кадр обновляем позицию
        if (this->m_player1) {
            float px = this->m_player1->getPositionX();
            float py = this->m_player1->getPositionY();

            // Каждые 30 кадров (0.5 сек) логируем подробности
            if (g_tickCount % 30 == 0) {
                float dx = px - g_bot.lastX;
                float dy = py - g_bot.lastY;
                log::info("tick #{} X={:.0f} Y={:.1f} dX={:.1f} dY={:.1f} dt={:.4f}",
                          g_tickCount, px, py, dx, dy, dt);
                g_bot.lastX = px;
                g_bot.lastY = py;
            }
        } else {
            if (g_tickCount % 60 == 0) {
                log::warn("tick #{}: m_player1 is NULL!", g_tickCount);
            }
        }

        // НИКАКИХ ПРЫЖКОВ! Просто наблюдаем
    }

    void destroyPlayer(PlayerObject* p, GameObject* o) {
        g_bot.onDeath(this);
        PlayLayer::destroyPlayer(p, o);
    }

    void resetLevel() {
        log::info(">>> resetLevel() called <<<");
        PlayLayer::resetLevel();
        g_tickCount = 0;
    }
};

$on_mod(Loaded) {
    log::info("================================================");
    log::info("=== chillbot v1.0.18-OBSERVE loaded ===");
    log::info("=== PASSIVE MODE - watches without acting ===");
    log::info("=== Press F5 to toggle observer ===");
    log::info("================================================");
}
