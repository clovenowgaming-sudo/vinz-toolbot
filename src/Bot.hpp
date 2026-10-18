#pragma once
#include <Geode/Geode.hpp>
#include <unordered_map>
#include "Core.hpp"

// state global bot (satu instance)
BotCore& bot();

// snapshot checkpoint practice mode (key = pointer CheckpointObject)
std::unordered_map<void*, CheckpointSnap>& checkpointSnaps();
size_t& lastCheckpointCount();

PlayerSnap snapOf(PlayerObject* p);
void applySnap(PlayerObject* p, PlayerSnap const& s);

// true kalau layer ini adalah PlayLayer yang sedang aktif (bukan editor)
bool isPlayLayer(GJBaseGameLayer* layer);

// teks status kiri atas (REC / PLAY)
void hudAttach(PlayLayer* pl);
void hudTick(PlayLayer* pl);
