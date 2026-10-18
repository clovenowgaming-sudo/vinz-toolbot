#include "Bot.hpp"

using namespace geode::prelude;

BotCore& bot() {
    static BotCore instance;
    return instance;
}

std::unordered_map<void*, CheckpointSnap>& checkpointSnaps() {
    static std::unordered_map<void*, CheckpointSnap> m;
    return m;
}

size_t& lastCheckpointCount() {
    static size_t n = 0;
    return n;
}

PlayerSnap snapOf(PlayerObject* p) {
    PlayerSnap s;
    if (!p) return s;
    s.x = p->getPositionX();
    s.y = p->getPositionY();
    s.rot = p->getRotation();
    s.yv = p->m_yVelocity;
    s.xv = 0.0;
    return s;
}

void applySnap(PlayerObject* p, PlayerSnap const& s) {
    if (!p) return;
    p->setPosition({s.x, s.y});
    p->setRotation(s.rot);
    p->m_yVelocity = s.yv;
}

bool isPlayLayer(GJBaseGameLayer* layer) {
    auto pl = PlayLayer::get();
    return pl && static_cast<GJBaseGameLayer*>(pl) == layer;
}

static constexpr auto kStatusId = "vinz-status";

void hudAttach(PlayLayer* pl) {
    if (!pl || pl->getChildByID(kStatusId)) return;
    auto ws = CCDirector::get()->getWinSize();
    auto label = CCLabelBMFont::create("", "bigFont.fnt");
    label->setID(kStatusId);
    label->setScale(0.4f);
    label->setOpacity(170);
    label->setAnchorPoint({0.f, 1.f});
    label->setPosition({8.f, ws.height - 6.f});
    pl->addChild(label, 1000);
}

void hudTick(PlayLayer* pl) {
    if (!pl) return;
    auto label = typeinfo_cast<CCLabelBMFont*>(pl->getChildByID(kStatusId));
    if (!label) return;
    auto& b = bot();
    std::string text;
    if (Mod::get()->getSettingValue<bool>("show-status")) {
        switch (b.state) {
            case BotState::Recording:
                text = fmt::format("REC  f{}  {}tps", b.frame, b.activeTps);
                break;
            case BotState::Replaying:
                text = fmt::format("PLAY  f{}  {}tps", b.frame, b.activeTps);
                break;
            default: break;
        }
    }
    if (std::string(label->getString()) != text) {
        label->setString(text.c_str());
    }
}
