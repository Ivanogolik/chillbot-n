// ============================================================
// chillbot v1.0.22-MEGABOT by Ivanogolik
// 8 jump methods + simulated keys + player input lock + overlay
// Verified against xdBot 3.4.0 + EchoBot + Geode 5.6.1 docs
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
    bool active = true;       // включён по умолчанию
    bool blockPlayerInput = true;  // блокировать ввод игрока когда бот активен
    int jumpCooldown = 0;
    int realDeaths = 0;
    float lastX = 0.0f;
    float maxX = 0.0f;
    int jumpsExecuted = 0;
    std::string currentLevelName = "";
    std::set<int> deathPositions;

    bool shouldJump(GJBaseGameLayer* gl) {
        if (!gl || !gl->m_player1) return false;
        auto player = gl->m_player1;
        float px = player->getPositionX();
        float py = player->getPositionY();

        // 1) Запомненные смертельные точки
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
            if (dx < 5.0f || dx > 200.0f) continue;
            if (dy > 120.0f) continue;

            int id = obj->m_objectID;
            // Шипы
            if (id == 8 || id == 9 || id == 39 || id == 103 ||
                id == 216 || id == 217 || id == 218 || id == 219 ||
                id == 392 || id == 397 || id == 458 || id == 459 ||
                id == 446 || id == 447 || id == 88 || id == 89 ||
                id == 98 || id == 99 || id == 100 || id == 177 ||
                id == 178 || id == 179) {
                if (dx < 90.0f) return true;
            }
            // Блоки
            if (id == 1 || id == 2 || id == 3 || id == 4 || id == 5 ||
                id == 6 || id == 7 || id == 40 || id == 41 || id == 42 ||
                id == 43 || id == 44 || id == 467 || id == 468) {
                if (dx < 70.0f && dy < 60.0f) return true;
            }
        }
        return false;
    }

    // ВСЕ 8 методов прыжка одновременно — один точно сработает
    void executeJump(PlayLayer* pl) {
        if (!pl || !pl->m_player1) return;

        // Метод 1: PlayLayer::handleButton
        pl->handleButton(true, 1, true);
        // Метод 2: GJBaseGameLayer::handleButton
        static_cast<GJBaseGameLayer*>(pl)->handleButton(true, 1, true);
        // Метод 3: PlayerObject::pushButton
        pl->m_player1->pushButton(PlayerButton::Jump);
        // Метод 4: queueButton (buffered input)
        static_cast<GJBaseGameLayer*>(pl)->queueButton(1, true, false, 0.0);
        // Метод 5: ПРЯМАЯ ФИЗИКА — гарантированный подъём
        pl->m_player1->m_yVelocity = 16.0;
        // Метод 6: propellPlayer (специальная функция прыжка)
        pl->m_player1->propellPlayer(16.0f, false, 0);
        // Метод 7: Симуляция Space через CCKeyboardDispatcher
        auto kd = cocos2d::CCDirector::sharedDirector()->getKeyboardDispatcher();
        if (kd) {
            kd->dispatchKeyboardMSG(cocos2d::enumKeyCodes::KEY_Space, true, false, 0.0);
        }
        // Метод 8: Симуляция стрелки вверх
        if (kd) {
            kd->dispatchKeyboardMSG(cocos2d::enumKeyCodes::KEY_Up, true, false, 0.0);
        }

        jumpsExecuted++;
    }

    void releaseJump(PlayLayer* pl) {
        if (!pl) return;
        pl->handleButton(false, 1, true);
        static_cast<GJBaseGameLayer*>(pl)->handleButton(false, 1, true);
        if (pl->m_player1) pl->m_player1->releaseButton(PlayerButton::Jump);
        static_cast<GJBaseGameLayer*>(pl)->queueButton(1, false, false, 0.0);
        auto kd = cocos2d::CCDirector::sharedDirector()->getKeyboardDispatcher();
        if (kd) {
            kd->dispatchKeyboardMSG(cocos2d::enumKeyCodes::KEY_Space, false, false, 0.0);
            kd->dispatchKeyboardMSG(cocos2d::enumKeyCodes::KEY_Up, false, false, 0.0);
        }
    }

    void onLevelInit(PlayLayer* pl) {
        if (!pl || !pl->m_level) return;
        currentLevelName = pl->m_level->m_levelName;
        log::info("Level loaded: {}", currentLevelName);
        log::info(">>> Bot {} | Player input {} <<<",
                  active ? "ON" : "OFF",
                  blockPlayerInput ? "BLOCKED" : "ALLOWED");
        maxX = 0;
        lastX = 0;
        jumpCooldown = 0;
        jumpsExecuted = 0;
    }
};

