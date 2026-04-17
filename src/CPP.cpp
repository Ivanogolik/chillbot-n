#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <vector>
#include <random>
#include <cmath>
#include <fstream>

using namespace geode::prelude;

// =========================================
// НЕЙРОННАЯ СЕТЬ (простая feedforward NN)
// =========================================
struct NeuralNet {
    // Веса слоёв: [входной → скрытый → выходной]
    std::vector<std::vector<float>> w1; // 5 входов → 8 нейронов
    std::vector<float> b1;             // bias скрытого слоя
    std::vector<float> w2;             // 8 → 1 выход (прыгать/нет)
    float b2;

    float fitness = 0.0f; // насколько хорошо прошёл уровень

    NeuralNet() {
        std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

        w1.resize(8, std::vector<float>(5));
        b1.resize(8);
        w2.resize(8);
        b2 = dist(rng);

        for (auto& row : w1)
            for (auto& v : row) v = dist(rng);
        for (auto& v : b1) v = dist(rng);
        for (auto& v : w2) v = dist(rng);
    }

    float relu(float x) { return x > 0 ? x : 0; }
    float sigmoid(float x) { return 1.0f / (1.0f + std::exp(-x)); }

    // Получить решение: прыгать (>0.5) или нет
    bool decide(std::vector<float> inputs) {
        std::vector<float> hidden(8, 0.0f);
        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 5; j++)
                hidden[i] += w1[i][j] * inputs[j];
            hidden[i] = relu(hidden[i] + b1[i]);
        }
        float out = b2;
        for (int i = 0; i < 8; i++)
            out += w2[i] * hidden[i];
        return sigmoid(out) > 0.5f;
    }

    // Мутация весов (эволюция)
    NeuralNet mutate(float rate = 0.1f) {
        NeuralNet child = *this;
        std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<float> dist(-0.3f, 0.3f);
        std::uniform_real_distribution<float> chance(0.0f, 1.0f);

        for (auto& row : child.w1)
            for (auto& v : row)
                if (chance(rng) < rate) v += dist(rng);
        for (auto& v : child.b1)
            if (chance(rng) < rate) v += dist(rng);
        for (auto& v : child.w2)
            if (chance(rng) < rate) v += dist(rng);
        if (chance(rng) < rate) child.b2 += dist(rng);

        return child;
    }
};

// =========================================
// ПОПУЛЯЦИЯ (генетический алгоритм)
// =========================================
struct Population {
    static const int SIZE = 50;
    std::vector<NeuralNet> bots;
    int generation = 0;
    int currentBot = 0;

    Population() { bots.resize(SIZE); }

    NeuralNet& current() { return bots[currentBot]; }

    // Отбор лучших и создание следующего поколения
    void evolve() {
        std::sort(bots.begin(), bots.end(),
            [](const NeuralNet& a, const NeuralNet& b) {
                return a.fitness > b.fitness;
            });

        log::info("Поколение {} | Лучший fitness: {:.2f}",
            generation, bots[0].fitness);

        std::vector<NeuralNet> next;
        // Оставляем топ-10
        for (int i = 0; i < 10; i++) next.push_back(bots[i]);
        // Мутируем их для заполнения популяции
        while ((int)next.size() < SIZE)
            next.push_back(bots[next.size() % 10].mutate(0.15f));

        bots = next;
        for (auto& b : bots) b.fitness = 0.0f;
        generation++;
        currentBot = 0;
    }
};

// =========================================
// ХУКИ В PLAYLAYER
// =========================================
static Population g_pop;
static bool g_botEnabled = false;

struct AIBotLayer : Modify<AIBotLayer, PlayLayer> {

    // Вызывается каждый кадр
    void update(float dt) {
        PlayLayer::update(dt);
        if (!g_botEnabled) return;

        auto player = m_player1;
        if (!player) return;

        // === ВХОДНЫЕ ДАННЫЕ ДЛЯ НЕЙРОСЕТИ ===
        // 1. Y позиция игрока (нормализованная)
        float playerY = player->getPositionY() / 300.0f;
        // 2. Y скорость
        float velY = player->m_yVelocity / 15.0f;
        // 3. На земле или нет
        float onGround = player->m_isOnGround ? 1.0f : 0.0f;
        // 4-5. Расстояние до ближайшего препятствия (упрощённо)
        float obstDist = 0.5f;
        float obstHeight = 0.5f;

        // Ищем ближайший объект перед игроком
        auto objects = m_objects;
        float minDist = 9999.0f;
        float closestY = 0.0f;
        if (objects) {
            for (auto obj : CCArrayExt<GameObject*>(objects)) {
                if (!obj) continue;
                float ox = obj->getPositionX();
                float px = player->getPositionX();
                float dist = ox - px;
                if (dist > 0 && dist < minDist && dist < 400.0f) {
                    minDist = dist;
                    closestY = obj->getPositionY();
                }
            }
        }
        obstDist   = std::min(minDist / 400.0f, 1.0f);
        obstHeight = closestY / 300.0f;

        std::vector<float> inputs = {
            playerY, velY, onGround, obstDist, obstHeight
        };

        // === РЕШЕНИЕ НЕЙРОСЕТИ ===
        bool shouldJump = g_pop.current().decide(inputs);
        if (shouldJump) {
            player->pushButton(PlayerButton::Jump);
            player->releaseButton(PlayerButton::Jump);
        }

        // Fitness = расстояние пройденного
        g_pop.current().fitness = player->getPositionX();
    }

    // Смерть игрока → следующий бот или эволюция
    void playerDied(PlayerObject* p, bool b) {
        PlayLayer::playerDied(p, b);
        if (!g_botEnabled) return;

        g_pop.currentBot++;
        if (g_pop.currentBot >= Population::SIZE) {
            g_pop.evolve();
        }
        // Перезапуск уровня
        this->resetLevel();
    }

    // Победа — максимальный fitness
    void levelComplete() {
        if (g_botEnabled) {
            g_pop.current().fitness = 999999.0f;
            g_pop.evolve();
        }
        PlayLayer::levelComplete();
    }
};

// =========================================
// ТОЧКА ВХОДА МОДА
// =========================================
$on_mod(Loaded) {
    log::info("GD AI Bot загружен! Поколение: {}",
        g_pop.generation);
    // Включить бота: g_botEnabled = true;
    // (можно добавить кнопку в UI через Geode настройки)
}