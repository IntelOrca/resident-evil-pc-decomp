#pragma once
// Entry points for the two migrations the GUI drives.

#include "core/AudioWriter.h"
#include "core/Types.h"

#include <string>

namespace re1 {

struct PcMigrationOptions {
    // Either an already-extracted folder or a disc image, per sourceIsImage.
    std::string sourcePath;
    bool sourceIsImage = false;
    // Game folder the USA/ or JPN/ tree is written under.
    std::string targetRoot;
    AssetVersion version = AssetVersion::USA;
    // Convert every .avi in the migrated Movie folder to .mp4 (H.264 + AAC).
    bool convertMovies = false;
    // Keep the source .avi next to the generated .mp4 (the engine prefers the
    // .mp4 and falls back to the .avi).
    bool keepAvi = true;
    // ffmpeg executable; resolved from PATH when it is a bare name.
    std::string ffmpegPath = "ffmpeg";

    // Optional PS1 disc image (the 1996 release or the Director's Cut) whose
    // PS1-only assets are added to the same USA/JPN tree, for the PS1 ending
    // credits in OG mode ([Game] Ps1EndingCredits=1). A raw .bin/.cue keeps
    // the movies' CD-XA audio.
    std::string ps1ImagePath;
    // Copy DATA/STAFF.STF, STAFF2.STF and BIO.TIM into <tree>/Data. The
    // tree's own EN05/EN07/CLIS01/JILL01 are left alone.
    bool ps1Credits = true;
    // Convert the disc's .STR movies to .mp4 in <tree>/Movie: always the
    // credits movies STFC/STFJ, and any other movie the tree has no version
    // of (under its PC name).
    bool ps1Movies = true;
    // Also overwrite the tree's own movies with the PS1 versions, and
    // re-convert STFC/STFJ when they already exist.
    bool ps1ReplaceMovies = false;
    // Decode the disc's DATA/JIMAKU*.RGB into <tree>/Data/jimaku*.png, the
    // prologue FMV subtitles ([Game] Ps1FmvSubtitles=1). Only the JPN releases
    // carry them; a disc without them is skipped, not an error.
    bool ps1FmvSubtitles = true;
    // Replace the tree's sound effects, voices and BGM with the disc's (the
    // files in <tree>/Sound and <tree>/Voice, under the PC's names). Every
    // name the PS1 cannot supply keeps the tree's file; everything replaced is
    // listed in Sound/AUDIO.TXT. A later run with a PC source restores the
    // PC files. Off by default.
    bool ps1Audio = false;
    bool ps1AudioSfx = true;
    bool ps1AudioVoices = true;
    bool ps1AudioBgm = true;
    // WAV, or Ogg Vorbis at ps1AudioOggQuality (needs ffmpeg with libvorbis).
    bool ps1AudioOgg = false;
    int ps1AudioOggQuality = 6;
};

struct DcMigrationOptions {
    // The Director's Cut disc image (.bin/.cue/.iso). A raw 2352-byte image is
    // required for clean CD-XA audio in the movie conversion.
    std::string imagePath;
    // Game folder the DC/ overlay is written under.
    std::string targetRoot;
    // Base tree the overlay is built on top of.
    AssetVersion base = AssetVersion::USA;
    Backgrounds backgrounds = Backgrounds::All;
    // Convert the disc's .STR movies to .mp4 into the overlay's Movie/ folder.
    bool convertMovies = false;
    // Run the coverage/background check after the overlay is written.
    bool verify = true;
    std::string ffmpegPath = "ffmpeg";
    // Decode the disc's DATA/JIMAKU*.RGB into <target>/JPN/Data/jimaku*.png,
    // the prologue FMV subtitles ([Game] Ps1FmvSubtitles=1). They go into the
    // JPN tree, not the overlay: Ps1FmvSubtitles builds its path from the
    // compile-time GAME_DATA_ROOT_JPN and calls LoadFile directly, so the
    // overlay's Data folder is never searched for them. Only the JPN discs
    // carry the files; a disc without them is skipped, not an error.
    bool fmvSubtitles = true;
};

struct SaturnMigrationOptions {
    // The Sega Saturn disc image (the track-1 .bin, or its .cue). Track 1 is
    // MODE1/2352 ISO9660; the audio tracks are not read.
    std::string imagePath;
    // Game folder the SATURN/ overlay is written under.
    std::string targetRoot;
    // What the Saturn adds is always migrated - there is no switch for it, because
    // the port's Saturn mode needs all of it:
    //   - the Saturn-only creatures (EM1016/EM1116 = the Tick, EM1017/EM1117 =
    //     Zombie Wesker) and the Saturn-only alternate outfits (EM1030..EM1033,
    //     written as EM1060/1061/1160/1161), as PC .EMDs in SATURN/Enemy;
    //   - the 15 Battle Game arena rooms (STAGE8/ROOM80x0.RDT, LZ77) in
    //     SATURN/Stage8, with the PS1 colour-key -> PC index-0 relabel;
    //   - the Laboratory Tyrant room (STAGE5/ROOM513): its room file, its eight
    //     re-drawn backgrounds (RC5130..RC5137.pak) and OB80, the cover over the
    //     second capsule Chris' second Tyrant comes out of;
    //   - the title menu typeface (SATURN/Data/TITLEFNT.BIN, core/TitleFont.h;
    //     carried by the tool, not read from the disc), the Battle Game's results
    //     screens, and its exclusive sound: the arena / end-of-run / Tyrant /
    //     ranking music and the Tick's sound effects (core/SaturnMusic.cpp).
    //
    // Optional: every other room of the campaign. The Saturn's 348 shared RDTs
    // decode byte-identical to the PS1's (most also to the PC's), so this is a
    // choice about which game's data Saturn mode runs, and it is big.
    //   allRooms: every STAGE1..7 ROOM*.RDT decoded into SATURN/Stage<n>, and every
    //   ROOM*.BSS background as RC*.pak (core/SaturnBss).
    bool allRooms = false;
    //   allBgm: every campaign BGM the PC names (g_BgmNameTable), rendered from the
    //   Saturn's BGM<group>.CDP sequences into SATURN/Sound under the PC's own
    //   file names (core/SaturnBgmTable.h).
    bool allBgm = false;
    //   movies: MOVIE.CPK's 28 Sega FILM movies -> SATURN/Movie/<PC name>.mp4
    //   (H.264 + AAC, needs ffmpeg; core/SaturnMovie.h). The prologue's three
    //   Saturn files are joined into one, which the game cuts for Jill at the
    //   Saturn's own boundaries. The disc has no PS1-form subtitles or credits
    //   text (JIMAKU*.RGB, STAFF*.STF): the Japanese prologue has its subtitles
    //   in the picture and the staff rolls their credits.
    bool movies = false;
    // Write the PC/PS1 option typeface as SATURN/Data/TITLEFNT.BIN.
    bool titleFont = true;
    // Audio format and quality, for every rendered sound.
    AudioFormat musicFormat = AudioFormat::Wav;
    int oggQuality = 6;             // libvorbis -q:a when musicFormat is Ogg
    std::string ffmpegPath = "ffmpeg";
    // Re-read everything written and check it.
    bool verify = true;
};

// migratePcAssets runs the base-tree copy when sourcePath is set, then the PS1
// supplement when ps1ImagePath is set; at least one of the two is required.
// Both return true on success; on failure `error` carries a human-readable
// message. Cancellation through `progress.cancelled` returns false with
// "cancelled" as the error.
bool migratePcAssets(const PcMigrationOptions& opts, const Progress& progress,
                     std::string* error);

bool migrateDcAssets(const DcMigrationOptions& opts, const Progress& progress,
                     std::string* error);

// Builds <target>/SATURN from a Saturn disc (core/SaturnOverlay.cpp).
bool migrateSaturnAssets(const SaturnMigrationOptions& opts,
                         const Progress& progress, std::string* error);

}  // namespace re1
