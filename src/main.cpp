#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/UILayer.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <gdr/gdr.hpp>
#include <random>
#include <fstream>

using namespace geode::prelude;

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
        std::normal_distribution<float> nd(0, 0.5f);
        for (int i=0;i<IN;i++) for(int h=0;h<HID;h++) w1[i][h]=nd(rng)*0.5f;
        for(int h=0;h<HID;h++) b1[h]=0;
        for(int h=0;h<HID;h++) for(int o=0;o<OUT;o++) w2[h][o]=nd(rng)*0.5f;
        for(int o=0;o<OUT;o++) b2[o]=0;
    }

    std::array<float,OUT> forward(const AIState& s) {
        float in[IN] = {
            s.x / 10000.f,
            s.y / 1000.f,
            s.velY / 1000.f,
            s.distToObstacle / 500.f,
            s.gamemode / 6.f,
            s.upsideDown ? 1.f : 0.f
        };
        float h[HID];
        for(int j=0;j<HID;j++) {
            float sum = b1[j];
            for(int i=0;i<IN;i++) sum += in[i]*w1[i][j];
            h[j] = sum > 0 ? sum : sum*0.01f; // leaky relu
        }
        std::array<float,OUT> out;
        for(int k=0;k<OUT;k++) {
            float sum = b2[k];
            for(int j=0;j<HID;j++) sum += h[j]*w2[k][j];
            out[k] = 1.f / (1.f + expf(-sum)); // sigmoid
        }
        return out;
    }

    void train(const AIState& s, int action, float reward) {
        // упрощенный Q-learning шаг
        auto pred = forward(s);
        float target[OUT] = { pred[0], pred[1] };
        target[action] = std::clamp(pred[action] + lr * (reward - pred[action]), 0.f, 1.f);
        
        // backprop (сокращенный)
        float in[IN] = { s.x/10000.f, s.y/1000.f, s.velY/1000.f, s.distToObstacle/500.f, s.gamemode/6.f, s.upsideDown?1.f:0.f };
        float h[HID];
        for(int j=0;j<HID;j++) { float sum=b1[j]; for(int i=0;i<IN;i++) sum+=in[i]*w1[i][j]; h[j]=sum>0?sum:sum*0.01f; }
        
        for(int k=0;k<OUT;k++) {
            float err = target[k] - pred[k];
            float grad = err * pred[k] * (1-pred[k]);
            for(int j=0;j<HID;j++) w2[j][k] += lr * grad * h[j];
            b2[k] += lr * grad;
        }
    }

    void save() {
        auto path = Mod::get()->getSaveDir() / "ai_weights.bin";
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
        lr = Mod::get()->getSettingValue<float>("learning-rate");
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
    std::vector<AIState> history;
    std::vector<int> actions;
    float bestPercent = 0;
    int deaths = 0;
    PlayLayer* pl = nullptr;

    bool init() {
        net.load();
        enabled = Mod::get()->getSettingValue<bool>("ai-enabled");
        currentReplay = gdr::Replay("chillbot", 1);
        currentReplay.framerate = 240.0;
        currentReplay.botInfo = {"chillbot", "1.0.0"};
        
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
            for(auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
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
        if(percent > bestPercent && Mod::get()->getSettingValue<bool>("save-replays")) {
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
        for(size_t i=0;i<history.size();i++) net.train(history[i], actions[i], 1.0f);
        saveReplay(percent);
        net.save();
        Notification::create("Уровень пройден ИИ!", NotificationIcon::Success)->show();
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
class $modify(AIUILayer, UILayer) {
    void keyDown(enumKeyCodes key) {
        if (key == enumKeyCodes::KEY_F5) {
            AIBot::get()->toggle();
        }
        UILayer::keyDown(key);
    }
};

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

