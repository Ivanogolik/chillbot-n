// ============================================================
// chillbot v1.0.24 by Ivanogolik
// Cleaned up from v1.0.22-MEGABOT:
//   - Removed conflicting jump methods (kept 2 that actually work)
//   - Fixed isOnGround check before every jump
//   - Removed dead GDReplayFormat dependency
//   - Removed keyboard simulation (caused conflicts with input block)
//   - Cleaner object scanner (spikes + orbs)
//   - Death detection via X-position reset (verified working)
//   - K key: debug object scanner toggle
//   - Macro save/load (gdai format)
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
#include <Geode/utils/cocos.hpp>
#include <cocos2d.h>

#include <set>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <cmath>
#include <algorithm>

// --- Убиваем конфликтующие макросы Windows SDK ---
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

// ============================================================
//  Глобальные флаги
// ============================================================

static bool g_simulatingKey = false;
static uint64_t g_tickCount = 0;

// ============================================================
//  Спайк ID — все известные объекты-убийцы в GD 2.2
// ============================================================
static const std::set<int> SPIKE_IDS = {
    8, 9, 39, 88, 89, 98, 99, 100,
    103, 177, 178, 179, 216, 217, 218, 219,
    392, 397, 446, 447, 458, 459
};

// Орбы (jump pads/orbs — игрок должен нажать при касании)
static const std::set<int> ORB_IDS = {
    36,   // Жёлтый orb (jump)
    84,   // Синий orb (gravity flip)
    141,  // Красный orb (boost down)
    1022, // Зелёный orb (push up)
    1330, // Чёрный orb (push down)
};

// ============================================================
//  Утилита: путь к файлу макроса
// ============================================================
static std::filesystem::path getMacroPath() {
    auto dir = Mod::get()->getSaveDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir / "chillbot_macro.gdai";
}

// ============================================================
//  Состояние бота
// ============================================================
struct BotState {
    // --- Управление ---
    bool active         = true;
    bool blockInput     = true;
    bool debugScanner   = false;

    // --- Статистика сессии ---
    int  realDeaths     = 0;
    int  jumpsExecuted  = 0;
    int  jumpCooldown   = 0;
    float lastX         = 0.0f;
    float maxX          = 0.0f;
    std::string levelName;

    // --- База знаний: X-позиции смерти (сохраняются на диск) ---
    std::set<int> deathPositions;

    // ─────────────────────────────────────────
    //  Проверка: игрок на земле?
    //  Используем m_yVelocity ≈ 0
    // ─────────────────────────────────────────
    bool isOnGround(PlayerObject* p) const {
        if (!p) return false;
        return std::abs(p->m_yVelocity) < 1.5;
    }

    // ─────────────────────────────────────────
    //  Нужно ли прыгать прямо сейчас?
    // ─────────────────────────────────────────
    bool shouldJump(GJBaseGameLayer* gl) {
        if (!gl || !gl->m_player1) return false;
        auto* p  = gl->m_player1;
        float px = p->getPositionX();
        float py = p->getPositionY();

        // 1) Если мы близко к запомненной позиции смерти — прыгать заранее
        for (int dx : deathPositions) {
            float dist = (float)dx - px;
            if (dist > 0.0f && dist < 120.0f) {
                if (debugScanner)
                    log::debug("[scanner] Near learned death X={} dist={:.1f}", dx, dist);
                return true;
            }
        }

        // 2) Сканируем объекты впереди
        if (!gl->m_objects) return false;
        int count = (int)gl->m_objects->count();

        for (int i = 0; i < count; i++) {
            auto* obj = typeinfo_cast<GameObject*>(gl->m_objects->objectAtIndex(i));
            if (!obj) continue;

            float ox = obj->getPositionX();
            float oy = obj->getPositionY();
            float dx = ox - px;   // положительно = впереди
            float dy = std::abs(oy - py);

            // Только объекты впереди в диапазоне 10–200 px, по высоте ±100 px
            if (dx < 10.0f || dx > 200.0f) continue;
            if (dy > 100.0f)               continue;

            int id = obj->m_objectID;

            // Шипы — реагируем раньше (60–160 px до объекта)
            if (SPIKE_IDS.count(id) && dx > 60.0f) {
                if (debugScanner)
                    log::debug("[scanner] Spike id={} dx={:.1f} dy={:.1f}", id, dx, dy);
                return true;
            }

            // Орбы — реагируем очень близко (10–50 px)
            if (ORB_IDS.count(id) && dx < 50.0f) {
                if (debugScanner)
                    log::debug("[scanner] Orb id={} dx={:.1f} dy={:.1f}", id, dx, dy);
                return true;
            }
        }

        return false;
    }

