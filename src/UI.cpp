#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <vector>
#include "Bot.hpp"

using namespace geode::prelude;
namespace fs = std::filesystem;

namespace {

constexpr auto kRootId = "vinz-toolbot-ui";
constexpr int kGameVersion = 22081;

fs::path macroDir() {
    return Mod::get()->getSaveDir() / "macros";
}

void alertMsg(std::string const& title, std::string const& body) {
    FLAlertLayer::create(title.c_str(), body, "OK")->show();
}

std::string sanitize(std::string s) {
    for (auto& c : s) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '_';
    }
    if (s.empty()) s = "macro";
    return s;
}

void buildUI(CCNode* root);

class UIHandler : public CCObject {
public:
    int page = 0; // 0 = utama, 1 = TPS, 2 = macro
    Ref<CCNode> pending;
    std::vector<fs::path> files;
    int sel = 0;

    static UIHandler* get() {
        static UIHandler* inst = new UIHandler();
        return inst;
    }

    // ---------- util ----------
    static CCNode* rootOf(CCObject* sender) {
        auto node = static_cast<CCNode*>(sender);
        auto menu = node ? node->getParent() : nullptr;
        return menu ? menu->getParent() : nullptr;
    }

    // UI dibangun ulang di frame berikutnya (tombol yang ditekan tidak boleh dihapus saat callback-nya jalan)
    void requestRebuild(CCObject* sender) {
        auto root = rootOf(sender);
        if (!root) return;
        pending = root;
        root->runAction(CCSequence::create(
            CCDelayTime::create(0.f),
            CCCallFunc::create(this, callfunc_selector(UIHandler::rebuildNow)),
            nullptr
        ));
    }

    void rebuildNow() {
        if (!pending) return;
        Ref<CCNode> node = pending;
        pending = nullptr;
        buildUI(node);
    }

