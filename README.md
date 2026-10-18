# Vinz Tool Bot

Macro bot (record / replay) untuk **Geometry Dash 2.2081** dengan **Geode SDK 5.11.2**.
Format macro: **GDR2** ([GDReplayFormat](https://github.com/maxnut/GDReplayFormat), sudah ada di `include/gdr`).

## Cara pakai (di pause menu)

| Tombol | Fungsi |
|---|---|
| **REC** (lingkaran 1) | Mulai rekam dari awal level. Tekan lagi (STOP) untuk berhenti. |
| **PLAY** (lingkaran 2) | Replay macro yang ada di memori dari awal level. |
| `input: N` | Jumlah input di macro sekarang. |
| **+** (di bawah `input:`) | Panel TPS: -10 / +10 / -1 / +1 / preset 60, 240, 480, 960. |
| **Macro** | Panel `<` `>` pilih file, **Save**, **Load**, **Export**, **Import**, **Back**. |

* **Save** menyimpan ke folder `macros/` di save dir mod (format `.gdr2`).
* **Load** memuat file yang dipilih dengan `<` `>`. File dari luar: taruh `.gdr2` di folder `macros/`.
* **Export** menyalin macro sebagai teks base64 ke clipboard, **Import** membaca teks itu dari clipboard.
* TPS yang dipilih dipakai saat REC dan PLAY. Saat PLAY, TPS otomatis mengikuti macro. TPS 240 = physics bawaan game.

## Practice mode

* **Practice fix**: waktu respawn dari checkpoint, posisi / rotasi / kecepatan-Y pemain dikembalikan persis seperti saat checkpoint dibuat.
* Saat REC di practice mode lalu mati: semua input setelah checkpoint dibuang dan **death tidak ikut terekam**, jadi saat replay tidak mati di tempat itu.
* Frame fix (di Settings mod): tiap input menyimpan posisi / rotasi / kecepatan-Y, dan dipakai untuk mengoreksi saat replay.

## Build lokal (opsional)

```
geode build
```

## Build lewat GitHub Actions

Push ke GitHub, buka tab **Actions**, tunggu job selesai, download artifact **Build Output** (isinya `vinz.toolbot.geode`).
Copy file `.geode` ke folder `geode/mods/` di GD (Android: `Android/media/com.geode.launcher/game/geode/mods/`).

## Struktur

```
src/Core.hpp    logika inti (record, truncate practice, TPS, simpan/muat) - tanpa Geode
src/Hooks.cpp   hook GJBaseGameLayer / PlayLayer
src/UI.cpp      tombol di PauseLayer
src/Bot.*       helper + status REC/PLAY di layar
include/gdr     GDReplayFormat
```