    // ─────────────────────────────────────────
    //  Выполнить прыжок (2 надёжных метода)
    // ─────────────────────────────────────────
    void executeJump(PlayLayer* pl) {
        if (!pl || !pl->m_player1) return;

        // Метод A — стандартный handleButton (как у EchoBot / xdBot)
        pl->handleButton(true, 1, true);

        // Метод B — прямая физика (гарантия, если handleButton игнорируется)
        pl->m_player1->m_yVelocity = 16.0;

        jumpsExecuted++;
    }

    void releaseJump(PlayLayer* pl) {
        if (!pl) return;
        pl->handleButton(false, 1, true);
        if (pl->m_player1)
            pl->m_player1->releaseButton(PlayerButton::Jump);
    }

    // ─────────────────────────────────────────
    //  Инициализация при входе на уровень
    // ─────────────────────────────────────────
    void onLevelInit(PlayLayer* pl) {
        if (!pl || !pl->m_level) return;

        levelName     = pl->m_level->m_levelName;
        lastX         = 0.0f;
        maxX          = 0.0f;
        realDeaths    = 0;
        jumpsExecuted = 0;
        jumpCooldown  = 0;

        loadMacro();

        log::info("=== Level: {} | Bot: {} | InputBlock: {} | Deaths known: {} ===",
            levelName,
            active    ? "ON"  : "OFF",
            blockInput ? "ON" : "OFF",
            (int)deathPositions.size()
        );
    }

    // ─────────────────────────────────────────
    //  Сохранение макроса
    // ─────────────────────────────────────────
    void saveMacro() {
        auto path = getMacroPath();
        std::ofstream f(path);
        if (!f.is_open()) {
            log::warn("[chillbot] Failed to open macro file for writing: {}", path.u8string());
            return;
        }
        f << "GDAI1\n";
        f << "LEVEL " << levelName << "\n";
        f << "BEST "  << maxX      << "\n";
        f << "DEATHS " << realDeaths << "\n";
        for (int dx : deathPositions)
            f << "DEATH " << dx << "\n";
        f.close();
        log::info("[chillbot] Macro saved: {} deaths, maxX={:.0f}", (int)deathPositions.size(), maxX);
    }

    // ─────────────────────────────────────────
    //  Загрузка макроса
    // ─────────────────────────────────────────
    void loadMacro() {
        auto path = getMacroPath();
        std::ifstream f(path);
        if (!f.is_open()) {
            log::info("[chillbot] No macro file found — starting fresh.");
            return;
        }
        std::string line;
        deathPositions.clear();
        while (std::getline(f, line)) {
            if (line.rfind("DEATH ", 0) == 0) {
                int x = std::stoi(line.substr(6));
                deathPositions.insert(x);
            }
        }
        f.close();
        log::info("[chillbot] Macro loaded: {} death positions known.", (int)deathPositions.size());
    }
};

static BotState g_bot;

// ============================================================
//  Хук клавиатуры
// ============================================================
class $modify(BotKeyboard, cocos2d::CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(
        cocos2d::enumKeyCodes key,
        bool  down,
        bool  repeat,
        double delta
    ) {
        // F5 — вкл/выкл бота
        if (down && !repeat && key == cocos2d::enumKeyCodes::KEY_F5) {
            g_bot.active = !g_bot.active;
            log::info("[chillbot] Bot {}", g_bot.active ? "ENABLED" : "DISABLED");
            auto* scene = cocos2d::CCDirector::sharedDirector()->getRunningScene();
            if (scene) {
                auto* label = cocos2d::CCLabelTTF::create(
                    g_bot.active ? "chillbot: ON" : "chillbot: OFF",
                    "chatFont.fnt",
                    24.0f
                );
                if (label) {
                    auto* sz = &cocos2d::CCDirector::sharedDirector()->getWinSize();
                    label->setPosition({sz->width / 2.0f, sz->height - 40.0f});
                    label->setColor(g_bot.active ? cocos2d::ccGREEN : cocos2d::ccRED);
                    label->setTag(9991);
                    if (scene->getChildByTag(9991)) scene->removeChildByTag(9991, true);
                    scene->addChild(label, 999);
                    label->runAction(cocos2d::CCSequence::create(
                        cocos2d::CCDelayTime::create(1.5f),
                        cocos2d::CCFadeOut::create(0.5f),
                        cocos2d::CCRemoveSelf::create(),
                        nullptr
                    ));
                }
            }
            return true;
        }

        // L — вкл/выкл блокировки ввода игрока
        if (down && !repeat && key == cocos2d::enumKeyCodes::KEY_L) {
            g_bot.blockInput = !g_bot.blockInput;
            log::info("[chillbot] Player input {}", g_bot.blockInput ? "BLOCKED" : "ALLOWED");
            return true;
        }

        // K — вкл/выкл debug сканера объектов
        if (down && !repeat && key == cocos2d::enumKeyCodes::KEY_K) {
            g_bot.debugScanner = !g_bot.debugScanner;
            log::info("[chillbot] Object debug scanner {}", g_bot.debugScanner ? "ON" : "OFF");
            return true;
        }

        // Блокировка ввода игрока (Space / Up / W) когда бот активен
        if (g_bot.active && g_bot.blockInput && !g_simulatingKey) {
            if (key == cocos2d::enumKeyCodes::KEY_Space ||
                key == cocos2d::enumKeyCodes::KEY_Up    ||
                key == cocos2d::enumKeyCodes::KEY_W)
            {
                return true;  // Поглощаем нажатие
            }
        }

        return cocos2d::CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, delta);
    }
};