static BotState g_bot;
static int g_tickCount = 0;
// Флаг чтобы избежать рекурсии когда мы сами симулируем нажатие клавиш
static bool g_simulatingKey = false;

class $modify(MyKeyboardDispatcher, cocos2d::CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(cocos2d::enumKeyCodes key, bool down, bool repeat, double delta) {
        // F5 toggle - всегда работает
        if (down && !repeat && key == cocos2d::enumKeyCodes::KEY_F5) {
            g_bot.active = !g_bot.active;
            log::info(">>> F5 - Bot {} <<<", g_bot.active ? "ON" : "OFF");
            Notification::create(
                g_bot.active ? "chillbot: ON" : "chillbot: OFF",
                g_bot.active ? NotificationIcon::Success : NotificationIcon::Info,
                1.0f
            )->show();
            return true;
        }
        // L toggle - блокировка ввода игрока
        if (down && !repeat && key == cocos2d::enumKeyCodes::KEY_L) {
            g_bot.blockPlayerInput = !g_bot.blockPlayerInput;
            log::info(">>> L - Player input {} <<<",
                      g_bot.blockPlayerInput ? "BLOCKED" : "ALLOWED");
            Notification::create(
                g_bot.blockPlayerInput ? "Player input: BLOCKED" : "Player input: ALLOWED",
                NotificationIcon::Info, 1.0f
            )->show();
            return true;
        }

        // БЛОКИРОВКА ИГРОКА — если бот активен и блокировка включена
        // НЕ блокируем когда мы сами симулируем нажатие
        if (g_bot.active && g_bot.blockPlayerInput && !g_simulatingKey) {
            if (key == cocos2d::enumKeyCodes::KEY_Space ||
                key == cocos2d::enumKeyCodes::KEY_Up ||
                key == cocos2d::enumKeyCodes::KEY_W) {
                // Игрок нажал Space/Up/W - игнорируем его ввод
                return true;  // не передаём в игру
            }
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

        // Детект смерти через сброс X
        if (px < g_bot.lastX - 100.0f && g_bot.lastX > 200.0f) {
            int deathX = (int)g_bot.lastX;
            g_bot.deathPositions.insert(deathX - 30);
            g_bot.realDeaths++;
            log::info("REAL DEATH at X={} (death #{}, learned!)",
                      deathX, g_bot.realDeaths);
        }
        g_bot.lastX = px;
        if (px > g_bot.maxX) g_bot.maxX = px;

        if (g_tickCount % 60 == 0) {
            float vy = this->m_player1->m_yVelocity;
            log::info("tick #{} X={:.0f} Y={:.1f} vY={:.2f} maxX={:.0f} jumps={} deaths={}",
                      g_tickCount, px, py, vy, g_bot.maxX,
                      g_bot.jumpsExecuted, g_bot.realDeaths);
        }

        if (g_bot.jumpCooldown > 0) {
            g_bot.jumpCooldown--;
        }

        bool needJump = g_bot.shouldJump(this);
        if (needJump && g_bot.jumpCooldown == 0) {
            // Устанавливаем флаг чтобы симулированные клавиши не блокировались нашим же хуком
            g_simulatingKey = true;
            g_bot.executeJump(this);
            g_simulatingKey = false;

            log::info("JUMP! X={:.0f} Y={:.1f} vY={:.2f} (#{})",
                      px, py, this->m_player1->m_yVelocity, g_bot.jumpsExecuted);
            g_bot.jumpCooldown = 12;  // ~0.2 сек между прыжками
        }

        // Отпускаем кнопки через 6 кадров после прыжка
        if (g_bot.jumpCooldown == 6) {
            g_simulatingKey = true;
            g_bot.releaseJump(this);
            g_simulatingKey = false;
        }
    }

    void resetLevel() {
        log::info(">>> RESET maxX={:.0f} jumps={} deaths={} <<<",
                  g_bot.maxX, g_bot.jumpsExecuted, g_bot.realDeaths);
        g_bot.maxX = 0;
        g_bot.lastX = 0;
        g_bot.jumpCooldown = 0;
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
    log::info("=== chillbot v1.0.22-MEGABOT loaded ===");
    log::info("=== 8 jump methods + key simulation ===");
    log::info("=== F5: bot toggle | L: player input lock ===");
    log::info("=== AUTO-ON, player input BLOCKED by default ===");
    log::info("================================================");
}
