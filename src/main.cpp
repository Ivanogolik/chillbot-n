// ============================================================
// chillbot v1.0.20-FINALBOT by Ivanogolik
// Real bot: scans obstacles, jumps when needed, uses 4 methods
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
    bool active = true;       // ВКЛЮЧЕН по умолчанию
    bool jumping = false;
    int jumpHoldFrames = 0;
    int realDeaths = 0;
    float lastX = 0.0f;
    float maxX = 0.0f;        // максимальное X в этой попытке
    int jumpsExecuted = 0;
    std::string currentLevelName = "";
    std::set<int> deathPositions;

    bool shouldJump(GJBaseGameLayer* gl) {
        if (!gl || !gl->m_player1) return false;
        auto player = gl->m_player1;
        float px = player->getPositionX();
        float py = player->getPositionY();

        // Запомненные смертельные точки — прыгать заранее
        for (int dx : deathPositions) {
            float dist = (float)dx - px;
            if (dist > 0 && dist < 70) return true;
        }

        if (!gl->m_objects) return false;
        int count = gl->m_objects->count();

        for (int i = 0; i < count; ++i) {
            auto obj = static_cast<GameObject*>(gl->m_objects->objectAtIndex(i));
            if (!obj) continue;
            float ox = obj->getPositionX();
            float oy = obj->getPositionY();
            float dx = ox - px;
            float dy = std::abs(oy - py);

            // Игнорируем то что не впереди или далеко
            if (dx < 5.0f || dx > 200.0f) continue;
            if (dy > 120.0f) continue;

            int id = obj->m_objectID;

            // Шипы (опасные) - прыгаем рано
            if (id == 8 || id == 9 || id == 39 || id == 103 ||
                id == 216 || id == 217 || id == 218 || id == 219 ||
                id == 392 || id == 397 || id == 458 || id == 459 ||
                id == 446 || id == 447 || id == 88 || id == 89 ||
                id == 98 || id == 99 || id == 100 || id == 177 ||
                id == 178 || id == 179) {
                if (dx < 90.0f) return true;
            }
            // Блоки - прыгаем когда ближе
            if (id == 1 || id == 2 || id == 3 || id == 4 || id == 5 ||
                id == 6 || id == 7 || id == 40 || id == 41 || id == 42 ||
                id == 43 || id == 44 || id == 467 || id == 468) {
                if (dx < 70.0f && dy < 60.0f) return true;
            }
        }
        return false;
    }

    void executeJump(PlayLayer* pl) {
        if (!pl || !pl->m_player1) return;
        // ВСЕ методы прыжка одновременно — один точно сработает
        pl->handleButton(true, 1, true);
        pl->m_player1->pushButton(PlayerButton::Jump);
        static_cast<GJBaseGameLayer*>(pl)->handleButton(true, 1, true);
        static_cast<GJBaseGameLayer*>(pl)->queueButton(1, true, false, 0.0);
        jumping = true;
        jumpHoldFrames = 0;
        jumpsExecuted++;
    }

    void releaseJump(PlayLayer* pl) {
        if (!pl) return;
        pl->handleButton(false, 1, true);
        if (pl->m_player1) pl->m_player1->releaseButton(PlayerButton::Jump);
        static_cast<GJBaseGameLayer*>(pl)->handleButton(false, 1, true);
        static_cast<GJBaseGameLayer*>(pl)->queueButton(1, false, false, 0.0);
        jumping = false;
    }

    void onLevelInit(PlayLayer* pl) {
        if (!pl || !pl->m_level) return;
        currentLevelName = pl->m_level->m_levelName;
        log::info("Level loaded: {}", currentLevelName);
        log::info(">>> Bot ACTIVE - F5 to toggle <<<");
        maxX = 0;
        lastX = 0;
        jumping = false;
        jumpsExecuted = 0;
    }
};

static BotState g_bot;
static int g_tickCount = 0;

class $modify(MyKeyboardDispatcher, cocos2d::CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(cocos2d::enumKeyCodes key, bool down, bool repeat, double delta) {
        if (down && !repeat && key == cocos2d::enumKeyCodes::KEY_F5) {
            g_bot.active = !g_bot.active;
            log::info(">>> F5 - Bot {} <<<", g_bot.active ? "ON" : "OFF");
            Notification::create(
                g_bot.active ? "chillbot: ON" : "chillbot: OFF",
                g_bot.active ? NotificationIcon::Success : NotificationIcon::Info,
                1.0f
            )->show();
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

        if (!g_bot.active) return;
        if (!this->m_player1) return;

        float px = this->m_player1->getPositionX();
        float py = this->m_player1->getPositionY();

        // Детект сброса (X резко уменьшилось — значит смерть+ресет)
        if (px < g_bot.lastX - 100.0f && g_bot.lastX > 200.0f) {
            int deathX = (int)g_bot.lastX;
            g_bot.deathPositions.insert(deathX - 30);
            g_bot.realDeaths++;
            log::info("REAL DEATH at X={} (death #{}, learned!)",
                      deathX, g_bot.realDeaths);
        }
        g_bot.lastX = px;
        if (px > g_bot.maxX) g_bot.maxX = px;

        // Лог раз в секунду
        if (g_tickCount % 60 == 0) {
            log::info("tick #{} X={:.0f} Y={:.1f} maxX={:.0f} jumps={} deaths={}",
                      g_tickCount, px, py, g_bot.maxX,
                      g_bot.jumpsExecuted, g_bot.realDeaths);
        }

        // Логика бота
        bool needJump = g_bot.shouldJump(this);

        if (needJump && !g_bot.jumping) {
            g_bot.executeJump(this);
            log::info("JUMP! at X={:.0f} Y={:.1f} (#{}) ",
                      px, py, g_bot.jumpsExecuted);
        } else if (g_bot.jumping) {
            g_bot.jumpHoldFrames++;
            if (g_bot.jumpHoldFrames > 6 || !needJump) {
                g_bot.releaseJump(this);
            }
        }
    }

    void resetLevel() {
        log::info(">>> RESET maxX={:.0f} jumps={} deaths={} <<<",
                  g_bot.maxX, g_bot.jumpsExecuted, g_bot.realDeaths);
        g_bot.maxX = 0;
        g_bot.lastX = 0;
        g_bot.jumping = false;
        g_bot.jumpsExecuted = 0;
        g_tickCount = 0;
        PlayLayer::resetLevel();
    }

    void levelComplete() {
        log::info(">>> LEVEL COMPLETE! Final maxX={:.0f} <<<", g_bot.maxX);
        PlayLayer::levelComplete();
    }
};

$on_mod(Loaded) {
    log::info("================================================");
    log::info("=== chillbot v1.0.20-FINALBOT loaded ===");
    log::info("=== AUTO-ON, F5 to toggle, real bot logic ===");
    log::info("================================================");
}