// ============================================================
//  Хук PlayLayer
// ============================================================
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

        if (!g_bot.active)    return;
        if (!this->m_player1) return;

        auto* p  = this->m_player1;
        float px = p->getPositionX();
        float py = p->getPositionY();

        // --- Детект смерти через резкий сброс X ---
        if (px < g_bot.lastX - 200.0f) {
            int deathX = (int)g_bot.lastX;
            int learnX = deathX - 40;   // прыгаем чуть раньше места смерти
            g_bot.deathPositions.insert(learnX);
            g_bot.realDeaths++;
            log::info("[chillbot] DEATH #{} at X={} → learned X={}", g_bot.realDeaths, deathX, learnX);
            g_bot.saveMacro();
        }
        g_bot.lastX = px;
        if (px > g_bot.maxX) g_bot.maxX = px;

        // --- Лог каждые 60 тиков (~1 сек) ---
        if (g_tickCount % 60 == 0) {
            log::info("[chillbot] tick={} X={:.0f} Y={:.1f} vY={:.2f} maxX={:.0f} jumps={} deaths={}",
                g_tickCount, px, py, p->m_yVelocity, g_bot.maxX, g_bot.jumpsExecuted, g_bot.realDeaths);
        }

        // --- Cooldown ---
        if (g_bot.jumpCooldown > 0) {
            g_bot.jumpCooldown--;

            // Отпускаем кнопку через 8 тиков после прыжка
            if (g_bot.jumpCooldown == 8) {
                g_simulatingKey = true;
                g_bot.releaseJump(this);
                g_simulatingKey = false;
            }
            return;
        }

        // --- Проверяем нужен ли прыжок ---
        bool onGround = g_bot.isOnGround(p);
        bool needJump = g_bot.shouldJump(this);

        if (needJump && onGround) {
            float vyBefore = p->m_yVelocity;
            g_simulatingKey = true;
            g_bot.executeJump(this);
            g_simulatingKey = false;
            log::info("[chillbot] JUMP! X={:.0f} Y={:.1f} vY: {:.2f} → {:.2f} (jump #{})",
                px, py, vyBefore, p->m_yVelocity, g_bot.jumpsExecuted);
            g_bot.jumpCooldown = 15;  // ~0.25 сек между прыжками
        }
    }

    void resetLevel() {
        log::info("[chillbot] RESET | maxX={:.0f} jumps={} deaths={}",
            g_bot.maxX, g_bot.jumpsExecuted, g_bot.realDeaths);
        g_bot.jumpCooldown = 0;
        PlayLayer::resetLevel();
    }

    void levelComplete() {
        log::info("[chillbot] *** LEVEL COMPLETE! maxX={:.0f} jumps={} deaths={} ***",
            g_bot.maxX, g_bot.jumpsExecuted, g_bot.realDeaths);
        g_bot.saveMacro();
        PlayLayer::levelComplete();
    }
};

// ============================================================
//  Инициализация мода
// ============================================================
$on_mod(Loaded) {
    // Читаем настройки из mod.json
    g_bot.active     = Mod::get()->getSettingValue<bool>("bot-enabled");
    g_bot.blockInput = Mod::get()->getSettingValue<bool>("block-player-input");

    log::info("================================================");
    log::info("=== chillbot v1.0.24 loaded                 ===");
    log::info("=== F5: bot on/off | L: block input         ===");
    log::info("=== K: debug scanner | auto-saves macro     ===");
    log::info("=== Bot: {} | InputBlock: {}               ===",
        g_bot.active    ? "ON"  : "OFF",
        g_bot.blockInput ? "ON" : "OFF");
    log::info("================================================");
}
