// ============================================================
// chillbot v1.0.13-DIAG by Ivanogolik
// DIAGNOSTIC: forced jump test + object scan logging
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
    bool forcedJumpTest = true;  // прыгать каждые 90 кадров для теста
    bool isHoldingJump = false;
    int jumpHoldFrames = 0;
    int forcedJumpCounter = 0;
    std::set<int> deathPositions;
    int totalDeaths = 0;
    std::string currentLevelName = "";

    // ДИАГНОСТИКА: сканируем и логируем что видим
    void diagnosticScan(PlayLayer* pl) {
        if (!pl || !pl->m_player1) return;
        auto player = pl->m_player1;
        float px = player->getPositionX();
        float py = player->getPositionY();

        if (!pl->m_objects) {
            log::warn("DIAG: m_objects is NULL!");
            return;
        }
        int count = pl->m_objects->count();
        log::info("DIAG: player at X={:.0f} Y={:.0f}, total objects={}", px, py, count);

        // Найдём ближайшие 5 объектов впереди
        struct ObjInfo { float dx, dy; int id; };
        std::vector<ObjInfo> nearby;
        for (int i = 0; i < count; ++i) {
            auto obj = static_cast<GameObject*>(pl->m_objects->objectAtIndex(i));
            if (!obj) continue;
            float ox = obj->getPositionX();
            float oy = obj->getPositionY();
            float dx = ox - px;
            float dy = oy - py;
            if (dx < -50.0f || dx > 500.0f) continue;
            nearby.push_back({dx, dy, obj->m_objectID});
        }
        std::sort(nearby.begin(), nearby.end(),
                  [](const ObjInfo& a, const ObjInfo& b){ return a.dx < b.dx; });
        int show = (int)std::min((size_t)8, nearby.size());
        for (int i = 0; i < show; ++i) {
            log::info("DIAG: obj id={} dx={:.0f} dy={:.0f}",
                      nearby[i].id, nearby[i].dx, nearby[i].dy);
        }
    }

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
                if (dx < 5.0f || dx > 300.0f) continue;
                if (dy > 150.0f) continue;

                int id = obj->m_objectID;
                // Spikes (расширенный список)
                if (id == 8 || id == 9 || id == 39 || id == 103 ||
                    id == 216 || id == 217 || id == 218 || id == 219 ||
                    id == 392 || id == 397 || id == 458 || id == 459 ||
                    id == 446 || id == 447 || id == 448 || id == 449 ||
                    id == 88 || id == 89 || id == 98 || id == 99 ||
                    id == 100 || id == 177 || id == 178 || id == 179 ||
                    id == 84 || id == 85 || id == 86 || id == 87) {
                    if (dx < 130.0f) return true;
                }
                // Blocks
                if (id == 1 || id == 2 || id == 3 || id == 4 || id == 5 ||
                    id == 6 || id == 7 || id == 40 || id == 41 || id == 42 ||
                    id == 43 || id == 44 || id == 467 || id == 468) {
                    if (dx < 80.0f && dy < 50.0f) return true;
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
    }

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
};

static BotState g_bot;
static int g_tickCount = 0;
static int g_keyLogCount = 0;

class $modify(MyKeyboardDispatcher, cocos2d::CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(cocos2d::enumKeyCodes key, bool down, bool repeat, double delta) {
        if (down && !repeat && g_keyLogCount < 30) {
            log::info("KEY: code={} (0x{:X})", (int)key, (int)key);
            g_keyLogCount++;
        }
        if (down && !repeat && key == cocos2d::enumKeyCodes::KEY_F5) {
            g_bot.active = !g_bot.active;
            log::info(">>> F5 - Bot {} <<<", g_bot.active ? "ENABLED" : "DISABLED");
        }
        if (down && !repeat && key == cocos2d::enumKeyCodes::KEY_B) {
            g_bot.forcedJumpTest = !g_bot.forcedJumpTest;
            log::info(">>> B - Forced jump test {} <<<",
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
            log::info("tick #{} (active={}, deaths={})",
                      g_tickCount, g_bot.active ? 1 : 0, g_bot.totalDeaths);
        }
        // Каждые 2 секунды печатаем что бот видит
        if (g_tickCount % 120 == 0 && g_bot.active) {
            g_bot.diagnosticScan(this);
        }

        if (!g_bot.active) return;
        if (!this->m_player1) return;

        bool shouldJump = g_bot.shouldJump(this);

        // ТЕСТ: принудительный прыжок каждые 90 кадров (≈1.5 сек)
        // если в игре куб подпрыгнул — handleButton РАБОТАЕТ
        if (g_bot.forcedJumpTest) {
            g_bot.forcedJumpCounter++;
            if (g_bot.forcedJumpCounter >= 90) {
                g_bot.forcedJumpCounter = 0;
                shouldJump = true;
                log::info("FORCED JUMP TEST at X={}",
                          (int)this->m_player1->getPositionX());
            }
        }

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
};

$on_mod(Loaded) {
    log::info("================================================");
    log::info("=== chillbot v1.0.13-DIAG loaded ===");
    log::info("=== AUTO-START + FORCED JUMP TEST ===");
    log::info("=== F5: toggle bot, B: toggle forced jumps ===");
    log::info("================================================");
}