    void refreshFiles() {
        files.clear();
        std::error_code ec;
        if (!fs::exists(macroDir(), ec)) {
            sel = 0;
            return;
        }
        std::vector<std::pair<fs::file_time_type, fs::path>> found;
        fs::directory_iterator it(macroDir(), ec), end;
        while (!ec && it != end) {
            std::error_code ec2;
            auto const& entry = *it;
            if (entry.is_regular_file(ec2) && entry.path().extension() == ".gdr2") {
                found.emplace_back(fs::last_write_time(entry.path(), ec2), entry.path());
            }
            it.increment(ec);
        }
        std::sort(found.begin(), found.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
        for (auto& f : found) files.push_back(f.second);
        sel = files.empty() ? 0 : static_cast<int>(files.size()) - 1;
    }

    VBot makeReplay() {
        auto& b = bot();
        VBot r;
        r.inputs = b.macro.inputs;
        r.deaths = b.macro.deaths;
        std::string author = GameManager::sharedState()->m_playerName;
        uint32_t id = 0;
        std::string name = "unknown";
        bool platformer = false;
        auto pl = PlayLayer::get();
        if (pl && pl->m_level) {
            id = static_cast<uint32_t>(pl->m_level->m_levelID.value());
            name = pl->m_level->m_levelName;
            platformer = pl->m_level->isPlatformer();
        }
        b.fillMeta(r, author, id, name, platformer, kGameVersion);
        return r;
    }

    void adopt(VBot const& r, CCObject* sender) {
        auto& b = bot();
        b.state = BotState::Idle;
        b.activeTps = 240;
        b.macro = r;
        b.idx = 0;
        b.clearHeld();
        int tps = static_cast<int>(std::lround(r.framerate));
        if (tps >= 1) {
            b.targetTps = tps;
            b.macroTps = tps;
        }
        b.clampTps();
        alertMsg("Vinz Tool Bot",
            fmt::format("Macro dimuat: {} input, {} tps.", r.inputs.size(), b.macroTps));
        requestRebuild(sender);
    }

    // ---------- mulai rec / play ----------
    void startRun(CCObject* sender, BotState s) {
        auto pl = PlayLayer::get();
        auto root = rootOf(sender);
        if (!pl || !root) return;
        auto pause = typeinfo_cast<PauseLayer*>(root->getParent());
        auto& b = bot();

        if (s == BotState::Replaying) {
            int tps = static_cast<int>(std::lround(b.macro.framerate));
            if (tps >= 1) b.targetTps = tps;
        }
        b.clampTps();
        if (s == BotState::Recording) {
            b.newMacro();
            b.macroTps = b.targetTps;
        }
        b.state = s;
        b.activeTps = b.targetTps;
        b.fromStart = true; // frame 0 harus sama dengan awal level
        b.idx = 0;

        // buang semua checkpoint practice supaya reset benar-benar dari awal
        if (pl->m_checkpointArray) pl->m_checkpointArray->removeAllObjects();

        if (pause) pause->onResume(nullptr);
        pl->resetLevel();
    }

    void stopRun(CCObject* sender) {
        auto& b = bot();
        b.state = BotState::Idle;
        b.activeTps = 240;
        requestRebuild(sender);
    }

    // ---------- callback tombol ----------
    void onRec(CCObject* sender) {
        if (bot().state == BotState::Recording) return stopRun(sender);
        startRun(sender, BotState::Recording);
    }

    void onPlay(CCObject* sender) {
        auto& b = bot();
        if (b.state == BotState::Replaying) return stopRun(sender);
        if (b.macro.inputs.empty()) {
            alertMsg("Vinz Tool Bot", "Belum ada macro. Rekam dulu, atau load/import macro.");
            return;
        }
        startRun(sender, BotState::Replaying);
    }

    void onTpsOpen(CCObject* sender) { page = 1; requestRebuild(sender); }
    void onMacroOpen(CCObject* sender) { page = 2; refreshFiles(); requestRebuild(sender); }
    void onBack(CCObject* sender) { page = 0; requestRebuild(sender); }

    void onTpsDelta(CCObject* sender) {
        auto& b = bot();
        b.targetTps += static_cast<CCNode*>(sender)->getTag();
        b.clampTps();
        requestRebuild(sender);
    }

    void onTpsSet(CCObject* sender) {
        auto& b = bot();
        b.targetTps = static_cast<CCNode*>(sender)->getTag();
        b.clampTps();
        requestRebuild(sender);
    }

    void onSelPrev(CCObject* sender) {
        if (!files.empty()) sel = (sel + static_cast<int>(files.size()) - 1) % static_cast<int>(files.size());
        requestRebuild(sender);
    }

    void onSelNext(CCObject* sender) {
        if (!files.empty()) sel = (sel + 1) % static_cast<int>(files.size());
        requestRebuild(sender);
    }

    void onSave(CCObject* sender) {
        auto& b = bot();
        if (b.macro.inputs.empty()) {
            alertMsg("Vinz Tool Bot", "Macro kosong, rekam dulu.");
            return;
        }
        VBot r = makeReplay();
        std::error_code ec;
        fs::create_directories(macroDir(), ec);
        auto path = macroDir() / fmt::format("{}_{}tps_{}.gdr2",
            sanitize(r.levelInfo.name), static_cast<int>(r.framerate), static_cast<long long>(std::time(nullptr)));
        auto res = r.exportData(path);
        if (res.isErr()) {
            alertMsg("Gagal simpan", res.unwrapErr());
            return;
        }
        alertMsg("Macro tersimpan", path.string());
        refreshFiles();
        requestRebuild(sender);
    }

    void onLoad(CCObject* sender) {
        if (files.empty()) {
            alertMsg("Vinz Tool Bot", "Belum ada macro di folder:\n" + macroDir().string());
            return;
        }
        auto res = VBot::importData(files[sel]);
        if (res.isErr()) {
            alertMsg("Gagal load", res.unwrapErr());
            return;
        }
        adopt(res.unwrap(), sender);
    }

    void onExport(CCObject* sender) {
        auto& b = bot();
        if (b.macro.inputs.empty()) {
            alertMsg("Vinz Tool Bot", "Macro kosong, rekam dulu.");
            return;
        }
        VBot r = makeReplay();
        auto res = r.exportData();
        if (res.isErr()) {
            alertMsg("Gagal export", res.unwrapErr());
            return;
        }
        auto text = b64encode(res.unwrap());
        geode::utils::clipboard::write(text);
        alertMsg("Export", fmt::format(
            "Macro ({} input) disalin ke clipboard sebagai teks base64. "
            "Tempel di device lain lalu tekan Import.", r.inputs.size()));
    }

    void onImport(CCObject* sender) {
        auto text = geode::utils::clipboard::read();
        auto bytes = b64decode(text);
        if (bytes.size() < 8) {
            alertMsg("Import", "Clipboard kosong atau bukan macro. Salin dulu teks hasil Export.");
            return;
        }
        auto res = VBot::importData(std::span<uint8_t>(bytes));
        if (res.isErr()) {
            alertMsg("Gagal import", res.unwrapErr());
            return;
        }
        adopt(res.unwrap(), sender);
    }
};

void buildUI(CCNode* root) {
    root->removeAllChildrenWithCleanup(true);

    auto h = UIHandler::get();
    auto& b = bot();
    auto ws = CCDirector::get()->getWinSize();
    auto P = [&](float fx, float fy) { return CCPoint{ws.width * fx, ws.height * fy}; };

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    root->addChild(menu);

    auto addItem = [&](CCNode* spr, SEL_MenuHandler cb, CCPoint pos, int tag = 0) {
        auto item = CCMenuItemSpriteExtra::create(spr, nullptr, h, cb);
        item->setPosition(pos);
        item->setTag(tag);
        menu->addChild(item);
        return item;
    };
    auto smallBtn = [](char const* text, float scale) {
        auto spr = ButtonSprite::create(text);
        spr->setScale(scale);
        return spr;
    };
    auto text = [&](std::string const& str, char const* font, float scale, CCPoint pos, CCPoint anchor) {
        auto lbl = CCLabelBMFont::create(str.c_str(), font);
        lbl->setScale(scale);
        lbl->setAnchorPoint(anchor);
        lbl->setPosition(pos);
        root->addChild(lbl);
        return lbl;
    };

    // ===== lingkaran 1: REC =====
    {
        bool on = b.state == BotState::Recording;
        auto lbl = CCLabelBMFont::create(on ? "STOP" : "REC", "bigFont.fnt");
        lbl->setScale(0.45f);
        auto spr = CircleButtonSprite::create(
            lbl, on ? CircleBaseColor::Gray : CircleBaseColor::Pink, CircleBaseSize::Medium);
        addItem(spr, menu_selector(UIHandler::onRec), P(0.062f, 0.75f));
    }

    // ===== lingkaran 2: PLAY (replay macro) =====
    {
        bool on = b.state == BotState::Replaying;
        auto lbl = CCLabelBMFont::create(on ? "STOP" : "PLAY", "bigFont.fnt");
        lbl->setScale(0.45f);
        auto spr = CircleButtonSprite::create(
            lbl, on ? CircleBaseColor::Gray : CircleBaseColor::Green, CircleBaseSize::Medium);
        addItem(spr, menu_selector(UIHandler::onPlay), P(0.062f, 0.5625f));
    }

    // ===== teks "input: N" =====
    text(fmt::format("input: {}", b.macro.inputs.size()), "chatFont.fnt", 1.f,
         P(0.027f, 0.403f), {0.f, 0.5f});

    if (h->page == 0) {
        // tombol "+" di bawah teks input -> atur TPS
        auto lbl = CCLabelBMFont::create("+", "bigFont.fnt");
        lbl->setScale(0.6f);
        auto plus = CircleButtonSprite::create(lbl, CircleBaseColor::Blue, CircleBaseSize::Small);
        addItem(plus, menu_selector(UIHandler::onTpsOpen), P(0.045f, 0.27f));
        text(fmt::format("tps: {}", b.targetTps), "chatFont.fnt", 0.8f, P(0.075f, 0.27f), {0.f, 0.5f});

        addItem(smallBtn("Macro", 0.5f), menu_selector(UIHandler::onMacroOpen), P(0.19f, 0.27f));
    } else if (h->page == 1) {
        // ===== panel TPS =====
        addItem(smallBtn("-10", 0.45f), menu_selector(UIHandler::onTpsDelta), P(0.03f, 0.27f), -10);
        text(std::to_string(b.targetTps), "bigFont.fnt", 0.45f, P(0.085f, 0.27f), {0.5f, 0.5f});
        addItem(smallBtn("+10", 0.45f), menu_selector(UIHandler::onTpsDelta), P(0.14f, 0.27f), 10);

        addItem(smallBtn("-1", 0.45f), menu_selector(UIHandler::onTpsDelta), P(0.03f, 0.17f), -1);
        addItem(smallBtn("+1", 0.45f), menu_selector(UIHandler::onTpsDelta), P(0.085f, 0.17f), 1);
        addItem(smallBtn("OK", 0.45f), menu_selector(UIHandler::onBack), P(0.14f, 0.17f));

        int presets[4] = {60, 240, 480, 960};
        for (int i = 0; i < 4; i++) {
            addItem(smallBtn(std::to_string(presets[i]).c_str(), 0.4f),
                    menu_selector(UIHandler::onTpsSet), P(0.025f + 0.04f * float(i), 0.07f), presets[i]);
        }
        text("berlaku di attempt berikutnya", "chatFont.fnt", 0.45f, P(0.027f, 0.335f), {0.f, 0.5f});
    } else {
        // ===== panel macro =====
        addItem(smallBtn("<", 0.45f), menu_selector(UIHandler::onSelPrev), P(0.03f, 0.27f));
        addItem(smallBtn(">", 0.45f), menu_selector(UIHandler::onSelNext), P(0.15f, 0.27f));
        std::string name = h->files.empty() ? "(kosong)" : h->files[h->sel].filename().string();
        if (name.size() > 16) name = name.substr(0, 14) + "..";
        text(name, "chatFont.fnt", 0.6f, P(0.09f, 0.27f), {0.5f, 0.5f});

        addItem(smallBtn("Save", 0.45f), menu_selector(UIHandler::onSave), P(0.03f, 0.17f));
        addItem(smallBtn("Load", 0.45f), menu_selector(UIHandler::onLoad), P(0.085f, 0.17f));
        addItem(smallBtn("Back", 0.45f), menu_selector(UIHandler::onBack), P(0.14f, 0.17f));

        addItem(smallBtn("Export", 0.45f), menu_selector(UIHandler::onExport), P(0.045f, 0.07f));
        addItem(smallBtn("Import", 0.45f), menu_selector(UIHandler::onImport), P(0.12f, 0.07f));
    }
}

} // namespace

class $modify(VBotPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        if (!PlayLayer::get()) return;
        auto root = CCNode::create();
        root->setID(kRootId);
        this->addChild(root, 100);
        UIHandler::get()->page = 0;
        buildUI(root);
    }
};
