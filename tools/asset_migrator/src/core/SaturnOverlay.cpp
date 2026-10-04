// Saturn overlay builder: <target>/SATURN from a Sega Saturn disc image.
//
// Only what the Saturn release adds on top of the PC/PS1 content goes into the
// overlay; everything else is shared with the base tree:
//
//   Enemy/EM1016.EMD, EM1116.EMD   the Tick (Chris / Jill model table block)
//   Enemy/EM1017.EMD, EM1117.EMD   Zombie Wesker
//                                  -> converted to PC .EMDs (core/SaturnEmd)
//   Stage8/ROOM8010..80F0.RDT      the 15 Battle Game arenas, LZ77-decoded;
//                                  decoded they are ordinary PS1 RDTs
//   Stage5/RC5130..5137.pak        the Laboratory Tyrant room's backgrounds,
//                                  which the Saturn re-drew on all eight
//                                  cameras (the tank vats are emptied because
//                                  the Saturn cannot do translucent glass, and
//                                  the second capsule is in this room)
//                                  -> decoded and re-packed (core/SaturnBss)
//   Data/TITLEFNT.BIN              the PC/PS1 option typeface for the title menu
//                                  (NEW GAME / LOAD GAME / EXTRA). Not on the
//                                  disc - the Saturn menu uses another font -
//                                  so core/TitleFont.cpp carries the glyphs
//   Sound/SAT_BATTLE, SAT_BATTLE_END, SAT_RANKING
//                                  the Battle Game's own music, rendered from
//                                  SND/BATTLE.CDP and SND/NAME.CDP through the
//                                  Sega sound driver + SCSP model
//                                  (core/SaturnSound, core/SaturnMusic)
//
// Not migrated yet, and logged as such: the rest of the Saturn audio (the
// campaign's own BGM arrangement, the SE banks, the .PCK room PCM and the X68
// SSCONV .BIN banks), the MOVIE.CPK FMV, the title/ending BM60
// art, and the A_*.DAT room props (the extra lab Tyrant capsule), whose format
// is decoded by tools/saturn_mbn.py but has no PC writer yet. The 348 campaign
// RDTs decode byte-identical to the PS1's, and the arenas reuse campaign
// backgrounds, so neither needs a copy. Only the rooms listed in kRoomBgs are
// copied: of 655 Saturn camera frames, those are the ones whose art the Saturn
// changed beyond a palette shift (tools/saturn_bss_to_pak.py --verify).
// Reference: docs/SATURN_VERSION_ANALYSIS.md, docs/SATURN_PORT.md.

#include "core/DiscImage.h"
#include "core/Migrate.h"
#include "core/Pak.h"
#include "core/SaturnBss.h"
#include "core/SaturnEmd.h"
#include "core/SaturnLz77.h"
#include "core/SaturnMusic.h"
#include "core/SaturnObModel.h"
#include "core/TitleFont.h"
#include "core/Tim.h"
#include "core/Util.h"

#include <cstdio>
#include <cstring>

