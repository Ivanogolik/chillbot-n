// ============================================================
// chillbot v1.0.15-MULTIJUMP by Ivanogolik
// Try multiple jump methods - one MUST work in 2.2081
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
    bool autoStart = true;
    bool forcedJumpTest = true;
    int forcedJumpCounter = 0;
    int totalDeaths = 0;
    std::string currentLevelName = "";

    void onLevelInit(PlayLayer* pl) {
        if (!pl || !pl->m_level) return;
        currentLevelName = pl->m_level->m_levelName;
        log::info("Level loaded: {}", currentLevelName);
        if (autoStart) {
            active = true;
            log::info(">>> AUTO-ENABLED on level enter <<<");
        }
        forcedJumpCounter = 0;
    }

    void onDeath(PlayLayer* pl) {
        if (!pl || !pl->m_player1) return;
        int px = (int)pl->m_player1->getPositionX();
        totalDeaths++;
        log::info("Death at X={}, total deaths={}", px, totalDeaths);
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
            log::info(">>> F5 - Bot {} <<<", g_bot.active ? "ENABLED" : "DISABLED");
        }
        if (down && !repeat && key == cocos2d::enumKeyCodes::KEY_B) {
            g_bot.forcedJumpTest = !g_bot.forcedJumpTest;
            log::info(">>> B - Forced jump {} <<<",
                      g_bot.forcedJumpTest ? "ON" : "OFF");
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
            float py = this->m_player1 ? this->m_player1->getPositionY() : -1;
            log::info("tick #{} (active={}, deaths={}, playerY={:.1f})",
                      g_tickCount, g_bot.active ? 1 : 0,
                      g_bot.totalDeaths, py);
        }

        if (!g_bot.active) return;
        if (!this->m_player1) return;

        if (g_bot.forcedJumpTest) {
            g_bot.forcedJumpCounter++;

            // НАЖАТЬ — раз в секунду на 60-м кадре
            if (g_bot.forcedJumpCounter == 60) {
                log::info("=== JUMP TEST at X={} Y={} ===",
                          (int)this->m_player1->getPositionX(),
                          (int)this->m_player1->getPositionY());

                // МЕТОД 1: handleButton от PlayLayer
                this->handleButton(true, 1, true);
                log::info("  M1: PlayLayer::handleButton(true, 1, true) called");

                // МЕТОД 2: pushButton на player с PlayerButton::Jump
                this->m_player1->pushButton(PlayerButton::Jump);
                log::info("  M2: m_player1->pushButton(PlayerButton::Jump) called");

                // МЕТОД 3: GJBaseGameLayer::handleButton (parent)
                static_cast<GJBaseGameLayer*>(this)->handleButton(true, 1, true);
                log::info("  M3: GJBaseGameLayer::handleButton(true, 1, true) called");

                // МЕТОД 4: handleButton с button=0 (вместо 1)
                this->handleButton(true, 0, true);
                log::info("  M4: PlayLayer::handleButton(true, 0, true) called");
            }

            // ОТПУСТИТЬ — на 70-м кадре (через 10 кадров после нажатия)
            if (g_bot.forcedJumpCounter == 70) {
                this->handleButton(false, 1, true);
                this->m_player1->releaseButton(PlayerButton::Jump);
                static_cast<GJBaseGameLayer*>(this)->handleButton(false, 1, true);
                this->handleButton(false, 0, true);
                log::info("  RELEASE all methods");
            }

            // СБРОС счётчика каждые 2 секунды
            if (g_bot.forcedJumpCounter >= 120) {
                g_bot.forcedJumpCounter = 0;
            }
        }
    }

    void destroyPlayer(PlayerObject* p, GameObject* o) {
        if (g_bot.active) g_bot.onDeath(this);
        PlayLayer::destroyPlayer(p, o);
    }
};

$on_mod(Loaded) {
    log::info("================================================");
    log::info("=== chillbot v1.0.15-MULTIJUMP loaded ===");
    log::info("=== Tries 4 jump methods each second ===");
    log::info("================================================");
}
