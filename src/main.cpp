// ============================================================
// chillbot v1.0.17-MEGAJUMP by Ivanogolik
// Fixed signatures for propellPlayer and queueButton
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
    int jumpCycle = 0;
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
        jumpCycle = 0;
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
            float px = this->m_player1 ? this->m_player1->getPositionX() : -1;
            log::info("tick #{} (active={}, deaths={}, X={:.0f}, Y={:.1f})",
                      g_tickCount, g_bot.active ? 1 : 0,
                      g_bot.totalDeaths, px, py);
        }

        if (!g_bot.active) return;
        if (!this->m_player1) return;

        if (g_bot.forcedJumpTest) {
            g_bot.forcedJumpCounter++;

            // НАЖАТЬ - на 60-м кадре каждой секунды
            if (g_bot.forcedJumpCounter == 60) {
                int method = g_bot.jumpCycle % 6;
                log::info("=== TEST METHOD {} at X={} Y={} ===",
                          method,
                          (int)this->m_player1->getPositionX(),
                          (int)this->m_player1->getPositionY());

                switch (method) {
                    case 0: {
                        // МЕТОД A: Симуляция нажатия Space через CCKeyboardDispatcher
                        log::info("  TRYING: dispatchKeyboardMSG(KEY_Space, true)");
                        auto kd = cocos2d::CCDirector::sharedDirector()->getKeyboardDispatcher();
                        if (kd) {
                            kd->dispatchKeyboardMSG(cocos2d::enumKeyCodes::KEY_Space, true, false, 0.0);
                        }
                        break;
                    }
                    case 1: {
                        // МЕТОД B: Прямое изменение m_yVelocity
                        log::info("  TRYING: m_player1->m_yVelocity = 16.0");
                        this->m_player1->m_yVelocity = 16.0;
                        break;
                    }
                    case 2: {
                        // МЕТОД C: propellPlayer (ПРАВИЛЬНАЯ сигнатура: yVelocity, noEffects, objectType)
                        log::info("  TRYING: m_player1->propellPlayer(16.0f, false, 0)");
                        this->m_player1->propellPlayer(16.0f, false, 0);
                        break;
                    }
                    case 3: {
                        // МЕТОД D: queueButton (ПРАВИЛЬНАЯ сигнатура: button, push, isPlayer2, timestamp)
                        log::info("  TRYING: queueButton(1, true, false, 0.0)");
                        static_cast<GJBaseGameLayer*>(this)->queueButton(1, true, false, 0.0);
                        break;
                    }
                    case 4: {
                        // МЕТОД E: Все 4 старых метода + изменение Y velocity
                        log::info("  TRYING: ALL old methods + yVelocity push");
                        this->handleButton(true, 1, true);
                        this->m_player1->pushButton(PlayerButton::Jump);
                        static_cast<GJBaseGameLayer*>(this)->handleButton(true, 1, true);
                        this->m_player1->m_yVelocity = 16.0;
                        break;
                    }
                    case 5: {
                        // МЕТОД F: handleButton + propellPlayer (комбо)
                        log::info("  TRYING: handleButton + propellPlayer");
                        this->handleButton(true, 1, true);
                        this->m_player1->propellPlayer(16.0f, false, 0);
                        break;
                    }
                }
            }

            // ОТПУСТИТЬ - на 70-м кадре
            if (g_bot.forcedJumpCounter == 70) {
                int method = g_bot.jumpCycle % 6;
                if (method == 0) {
                    auto kd = cocos2d::CCDirector::sharedDirector()->getKeyboardDispatcher();
                    if (kd) kd->dispatchKeyboardMSG(cocos2d::enumKeyCodes::KEY_Space, false, false, 0.0);
                }
                this->handleButton(false, 1, true);
                this->m_player1->releaseButton(PlayerButton::Jump);
                static_cast<GJBaseGameLayer*>(this)->handleButton(false, 1, true);
                static_cast<GJBaseGameLayer*>(this)->queueButton(1, false, false, 0.0);
                log::info("  RELEASE all (after method {})", method);
            }

            // Через 90 кадров переключаемся на следующий метод
            if (g_bot.forcedJumpCounter >= 90) {
                g_bot.forcedJumpCounter = 0;
                g_bot.jumpCycle++;
                log::info("--- Switching to method {} ---", g_bot.jumpCycle % 6);
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
    log::info("=== chillbot v1.0.17-MEGAJUMP loaded ===");
    log::info("=== 6 jump methods - watch playerY change ===");
    log::info("================================================");
}
