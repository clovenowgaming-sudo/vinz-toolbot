#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include "Bot.hpp"

using namespace geode::prelude;

namespace {

// Deteksi checkpoint baru di practice mode (dipanggil tiap frame render, sebelum physics jalan).
// Di titik ini belum ada step physics sejak checkpoint dibuat, jadi frame + state pemain = state checkpoint.
void detectCheckpoint(PlayLayer* pl) {
    auto& snaps = checkpointSnaps();
    auto& lastCount = lastCheckpointCount();
    auto arr = pl->m_checkpointArray;
    size_t count = arr ? arr->count() : 0;
    if (count == 0) {
        snaps.clear();
        lastCount = 0;
        return;
    }
    void* last = arr->lastObject();
    if (count > lastCount || snaps.find(last) == snaps.end()) {
        CheckpointSnap cp;
        cp.frame = bot().frame;
        cp.p1 = snapOf(pl->m_player1);
        cp.p2 = snapOf(pl->m_player2);
        snaps[last] = cp;
    }
    lastCount = count;
}

void replayInput(GJBaseGameLayer* layer, PhysicsInput const& in) {
    auto& b = bot();
    PlayerObject* p = in.player2 ? layer->m_player2 : layer->m_player1;

    // Frame fix: samakan posisi / rotasi / kecepatan dengan saat direkam
    if (p && Mod::get()->getSettingValue<bool>("frame-fix")) {
        bool hasData = in.xPosition != 0.f || in.yPosition != 0.f;
        float dx = std::abs(p->getPositionX() - in.xPosition);
        float dy = std::abs(p->getPositionY() - in.yPosition);
        // kalau selisihnya sangat jauh berarti macro-nya bukan buat level ini, jangan dipaksa
        if (hasData && dx < 60.f && dy < 60.f) {
            PlayerSnap s;
            s.x = in.xPosition;
            s.y = in.yPosition;
            s.rot = in.rotation;
            s.yv = in.yVelocity;
            applySnap(p, s);
        }
    }

    b.injecting = true;
    layer->handleButton(in.down, in.button, !in.player2);
    b.injecting = false;
}

} // namespace

class $modify(VBotGameLayer, GJBaseGameLayer) {
    // Dipanggil sekali tiap physics step -> dari sinilah frame dihitung dan input dimainkan.
    void processCommands(float dt) {
        if (!isPlayLayer(this)) {
            GJBaseGameLayer::processCommands(dt);
            return;
        }
        auto& b = bot();
        if (b.state == BotState::Replaying) {
            b.pump([this](PhysicsInput const& in) { replayInput(this, in); });
        }
        GJBaseGameLayer::processCommands(dt);
        b.frame++;
    }

    // Semua input (klik user ataupun dari bot) lewat sini.
    void handleButton(bool down, int button, bool isPlayer1) {
        if (!isPlayLayer(this)) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }
        auto& b = bot();
        if (!b.injecting) {
            // saat replay, input user diabaikan (sampai macro habis)
            if (b.state == BotState::Replaying && !b.replayFinished()) return;

            if (b.state == BotState::Recording) {
                PlayerObject* p = isPlayer1 ? m_player1 : m_player2;
                if (!b.recordInput(down, button, isPlayer1, b.frame, snapOf(p))) return;
            }
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    void update(float dt) {
        if (isPlayLayer(this)) {
            auto pl = PlayLayer::get();
            detectCheckpoint(pl);
            GJBaseGameLayer::update(dt);
            hudTick(pl);
            return;
        }
        GJBaseGameLayer::update(dt);
    }

    // TPS custom: simulasikan kelipatan 1/tps per frame render.
    float getModifiedDelta(float dt) {
        auto& b = bot();
        if (!isPlayLayer(this) || b.activeTps == 240) {
            return GJBaseGameLayer::getModifiedDelta(dt);
        }
        return static_cast<float>(b.stepDelta(dt, m_gameState.m_timeWarp));
    }
};

class $modify(VBotPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        auto& b = bot();
        b.state = BotState::Idle;
        b.activeTps = 240;
        b.frame = 0;
        b.idx = 0;
        b.extraDelta = 0.0;
        b.fromStart = false;
        b.injecting = false;
        b.clearHeld();
        checkpointSnaps().clear();
        lastCheckpointCount() = 0;
        hudAttach(this);
        return true;
    }

    void resetLevel() {
        auto& b = bot();

        // selama reset bawaan game, jangan ada input yang ikut terekam
        bool oldInjecting = b.injecting;
        b.injecting = true;
        PlayLayer::resetLevel();
        b.injecting = oldInjecting;

        b.activeTps = b.state != BotState::Idle ? b.targetTps : 240;

        bool forced = b.fromStart;
        b.fromStart = false;
        auto arr = m_checkpointArray;
        bool hasCheckpoint = m_isPracticeMode && arr && arr->count() > 0;

        auto& snaps = checkpointSnaps();
        auto it = hasCheckpoint ? snaps.find(arr->lastObject()) : snaps.end();

        if (!forced && hasCheckpoint && it == snaps.end() && b.state != BotState::Idle) {
            // checkpoint yang tidak dikenal -> frame tidak bisa disamakan, hentikan biar macro tidak rusak
            b.state = BotState::Idle;
            FLAlertLayer::create(
                "Vinz Tool Bot",
                "Checkpoint tidak dikenali, rekam/replay dihentikan supaya macro tidak rusak. "
                "Mulai lagi dari awal level.",
                "OK"
            )->show();
        }

        if (forced || !hasCheckpoint || it == snaps.end()) {
            // attempt baru dari awal
            b.resetFromStart();
            snaps.clear();
            lastCheckpointCount() = hasCheckpoint && !forced ? arr->count() : 0;
            return;
        }

        // Respawn dari checkpoint (practice mode):
        // input setelah checkpoint dibuang, death practice tidak direkam.
        auto releases = b.resetToCheckpoint(it->second);

        if (Mod::get()->getSettingValue<bool>("practice-fix")) {
            applySnap(m_player1, it->second.p1);
            applySnap(m_player2, it->second.p2);
        }

        b.injecting = true;
        for (auto const& r : releases) {
            this->handleButton(false, r.button, r.player1);
        }
        b.injecting = false;
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto& b = bot();
        // death di practice mode TIDAK PERNAH dimasukkan ke macro
        if (b.state == BotState::Recording && !m_isPracticeMode) {
            b.recordDeath();
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void levelComplete() {
        auto& b = bot();
        if (b.state != BotState::Idle) {
            b.state = BotState::Idle; // stop rec/play, macro tetap tersimpan di memori
        }
        PlayLayer::levelComplete();
    }
};