namespace re1 {

namespace {

const char* const kModels[][2] = {
    {"EM1016.EMD", "the Tick (Chris's game)"},
    {"EM1116.EMD", "the Tick (Jill's game)"},
    {"EM1017.EMD", "Zombie Wesker (Chris's game)"},
    {"EM1117.EMD", "Zombie Wesker (Jill's game)"},
};

const char* const kArenas[] = {"ROOM8010.RDT", "ROOM8020.RDT", "ROOM8030.RDT",
                               "ROOM8040.RDT", "ROOM8050.RDT", "ROOM8060.RDT",
                               "ROOM8070.RDT", "ROOM8080.RDT", "ROOM8090.RDT",
                               "ROOM80A0.RDT", "ROOM80B0.RDT", "ROOM80C0.RDT",
                               "ROOM80D0.RDT", "ROOM80E0.RDT", "ROOM80F0.RDT"};

// The rooms the Saturn re-drew: disc folder, .BSS file name, stage digit for
// the PC path, room id for the PC path. Everything else falls back to the base
// tree - see the header and tools/saturn_bss_to_pak.py.
struct RoomBg {
    const char* folder;
    const char* bss;
    char stageDigit;
    int roomId;
    const char* why;
};

const RoomBg kRoomBgs[] = {
    {"STAGE5", "ROOM513.BSS", '5', 0x13,
     "Laboratory B4, the Tyrant room - emptied vats, second capsule"},
};

// Campaign rooms whose *data* the Saturn also cut differently, and which are
// therefore overlaid as a whole .RDT rather than patched in code. The PC ships
// two files per room (the two character variants, chosen by the last digit of
// room5S RRc.rdt); the Saturn has one, so both names get the same file.
struct RoomRdt {
    const char* folder;
    const char* rdt;
    char stageDigit;
    int roomId;
    const char* why;
};

const RoomRdt kRoomRdts[] = {
    {"STAGE5", "ROOM5130.RDT", '5', 0x13,
     "Laboratory B4: the PC's room has one item the Saturn's does not (the "
     "init script is 28 bytes - one item_model_set - longer and item_icons "
     "holds two 40x30 icons against one), so the Saturn's room is loaded "
     "instead of the PC's"},
};

bool readDiscFile(const DiscImage& img, const std::string& folder,
                  const std::string& name, std::vector<uint8_t>* out,
                  std::string* error) {
    const DiscEntry* e = img.findInFolder(folder, name);
    if (!e) {
        if (error) *error = folder + "/" + name + " is not on the disc";
        return false;
    }
    if (!img.readFile(*e, out)) {
        if (error) *error = "cannot read " + folder + "/" + name;
        return false;
    }
    return true;
}

// A PC .EMD ends in a 5-dword directory; its TMD and TIM must be where it says.
bool checkPcEmd(const std::vector<uint8_t>& d, std::string* why) {
    if (d.size() < 64 || d.size() % 4) {
        *why = "bad size";
        return false;
    }
    const uint32_t tmd = readU32(d.data(), d.size(), d.size() - 8);
    const uint32_t tim = readU32(d.data(), d.size(), d.size() - 4);
    if (tmd >= tim || tim >= d.size()) {
        *why = "directory out of range";
        return false;
    }
    if (readU32(d.data(), d.size(), tim) != 0x10 ||
        readU32(d.data(), d.size(), tim + 4) != 9) {
        *why = "no 8 bpp TIM at the directory's texture offset";
        return false;
    }
    const uint32_t tmdSize = readU32(d.data(), d.size(), tmd);
    if (tmd + tmdSize > tim) {
        *why = "TMD overruns the TIM";
        return false;
    }
    return true;
}

// A background pak must come back out of the game's own LZW as exactly the one
// TIM the reader expects: 16 bpp, VRAM (0,240), 320x240, image block length
// counting only the pixel bytes. That checks the encoder against the decoder
// instead of against itself. See docs/PSX_BSS_AND_PC_PAK.md.
bool checkBackgroundPak(const std::vector<uint8_t>& d, std::string* why) {
    std::vector<uint8_t> tim;
    if (!lzwUnpack(d.data(), d.size(), &tim, why)) return false;
    if (tim.size() != 20 + 320 * 240 * 2) {
        *why = "decodes to " + std::to_string(tim.size()) + " B, expected " +
               std::to_string(20 + 320 * 240 * 2);
        return false;
    }
    if (readU32(tim.data(), tim.size(), 0) != 0x10 ||
        readU32(tim.data(), tim.size(), 4) != 2 ||
        readU32(tim.data(), tim.size(), 8) != 320 * 240 * 2) {
        *why = "not a 16 bpp background TIM";
        return false;
    }
    if (readU16(tim.data(), tim.size(), 12) != 0 ||
        readU16(tim.data(), tim.size(), 14) != 240 ||
        readU16(tim.data(), tim.size(), 16) != 320 ||
        readU16(tim.data(), tim.size(), 18) != 240) {
        *why = "wrong VRAM position or size";
        return false;
    }
    return true;
}

}  // namespace

bool migrateSaturnAssets(const SaturnMigrationOptions& opts,
                         const Progress& progress, std::string* error) {
    progress.info("Sega Saturn migration");
    if (opts.imagePath.empty()) {
        if (error) *error = "no disc image selected";
        return false;
    }
    if (opts.targetRoot.empty()) {
        if (error) *error = "no target game folder selected";
        return false;
    }
    DiscImage img;
    std::string err;
    if (!img.open(opts.imagePath, &err)) {
        if (error) *error = err;
        return false;
    }
    if (!img.findInFolder("ENEMY", "EM1016.EMD") ||
        !img.findInFolder("STAGE8", "ROOM8010.RDT")) {
        if (error)
            *error = "this is not the Resident Evil Saturn disc (no "
                     "ENEMY/EM1016.EMD or STAGE8/ROOM8010.RDT)";
        return false;
    }
    const std::string overlay = joinPath(opts.targetRoot, "SATURN");
    if (!makeDirs(overlay)) {
        if (error) *error = "cannot create " + overlay;
        return false;
    }
    progress.info("target: " + overlay);

    std::vector<std::string> written;
    const int total = (opts.convertModels ? 4 : 0) + (opts.arenaRooms ? 15 : 0) +
                      (opts.roomBackgrounds
                           ? (int)(sizeof(kRoomBgs) / sizeof(kRoomBgs[0])) +
                                 (int)(sizeof(kRoomRdts) / sizeof(kRoomRdts[0]))
                           : 0) +
                      (opts.battleMusic ? 1 : 0) + (opts.tickSfx ? 1 : 0);
    int done = 0;

    if (opts.convertModels) {
        progress.info("models: Saturn-only creatures -> PC .EMD");
        for (const auto& m : kModels) {
            if (progress.isCancelled()) {
                if (error) *error = "cancelled";
                return false;
            }
            progress.phase(done++, total, m[0]);
            std::vector<uint8_t> src, pc;
            if (!readDiscFile(img, "ENEMY", m[0], &src, error)) return false;
            SaturnEmdInfo info;
            if (!convertSaturnEmd(src, &pc, &info, &err)) {
                if (error) *error = std::string(m[0]) + ": " + err;
                return false;
            }
            const std::string rel = std::string("Enemy/") + m[0];
            if (!writeFile(joinPath(overlay, rel), pc)) {
                if (error) *error = "cannot write " + joinPath(overlay, rel);
                return false;
            }
            written.push_back(rel);
            char line[256];
            snprintf(line, sizeof(line),
                     "  %s  %s: %d parts, %d palettes, %d textures -> %d page(s), "
                     "TIM 256x%d, %zu bytes",
                     m[0], m[1], info.parts, info.palettes, info.textures,
                     info.pages, info.timHeight, pc.size());
            progress.info(line);
        }
    }

    if (opts.arenaRooms) {
        progress.info("rooms: Battle Game arenas (STAGE8) -> decoded RDTs");
        size_t relabelled = 0;
        for (const char* name : kArenas) {
            if (progress.isCancelled()) {
                if (error) *error = "cancelled";
                return false;
            }
            progress.phase(done++, total, name);
            std::vector<uint8_t> src, rdt;
            if (!readDiscFile(img, "STAGE8", name, &src, error)) return false;
            size_t used = 0;
            if (!saturnLz77Decode(src.data(), src.size(), &rdt, &used, &err)) {
                if (error) *error = std::string(name) + ": " + err;
                return false;
            }
            // PS1 art keys holes by colour 0x0000; the PC engine by index 0.
            int moved = 0;
            for (const auto& blk : iterTimBlocks(rdt))
                moved += foldTimTransparency(&rdt, blk);
            relabelled += moved;
            const std::string rel = std::string("Stage8/") + name;
            if (!writeFile(joinPath(overlay, rel), rdt)) {
                if (error) *error = "cannot write " + joinPath(overlay, rel);
                return false;
            }
            written.push_back(rel);
            progress.info("  " + std::string(name) + ": " +
                          std::to_string(src.size()) + " -> " +
                          std::to_string(rdt.size()) + " bytes");
        }
        progress.info("  " + std::to_string(relabelled) +
                      " texel(s) moved onto the index-0 transparency key");
    }

    if (opts.roomBackgrounds) {
        progress.info("rooms: backgrounds the Saturn re-drew -> PC paks");
        for (const RoomBg& bg : kRoomBgs) {
            if (progress.isCancelled()) {
                if (error) *error = "cancelled";
                return false;
            }
            progress.phase(done++, total, bg.bss);
            std::vector<uint8_t> src;
            if (!readDiscFile(img, bg.folder, bg.bss, &src, error)) return false;
            std::vector<SaturnBssFrame> frames;
            if (!saturnBssDecode(src.data(), src.size(), &frames, &err)) {
                if (error) *error = std::string(bg.bss) + ": " + err;
                return false;
            }
            const std::string dir =
                joinPath(overlay, std::string("Stage") + bg.stageDigit);
            if (!makeDirs(dir)) {
                if (error) *error = "cannot create " + dir;
                return false;
            }
            progress.info(std::string("  ") + bg.bss + ": " + bg.why);
            for (const SaturnBssFrame& f : frames) {
                const std::string name = pakName(bg.stageDigit, bg.roomId, f.index);
                // Same PC writer the DC's arrange backgrounds go through: a
                // 320x240 16 bpp TIM (vram 0,240) inside the game's LZW.
                const std::vector<uint8_t> tim = makeBackgroundTim(f.rgb);
                const std::vector<uint8_t> pak = lzwPack(tim);
                const std::string rel =
                    std::string("Stage") + bg.stageDigit + "/" + name;
                if (!writeFile(joinPath(overlay, rel), pak)) {
                    if (error) *error = "cannot write " + joinPath(overlay, rel);
                    return false;
                }
                written.push_back(rel);
                progress.info("  " + name + ": " + std::to_string(src.size()) +
                              " B .BSS -> " + std::to_string(pak.size()) +
                              " B pak (camera " + std::to_string(f.index) + ")");
            }
        }
        for (const RoomRdt& r : kRoomRdts) {
            if (progress.isCancelled()) {
                if (error) *error = "cancelled";
                return false;
            }
            progress.phase(done++, total, r.rdt);
            std::vector<uint8_t> packed;
            if (!readDiscFile(img, r.folder, r.rdt, &packed, error)) return false;
            std::vector<uint8_t> rdt;
            std::string why;
            if (!saturnLz77Decode(packed.data(), packed.size(), &rdt, nullptr, &why)) {
                if (error) *error = std::string(r.rdt) + ": " + why;
                return false;
            }
            // A PC .RDT is a plain file - LoadFile reads it verbatim - so this is
            // the Saturn's room data with nothing to relabel, unlike the arenas.
            //
            // The Laboratory Tyrant room also gets the Saturn's model for omodel
            // slot 0, ENEMY/OB80.DAT - the dark cover over the second capsule.
            // The Saturn draws no room-file TMD at all: it loads OBnn.DAT per
            // slot (GAME4 table 0x060980F4, row for this room 50 00 00 00 12 5A
            // 03) and places each with a hardcoded transform (0x060681E8). OB80's
            // cases: position (-4, 1.5, 0) at 0x06068616, scale (24, 44, 64) at
            // 0x06068878, rotation (0, 0x2580, 0) at 0x06068A00 - the same values
            // read back from the running game. Slots 1-3 get no Saturn model at
            // all; the engine skips them (saturn_omodel_has_no_model).
            if (r.roomId == 0x13 && r.stageDigit == '5') {
                std::vector<uint8_t> ob, tmd;
                if (!readDiscFile(img, "ENEMY", "OB80.DAT", &ob, error)) return false;
                const SaturnObTransform xf = {{-4.0, 1.5, 0.0}, {0, 0x2580, 0}, {24.0, 44.0, 64.0}};
                SaturnObInfo oi;
                // cba 0x7800 / tsb 0x0080 are the room TMDs' own relative
                // addressing (CLUT row 0x1E0, page 0, 8 bpp); the engine adds the
                // texture bank the TIM lands in (ProcessTmdTextures).
                if (!convertSaturnObToTmd(ob, xf, 4, 4, 0x7800, 0x0080, &tmd, &oi, &why)) {
                    if (error) *error = "OB80.DAT: " + why;
                    return false;
                }
                const std::vector<uint8_t> tim = makeFlatTim8(oi.colour, 0, 480, 0, 0);
                if (!patchRdtOmodel(&rdt, 0, tmd, tim, &why)) {
                    if (error) *error = std::string(r.rdt) + ": " + why;
                    return false;
                }
                char line[200];
                std::snprintf(line, sizeof(line),
                              "  OB80.DAT -> omodel slot 0: %d points, %d polygons -> %d "
                              "triangles, colour %04X",
                              oi.points, oi.polygons, oi.triangles, oi.colour);
                progress.info(line);
            }
            const std::string dir =
                joinPath(overlay, std::string("Stage") + r.stageDigit);
            if (!makeDirs(dir)) {
                if (error) *error = "cannot create " + dir;
                return false;
            }
            progress.info(std::string("  ") + r.rdt + ": " + r.why);
            // Both character variants resolve to the one room the Saturn has.
            for (int variant = 0; variant < 2; ++variant) {
                char name[32];
                std::snprintf(name, sizeof(name), "ROOM%c%02X%d.RDT",
                              r.stageDigit, r.roomId, variant);
                const std::string rel =
                    std::string("Stage") + r.stageDigit + "/" + name;
                if (!writeFile(joinPath(overlay, rel), rdt)) {
                    if (error) *error = "cannot write " + joinPath(overlay, rel);
                    return false;
                }
                written.push_back(rel);
                progress.info(std::string("  ") + name + ": " +
                              std::to_string(static_cast<size_t>(packed.size())) +
                              " B LZ77 -> " +
                              std::to_string(static_cast<size_t>(rdt.size())) + " B rdt");
            }
        }
    }
    if (opts.titleFont) {
        progress.info("title menu font -> Data/TITLEFNT.BIN");
        const std::vector<uint8_t> font = buildTitleFont();
        const std::string rel = "Data/TITLEFNT.BIN";
        if (!makeDirs(joinPath(overlay, "Data")) ||
            !writeFile(joinPath(overlay, rel), font)) {
            if (error) *error = "cannot write " + joinPath(overlay, rel);
            return false;
        }
        written.push_back(rel);
        progress.info("  " + rel + " (" + std::to_string(font.size()) + " bytes)");
    }
    if (opts.battleMusic) {
        progress.info("music: the Battle Game's own tracks -> rendered from the sequences");
        // The Battle Game's results screens (RANKING.PRG 0x0602B1E4): their
        // background BMP, both fonts and palettes are in /BATTLE.BIN (X68
        // SSCONV sections at 0x0, 0x10000 BMP, 0x30000, 0x34000) and the
        // item-name labels in /WAPON3.BIN. The game reads them as they are
        // (src/game/saturn/SaturnBattle.cpp), so they are copied verbatim.
        for (const char* name : {"BATTLE.BIN", "WAPON3.BIN"}) {
            const DiscEntry* e = img.find(name);
            std::vector<uint8_t> d;
            if (!e || !img.readFile(*e, &d)) {
                if (error) *error = std::string("cannot read /") + name;
                return false;
            }
            const std::string rel = std::string("Data/") + name;
            if (!writeFile(joinPath(overlay, rel), d)) {
                if (error) *error = "cannot write " + joinPath(overlay, rel);
                return false;
            }
            written.push_back(rel);
            progress.info("  " + rel + " (results screens, " + std::to_string(d.size()) + " bytes)");
        }
    }
    if (opts.battleMusic || opts.tickSfx) {
        progress.info("audio: the Battle Game's tracks and the Tick's sound effects -> "
                      "rendered from the sequences");
        progress.phase(done, total, "audio");
        SaturnMusicOptions mo;
        mo.music = opts.battleMusic;
        mo.tickSfx = opts.tickSfx;
        mo.format = opts.musicFormat;
        mo.oggQuality = opts.oggQuality;
        mo.ffmpegPath = opts.ffmpegPath;
        std::vector<std::string> music;
        if (!migrateSaturnMusic(img, overlay, mo, progress, &music, error)) return false;
        written.insert(written.end(), music.begin(), music.end());
        done += 2 - (opts.battleMusic ? 0 : 1) - (opts.tickSfx ? 0 : 1);
    }
    progress.phase(total, total, "done");

    progress.info("not migrated yet (no PC asset / no engine support):");
    progress.info("  the campaign's Saturn BGM/SE (SND/*.CDP, *.PCK), X68 SSCONV .BIN banks, "
                  "MOVIE.CPK FMV, TITLE.BIN/END.BIN art, ENEMY/A_*.DAT props");

    // Manifest: what this overlay holds, for people and for later passes.
    std::string manifest =
        "SATURN overlay written by the RE1 Asset Migrator from\n  " +
        opts.imagePath + "\n\nFiles:\n";
    for (const auto& w : written) manifest += "  " + w + "\n";
    manifest +=
        "\nEverything else falls back to the base tree. See "
        "docs/SATURN_PORT.md.\n";
    writeFile(joinPath(overlay, "SATURN.TXT"),
              (const uint8_t*)manifest.data(), manifest.size());

    if (opts.verify) {
        progress.info("verify:");
        int bad = 0;
        for (const auto& rel : written) {
            std::vector<uint8_t> d;
            std::string why;
            bool ok = readFile(joinPath(overlay, rel), &d);
            if (!ok) why = "cannot read back";
            else if (rel == "Data/TITLEFNT.BIN") {
                ok = checkTitleFont(d, nullptr);
                if (!ok) why = "not a consistent TITLEFNT.BIN";
            }
            else if (rel.rfind("Enemy/", 0) == 0) ok = checkPcEmd(d, &why);
            else if (rel.find(".pak") != std::string::npos)
                ok = checkBackgroundPak(d, &why);
            else if (rel.rfind("Sound/", 0) == 0) {
                ok = d.size() > 44 && (std::memcmp(d.data(), "RIFF", 4) == 0 ||
                                       std::memcmp(d.data(), "OggS", 4) == 0);
                if (!ok) why = "not a WAV / Ogg file";
            }
            else if (d.size() < 0x100) { ok = false; why = "too small for an RDT"; }
            if (!ok) {
                ++bad;
                progress.info("  FAIL " + rel + ": " + why);
            }
        }
        if (bad) {
            if (error) *error = std::to_string(bad) + " file(s) failed verification";
            return false;
        }
        progress.info("  " + std::to_string(written.size()) + " file(s) OK");
    }
    progress.info("Saturn overlay complete: " + std::to_string(written.size()) +
                  " file(s) in " + overlay);
    return true;
}

}  // namespace re1
