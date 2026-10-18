#pragma once
// Logika inti bot (TANPA dependency Geode) supaya bisa dites terpisah.
#include <gdr/gdr.hpp>
#include <gdr/physics_extension.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct VBot : gdr::Replay<VBot, PhysicsInput> {
    VBot() : Replay("VinzToolBot", 1) {}
    bool shouldParseExtension() const override { return false; }
};

struct PlayerSnap {
    float x = 0.f, y = 0.f, rot = 0.f;
    double xv = 0.0, yv = 0.0;
};

struct CheckpointSnap {
    uint64_t frame = 0;
    PlayerSnap p1, p2;
};

enum class BotState { Idle, Recording, Replaying };

struct ReleaseCmd {
    int button;     // 1 = jump, 2 = left, 3 = right
    bool player1;
    PlayerSnap snap;
};

class BotCore {
public:
    BotState state = BotState::Idle;
    VBot macro;
    uint64_t frame = 0;     // jumlah physics step yang sudah jalan sejak awal attempt
    size_t idx = 0;         // pointer playback
    bool held[2][4] = {};   // [player][button]
    bool injecting = false; // true saat bot sendiri yang menekan tombol
    bool fromStart = false; // paksa reset dari awal (dipakai saat mulai rec/play)
    int targetTps = 240;    // yang dipilih user di UI
    int activeTps = 240;    // yang sedang dipakai physics
    int macroTps = 240;     // TPS macro yang ada di memori (dipakai saat simpan)
    double extraDelta = 0.0;

    static constexpr int kMinTps = 1;
    static constexpr int kMaxTps = 10000;

    void clampTps() { targetTps = std::clamp(targetTps, kMinTps, kMaxTps); }

    void clearHeld() {
        for (auto& a : held) for (auto& b : a) b = false;
    }

    void newMacro() {
        macro = VBot();
        idx = 0;
        clearHeld();
    }

    // Dipanggil dari handleButton saat recording. true kalau benar2 direkam.
    bool recordInput(bool down, int button, bool player1, uint64_t at, PlayerSnap const& s) {
        if (button < 0 || button > 3) return false;
        bool& h = held[player1 ? 0 : 1][button];
        if (h == down) return false; // duplikat, abaikan
        h = down;
        macro.inputs.emplace_back(at, static_cast<uint8_t>(button), !player1, down,
                                  s.x, s.y, s.rot, s.xv, s.yv);
        return true;
    }

    // Reset dari awal level (attempt baru, atau practice tanpa checkpoint).
    void resetFromStart() {
        frame = 0;
        idx = 0;
        extraDelta = 0.0;
        clearHeld();
        if (state == BotState::Recording) {
            macro.inputs.clear();
            macro.deaths.clear();
        }
    }

    // Respawn dari checkpoint di practice mode.
    // Semua input yg terjadi di/setelah frame checkpoint dibuang, death di practice
    // TIDAK pernah disimpan. Return: daftar tombol yg harus dilepas (macro masih "nahan").
    std::vector<ReleaseCmd> resetToCheckpoint(CheckpointSnap const& cp) {
        std::vector<ReleaseCmd> out;
        frame = cp.frame;
        extraDelta = 0.0;
        if (state == BotState::Recording) {
            auto& in = macro.inputs;
            in.erase(std::remove_if(in.begin(), in.end(),
                         [&](auto const& i) { return i.frame >= cp.frame; }),
                     in.end());
            auto& d = macro.deaths;
            d.erase(std::remove_if(d.begin(), d.end(), [&](auto f) { return f >= cp.frame; }), d.end());

            // hitung ulang state tombol dari macro yg sudah dipotong
            clearHeld();
            for (auto const& i : in) held[i.player2 ? 1 : 0][std::min<int>(i.button, 3)] = i.down;

            // game melepas tombol saat respawn -> catat release di frame checkpoint
            for (int p = 0; p < 2; p++) {
                for (int b = 1; b <= 3; b++) {
                    if (!held[p][b]) continue;
                    held[p][b] = false;
                    auto const& s = p == 0 ? cp.p1 : cp.p2;
                    macro.inputs.emplace_back(cp.frame, static_cast<uint8_t>(b), p == 1, false,
                                              s.x, s.y, s.rot, s.xv, s.yv);
                    out.push_back({b, p == 0, s});
                }
            }
        } else if (state == BotState::Replaying) {
            idx = 0;
            while (idx < macro.inputs.size() && macro.inputs[idx].frame < frame) idx++;
        }
        return out;
    }

    void recordDeath() {
        if (macro.deaths.empty() || macro.deaths.back() != frame) macro.deaths.push_back(frame);
    }

    bool replayFinished() const { return idx >= macro.inputs.size(); }

    // Ambil semua input yg jatuh tempo di frame sekarang.
    template <class F>
    void pump(F&& apply) {
        while (idx < macro.inputs.size() && macro.inputs[idx].frame <= frame) {
            apply(macro.inputs[idx]);
            idx++;
        }
    }

    // TPS bypass: berapa detik yg disimulasikan di frame render ini.
    // Hasilnya selalu kelipatan 1/tps (bisa 0 kalau belum cukup satu step).
    double stepDelta(double dt, double timeWarp) {
        double tick = 1.0 / double(activeTps) * std::min(1.0, timeWarp);
        double total = dt + extraDelta;
        long long steps = static_cast<long long>(std::floor(total / tick + 1e-9));
        if (steps < 0) steps = 0;
        double result = double(steps) * tick;
        extraDelta = total - result;
        return result;
    }

    // ---- metadata untuk simpan ----
    void fillMeta(VBot& r, std::string const& author, uint32_t levelId, std::string const& levelName,
                  bool platformer, int gameVersion) const {
        r.author = author;
        r.levelInfo = gdr::Level(levelName, levelId);
        r.gameVersion = gameVersion;
        r.platformer = platformer;
        r.framerate = double(macroTps);
        uint64_t last = macro.inputs.empty() ? 0 : macro.inputs.back().frame;
        for (auto d : macro.deaths) last = std::max(last, d);
        r.duration = float(double(last) / double(macroTps));
    }
};

// ---- base64 kecil untuk export/import lewat clipboard ----
inline std::string b64encode(std::vector<uint8_t> const& d) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    o.reserve((d.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < d.size(); i += 3) {
        uint32_t v = (uint32_t(d[i]) << 16) | (uint32_t(d[i + 1]) << 8) | uint32_t(d[i + 2]);
        o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += t[v & 63];
    }
    if (i + 1 == d.size()) {
        uint32_t v = uint32_t(d[i]) << 16;
        o += t[v >> 18]; o += t[(v >> 12) & 63]; o += "==";
    } else if (i + 2 == d.size()) {
        uint32_t v = (uint32_t(d[i]) << 16) | (uint32_t(d[i + 1]) << 8);
        o += t[v >> 18]; o += t[(v >> 12) & 63]; o += t[(v >> 6) & 63]; o += '=';
    }
    return o;
}

inline std::vector<uint8_t> b64decode(std::string const& s) {
    std::vector<uint8_t> o;
    uint32_t v = 0;
    int bits = 0;
    for (char c : s) {
        int x;
        if (c >= 'A' && c <= 'Z') x = c - 'A';
        else if (c >= 'a' && c <= 'z') x = c - 'a' + 26;
        else if (c >= '0' && c <= '9') x = c - '0' + 52;
        else if (c == '+') x = 62;
        else if (c == '/') x = 63;
        else continue; // spasi, newline, '='
        v = (v << 6) | uint32_t(x);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            o.push_back(uint8_t((v >> bits) & 0xFF));
        }
    }
    return o;
}
