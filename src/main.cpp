#include 
#include 
#include 
#include 
#include 
#include 
#include 
#include 

using namespace geode::prelude;

#ifdef GEODE_IS_WINDOWS
#include 
using namespace keybinds;
#endif

// ========== ИИ ЯДРО ==========
struct AIState {
    float x, y, velY, rot;
    float distToObstacle;
    int gamemode; // 0 cube, 1 ship, 2 ball, 3 ufo, 4 wave, 5 robot, 6 spider
    bool upsideDown;
};

class NeuralNet {
public:
    static constexpr int IN = 6, HID = 12, OUT = 2;
    float w1[IN][HID], b1[HID];
    float w2[HID][OUT], b2[OUT];
    float lr = 0.15f;

    NeuralNet() { init(); }

    void init() {
        std::mt19937 rng(std::random_device{}());
        std::normal_distribution nd(0, 0.5f);
        for (int i=0;i forward(const AIState& s) {
        float in[IN] = {
            s.x / 10000.f,
            s.y / 1000.f,
            s.velY / 1000.f,
            s.distToObstacle / 500.f,
            s.gamemode / 6.f,
            s.upsideDown ? 1.f : 0.f
        };
        float h[HID];
        for(int j=0;j 0 ? sum : sum*0.01f; // leaky relu
        }
        std::array out;
        for(int k=0;k0?sum:sum*0.01f; }
        
        for(int k=0;kgetSaveDir() / "ai_weights.bin";
        std::ofstream f(path, std::ios::binary);
        f.write((char*)w1, sizeof w1); f.write((char*)b1, sizeof b1);
        f.write((char*)w2, sizeof w2); f.write((char*)b2, sizeof b2);
    }
    void load() {
        auto path = Mod::get()->getSaveDir() / "ai_weights.bin";
        if(!std::filesystem::exists(path)) return;
        std::ifstream f(path, std::ios::binary);
        f.read((char*)w1, sizeof w1); f.read((char*)b1, sizeof b1);
        f.read((char*)w2, sizeof w2); f.read((char*)b2, sizeof b2);
        lr = Mod::get()->getSettingValue("learning-rate");
    }
};

class AIBot : public CCNode {
public:
    static AIBot* get() {
        static AIBot* inst = nullptr;
        if(!inst) { inst = new AIBot(); inst->init(); }
        return inst;
    }

    bool enabled = false;
    NeuralNet net;
    gdr::Replay currentReplay;
    std::vector history;
    std::vector actions;
    float bestPercent = 0;
    int deaths = 0;
    PlayLayer* pl = nullptr;

    bool init() {
        net.load();
        enabled = Mod::get()->getSettingValue("ai-enabled");
        currentReplay = gdr::Replay("chillbot", 1);
        currentReplay.framerate = 240.0;
        currentReplay.botInfo = {"chillbot", "1.0.0"};
        
        // DevTools интеграция
        #ifdef GEODE_DEVTOOLS
        if (Loader::get()->isModLoaded("geode.devtools")) {
            devtools::waitForDevTools([]{
                devtools::registerNode([](AIBot* bot){
                    devtools::label("chillbot");
                    devtools::checkbox("Enabled", &bot->enabled);
                    devtools::property("Best %", bot->bestPercent);
                    devtools::property("Deaths", bot->deaths);
                    devtools::button("Save Weights", []{ AIBot::get()->net.save(); });
                });
            });
        }
        #endif

        scheduleUpdate();
        return true;
    }

    void toggle() {
        enabled = !enabled;
        Mod::get()->setSettingValue("ai-enabled", enabled);
        Notification::create(
            enabled ? "AI BOT ВКЛ" : "AI BOT ВЫКЛ",
            NotificationIcon::Success
        )->show();
    }

    AIState getState() {
        if(!pl || !pl->m_player1) return {};
        auto p = pl->m_player1;
        AIState s;
        s.x = p->getPositionX();
        s.y = p->getPositionY();
        s.velY = p->m_yVelocity;
        s.rot = p->getRotation();
        s.gamemode = (int)p->m_playerMode;
        s.upsideDown = p->m_isUpsideDown;
        
        // расстояние до ближайшего препятствия
        s.distToObstacle = 500.f;
        if(pl->m_objects) {
            float minDist = 9999;
            for(auto obj : CCArrayExt(pl->m_objects)) {
                if(!obj || obj->m_objectType != GameObjectType::Hazard) continue;
                float d = obj->getPositionX() - s.x;
                if(d > 0 && d < minDist) minDist = d;
            }
            if(minDist < 9999) s.distToObstacle = minDist;
        }
        return s;
    }

    void update(float dt) override {
        if(!enabled || !pl || pl->m_isDead || pl->m_hasCompletedLevel) return;

        auto state = getState();
        auto out = net.forward(state);
        bool shouldClick = out[1] > out[0] + 0.1f; // порог
        
        static bool lastClick = false;
        if(shouldClick != lastClick) {
            pl->m_gameState.m_currentProgress = pl->getCurrentPercent();
            if(shouldClick) {
                pl->handleButton(true, 1, true);
                currentReplay.inputs.push_back({(int)(pl->m_gameState.m_currentProgress * 240), true, false});
            } else {
                pl->handleButton(false, 1, true);
                currentReplay.inputs.push_back({(int)(pl->m_gameState.m_currentProgress * 240), false, false});
            }
            lastClick = shouldClick;
            history.push_back(state);
            actions.push_back(shouldClick ? 1 : 0);
        }
    }

    void onDeath() {
        if(!enabled || !pl) return;
        deaths++;
        float percent = pl->getCurrentPercent();
        
        // обучение: наказываем последние 60 кадров
        for(int i = (int)history.size()-1; i >= 0 && i > (int)history.size()-60; i--) {
            float reward = -1.0f * (1.0f - (history.size()-1-i)/60.f);
            net.train(history[i], actions[i], reward);
        }
        
        history.clear(); actions.clear();
        
        // сохраняем если рекорд
        if(percent > bestPercent && Mod::get()->getSettingValue("save-replays")) {
            bestPercent = percent;
            currentReplay.duration = pl->m_levelLength;
            currentReplay.levelInfo = { pl->m_level->m_levelID, pl->m_level->m_levelName };
            saveReplay(percent);
        }
        
        currentReplay.inputs.clear();
        if(deaths % 10 == 0) net.save();
    }

    void onWin() {
        if(!enabled) return;
        float percent = 100.f;
        // вознаграждаем всю историю
        for(size_t i=0;ishow();
    }

    void saveReplay(float percent) {
        auto dir = Mod::get()->getSaveDir() / "replays";
        std::filesystem::create_directories(dir);
        std::string name = fmt::format("{}_ai_{:.2f}.gdr2", pl->m_level->m_levelName, percent);
        auto path = dir / name;
        
        currentReplay.author = "AI Bot";
        currentReplay.description = fmt::format("AI progress {:.2f}%, deaths: {}", percent, deaths);
        
        std::ofstream file(path, std::ios::binary);
        auto data = currentReplay.exportData();
        file.write((char*)data.data(), data.size());
        
        log::info("Replay saved: {}", path.string());
    }
};

// ========== ХУКИ ==========
class $modify(AIPlayLayer, PlayLayer) {
    bool init(GJGameLevel* lvl, bool useReplay, bool dontCreate) {
        if(!PlayLayer::init(lvl, useReplay, dontCreate)) return false;
        AIBot::get()->pl = this;
        AIBot::get()->history.clear();
        AIBot::get()->actions.clear();
        return true;
    }

    void update(float dt) {
        PlayLayer::update(dt);
        if(AIBot::get()->pl == this) AIBot::get()->update(dt);
    }

    void destroyPlayer(PlayerObject* p, GameObject* obj) {
        PlayLayer::destroyPlayer(p, obj);
        if(!m_isPracticeMode) AIBot::get()->onDeath();
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        AIBot::get()->onWin();
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        AIBot::get()->currentReplay.inputs.clear();
    }
};

class $modify(AIPlayer, PlayerObject) {
    // BetterEdit интеграция - показываем траекторию ИИ
    void update(float dt) {
        PlayerObject::update(dt);
        if(Loader::get()->isModLoaded("hjfod.betteredit") && AIBot::get()->enabled) {
            // рисуем предсказание
        }
    }
};

// ========== КЕЙБИНД F5 ==========
$execute {
    // Регистрируем бинд через новый Geode API (2.208+)
    BindManager::get()->registerBindable({
        "toggle-ai"_spr,
        "Включить ИИ бота",
        "Запуск/остановка обучения",
        { Keybind::create(KEY_F5) },
        "chillbot/Управление"
    });

    new EventListener([](InvokeBindEvent* e){
        if(e->isDown()) AIBot::get()->toggle();
        return ListenerResult::Stop;
    }, InvokeBindFilter(nullptr, "toggle-ai"_spr));

    // Добавляем в сцену
    std::thread([]{
        std::this_thread::sleep_for(std::chrono::seconds(1));
        Loader::get()->queueInMainThread([]{
            if(auto scene = CCDirector::sharedDirector()->getRunningScene())
                scene->addChild(AIBot::get());
        });
    }).detach();
}
