// TestHooks.cpp - oracle test hooks (port-only, test-gated)
//
// Implements the reference side of the arklay-rs oracle trace contract
// (M:\git\arklay-rs\oracle\trace-v1.md). All behaviour here is gated on the
// --test-* command line flags parsed in TestHooks_Init; with none of them the
// functions are no-ops and the build behaves exactly as before.
//
// Nothing in this file exists in the original 1997 binary.
#include "TestHooks.h"

#include "../Globals.h"
#include "../marni/MarniDX.h"
#include "../platform/platform.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

extern void CenterScreenOrigin(void);
#include <cstring>

// ---------------------------------------------------------------------------
// Parsed flags (file-static; the game is single-threaded, single instance)
// ---------------------------------------------------------------------------
static int   s_newGameArmed     = 0;
static int   s_newGameCharacter = 0;
static int   s_noLimit          = 0;
static int   s_roomArmed        = 0;
static int   s_roomStage        = 0;   // 0-based g_stageId
static int   s_roomId           = 0;   // room byte
static int   s_demoArmed        = 0;
static int   s_demoId           = 0;   // pdemoN.dat index, 0..3
static int   s_demoMaxOverride  = 0;   // --test-demo-max, 0 = file header
static int   s_anyTestArmed     = 0;   // any --test-* flag: no live input

static char  s_tracePath[1024]  = {};
static char  s_captureDir[1024] = {};
static int   s_captureEvery     = 0;
static int   s_exitAfter        = 0;
static int   s_pressTick        = -1;
static int   s_pressHold        = 0;

// Trace runtime state
static FILE*          s_traceFile = NULL;
static int            s_traceArmed = 0;   // room 106 live, transition done
static int            s_pendingTick = 0;  // a gameplay tick ran this main_loop
static unsigned int   s_tick = 0;         // next traced tick index (t=)
static unsigned int   s_capturesWritten = 0;

// Pre-present capture stash (see TestHooks_OnBeforePresent): the back buffer
// is only valid before MarniPresent, so the frame is captured there and the
// trace line consumes the stash after main_loop returns.
static void*          s_capturedPixels = NULL;
static DWORD          s_capturedW = 0, s_capturedH = 0;
static unsigned int   s_capturedHash = 0;
static unsigned int   s_lastHash = 0;     // last presented frame's hash
static int            s_capturedThisTick = 0;

// ---------------------------------------------------------------------------
// Command line parsing. WinMain's lpCmdLine has no program name. Tokens are
// split on whitespace with double-quote awareness so paths may contain spaces.
// ---------------------------------------------------------------------------
static void TestHooks_Parse(const char* cmdLine)
{
    if (cmdLine == NULL) return;

    static char buf[4096];
    size_t len = strlen(cmdLine);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, cmdLine, len);
    buf[len] = '\0';

    char* tokens[64];
    int   tokenCount = 0;
    char* p = buf;
    while (*p != '\0' && tokenCount < 64) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;
        if (*p == '"') {
            p++;
            tokens[tokenCount++] = p;
            while (*p != '\0' && *p != '"') p++;
            if (*p != '\0') *p++ = '\0';
        } else {
            tokens[tokenCount++] = p;
            while (*p != '\0' && *p != ' ' && *p != '\t') p++;
            if (*p != '\0') *p++ = '\0';
        }
    }

    for (int i = 0; i < tokenCount; i++) {
        const char* t = tokens[i];
        if (strcmp(t, "--test-newgame") == 0 && i + 1 < tokenCount) {
            s_newGameArmed = 1;
            s_newGameCharacter = atoi(tokens[++i]);
        } else if (strcmp(t, "--test-room") == 0 && i + 1 < tokenCount) {
            // Three hex digits: stage (1-based) + room byte, e.g. 101.
            const char* v = tokens[++i];
            if (strlen(v) == 3 &&
                isxdigit((unsigned char)v[0]) &&
                isxdigit((unsigned char)v[1]) &&
                isxdigit((unsigned char)v[2])) {
                int stage = (int)(v[0] <= '9' ? v[0] - '0' : (v[0] | 0x20) - 'a' + 10);
                int hi    = (int)(v[1] <= '9' ? v[1] - '0' : (v[1] | 0x20) - 'a' + 10);
                int lo    = (int)(v[2] <= '9' ? v[2] - '0' : (v[2] | 0x20) - 'a' + 10);
                s_roomStage = stage - 1;
                s_roomId = (hi << 4) | lo;
                s_roomArmed = 1;
                // A room boot is still a new game: it needs the character
                // select transition and the new-game player seeding.
                s_newGameArmed = 1;
            } else {
                fprintf(stderr, "[oracle] --test-room expects three hex digits (e.g. 101)\n");
            }
        } else if (strcmp(t, "--test-demo") == 0 && i + 1 < tokenCount) {
            int id = atoi(tokens[++i]);
            if (id < 0 || id > 3) {
                fprintf(stderr, "[oracle] --test-demo expects 0..3\n");
            } else {
                s_demoArmed = 1;
                s_demoId = id;
            }
        } else if (strcmp(t, "--test-demo-max") == 0 && i + 1 < tokenCount) {
            s_demoMaxOverride = atoi(tokens[++i]);
        } else if (strcmp(t, "--test-trace") == 0 && i + 1 < tokenCount) {
            strncpy(s_tracePath, tokens[++i], sizeof(s_tracePath) - 1);
        } else if (strcmp(t, "--test-capture-every") == 0 && i + 1 < tokenCount) {
            s_captureEvery = atoi(tokens[++i]);
        } else if (strcmp(t, "--test-capture-dir") == 0 && i + 1 < tokenCount) {
            strncpy(s_captureDir, tokens[++i], sizeof(s_captureDir) - 1);
        } else if (strcmp(t, "--test-exit-after") == 0 && i + 1 < tokenCount) {
            s_exitAfter = atoi(tokens[++i]);
        } else if (strcmp(t, "--test-press") == 0 && i + 2 < tokenCount) {
            s_pressTick = atoi(tokens[++i]);
            s_pressHold = atoi(tokens[++i]);
        } else if (strcmp(t, "--test-nolimit") == 0) {
            s_noLimit = 1;
        }
    }

    // Any recognised test flag means the oracle harness owns this run and
    // there is no human at the keyboard (see TestHooks_LivePadDisabled).
    s_anyTestArmed = s_newGameArmed || s_roomArmed || s_demoArmed ||
                     s_tracePath[0] != '\0' || s_captureDir[0] != '\0' ||
                     s_captureEvery > 0 || s_exitAfter > 0 ||
                     s_pressTick >= 0 || s_noLimit;
}

// ---------------------------------------------------------------------------
// Create every component of a directory path (plat_mkdir is one level).
// ---------------------------------------------------------------------------
static void TestHooks_MakeDirs(const char* path)
{
    if (path == NULL || path[0] == '\0') return;

    static char tmp[1024];
    size_t n = strlen(path);
    if (n >= sizeof(tmp)) return;
    memcpy(tmp, path, n + 1);

    // Skip the drive prefix ("X:") or a leading '/', so the first component
    // tried is a real directory name.
    char* q = tmp;
    if (n >= 2 && tmp[1] == ':') {
        q = tmp + 2;
        if (*q == '\\' || *q == '/') q++;
    } else {
        while (*q == '\\' || *q == '/') q++;
    }

    for (; *q != '\0'; q++) {
        if (*q == '\\' || *q == '/') {
            char saved = *q;
            *q = '\0';
            plat_mkdir(tmp);
            *q = saved;
        }
    }
    plat_mkdir(tmp);
}

// ---------------------------------------------------------------------------
// TestHooks_Init
// ---------------------------------------------------------------------------
void TestHooks_Init(const char* cmdLine)
{
    TestHooks_Parse(cmdLine);

    if (s_newGameArmed) {
        fprintf(stderr, "[oracle] --test-newgame char=%d\n", s_newGameCharacter);
    }
    if (s_roomArmed) {
        fprintf(stderr, "[oracle] --test-room stage=%d room=0x%02X\n", s_roomStage + 1, s_roomId);
    }
    if (s_demoArmed) {
        fprintf(stderr, "[oracle] --test-demo id=%d%s\n", s_demoId,
                s_demoMaxOverride > 0 ? " (max override armed)" : "");
    }
    if (s_noLimit) {
        fprintf(stderr, "[oracle] --test-nolimit\n");
    }
    if (s_tracePath[0] != '\0') {
        s_traceFile = fopen(s_tracePath, "wb");
        if (s_traceFile != NULL) {
            fputs("oracle-trace v1 engine=ref\n", s_traceFile);
            fflush(s_traceFile);
            fprintf(stderr, "[oracle] trace %s (capture_every=%d dir=%s exit_after=%d)\n",
                    s_tracePath, s_captureEvery, s_captureDir, s_exitAfter);
        } else {
            fprintf(stderr, "[oracle] cannot open trace file %s\n", s_tracePath);
        }
    }
    if (s_captureDir[0] != '\0') {
        TestHooks_MakeDirs(s_captureDir);
    }
}

// ---------------------------------------------------------------------------
// Flag queries
// ---------------------------------------------------------------------------
int TestHooks_NewGameArmed(void)     { return s_newGameArmed; }
int TestHooks_NewGameCharacter(void) { return s_newGameCharacter; }
int TestHooks_NoLimit(void)          { return s_noLimit; }

// ---------------------------------------------------------------------------
// TestHooks_LivePadDisabled - TRUE whenever any --test-* flag is armed.
//
// The oracle harness always runs without a human: the scripted input comes
// from the trace scenario (--test-press bits, the attract reel's pdemoN
// stream), never from the keyboard or a pad. The live pad path must
// therefore read idle. It does not by itself: on a machine where the
// DirectInput keyboard state is not being refreshed for this process
// (unfocused window, remote session) keyboardCurr keeps a stale non-zero
// mask, JoyToPSX turns it into a phantom pad word, and StartAttractDemo's
// "any button held" check ends an attract reel on its first frame - the reel
// then cycles rooms every few ticks instead of playing. The port being
// compared has no live input at all, so the reference mirrors it by
// returning an idle pad from ReadPadBoth for the whole test run.
// ---------------------------------------------------------------------------
int TestHooks_LivePadDisabled(void)  { return s_anyTestArmed; }

// ---------------------------------------------------------------------------
// TestHooks_PadOverride - the --test-press window, in traced ticks. The
// message/loading narration needs a button press to advance; the Windows input
// path reads GetAsyncKeyState directly (RE1_TEST_KEYS' plat_key_state hook is
// dead code there), so the oracle injects the raw Cross bit (0x0040) into the
// frame's held word instead. Cross remaps to dpad bit 0x8000, the action
// button every menu/message advance reads.
// ---------------------------------------------------------------------------
unsigned int TestHooks_PadOverride(void)
{
    if (s_pressTick < 0 || !s_traceArmed) return 0;
    int tick = (int)s_tick;
    if (tick >= s_pressTick && tick < s_pressTick + s_pressHold) {
        return 0x0040u;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// TestHooks_SilenceSound
//
// oracle hook: silence the sound system for the deterministic run.
// Voice playback gates cutscene lines on wall-clock audio duration
// (UpdateMusicWaitState only clears MSF_VOICE_PLAYING once the voice bank
// reports finished), so with the engine running unthrottled the tick at
// which a scripted scene advances would depend on machine speed and audio
// buffer timing. The ported engine the trace is compared against has no audio
// at all, so the reference must not let audio pace the script either. Clearing
// the DirectSound initialised flag at +0x10 (the exact word every sound
// wrapper tests - loadSndBankFromWav, GetStatus, set_volume, ...) makes
// every bank load and getSndStat call a no-op, so UpdateMusicWaitState
// clears MSF_VOICE_PLAYING on the next frame and the script advances on
// frame counts alone. Both platform sound classes carry the same
// shared-wrapper gate DWORD at +0x10 (MarniSound.h / linux/audio.cpp), so
// the hack is not Windows-only. Test-only; only called from the armed hooks.
// ---------------------------------------------------------------------------
static void TestHooks_SilenceSound(void)
{
    if (g_pDirectSound != NULL) {
        *(int*)((BYTE*)g_pDirectSound + 0x10) = 0;
    }
}

// ---------------------------------------------------------------------------
// TestHooks_StartNewGame
//
// Faithful reproduction of CharacterSelectionScreen's case 6 (case_6, source
// lines 696-709: g_SelectedCharactedId / MSF_CHAR_VARIANT / g_bGameActive /
// nullsub_0047eb80 / the three cleanup_texture_slot(12/13/14) calls) followed
// by DisplayIntroAndStartGame (lines 24-32) minus the intro FMV.
//
// The FMV is the only thing dropped: MSF_FMV_REQUEST and g_selectedFmvId are
// NOT set, so game_start runs the room 106 intro directly. The texture slot
// cleanup is kept although this boot never loaded the character-select
// textures into those slots - cleanup_texture_slot is safe on empty slots and
// keeping it preserves the original state transition exactly.
// ---------------------------------------------------------------------------
void TestHooks_StartNewGame(void)
{
    g_SelectedCharactedId = (unsigned char)s_newGameCharacter;
    if (s_newGameCharacter != 0) {
        g_main_state_flags |= MSF_CHAR_VARIANT;
    }
    g_bGameActive = 2;
    nullsub_0047eb80();
    cleanup_texture_slot(12);
    cleanup_texture_slot(13);
    cleanup_texture_slot(14);

    // DisplayIntroAndStartGame with the FMV request omitted.
    sounds_reset();
    TestHooks_SilenceSound();

    g_main_state_flags |= MSF_PANNING_RESET;
    // The background quad is modulated by the global sprite colour, which the
    // normal boot leaves at white because LogosScreen calls
    // setSomeColor(128,128,128) before the title. This hook skips the logos,
    // so it must reproduce that side effect or every room background renders
    // multiplied to black (g_color_* default 0.0).
    extern void setSomeColor(int r, int g, int b);
    setSomeColor(128, 128, 128);
    // re1_srand(1): normalise the platform PRNG before the room runs, the
    // same seed both engines start a new game from (trace-v1.md "Trace origin").
    re1_srand(1);
    nullsub_0047eb80();
    Task_sleep(1);
    Task_chain((void*)game_start);
}

// ---------------------------------------------------------------------------
// --test-demo: boot straight into an attract demo reel.
// ---------------------------------------------------------------------------
int TestHooks_DemoArmed(void)
{
    return s_demoArmed;
}

// ---------------------------------------------------------------------------
// TestHooks_StartAttractDemo
//
// Reproduces the state the title's idle timeout leaves when it chains
// game_start, without running the title/logos/FMV states:
//
//   update_title_options, g_titleMode 0 case 2 (TitleScreen.cpp:612): the
//   idle countdown g_titleDemoTime hits 0 and arms the fade-out
//   (g_fade_type_id 2, g_fading_counter 0x400). Case 3 waits for the fade to
//   reach black and then (TitleScreen.cpp:627) sets g_titleSelectionId = 0,
//   pins g_fading_state = 0x7FFF / g_fading_counter = 0 and calls
//   title_exit_loop() (g_titleLoopFlag = 0, MSF_SCREEN_STANDALONE). title_state
//   then runs case 0: nullsub_0047eb80(), sets MSF2_ATTRACT_DEMO and chains
//   game_start. (The Task_chain(logos_state) that follows in that case is
//   unreachable: Task_chain yields and the task restarts at game_start.)
//
// g_CurrentAttractModeId is pinned BEFORE game_start so
// LoadAttractModePlayerData loads pdemoN.dat. The real title lets it cycle
// 0..3: the load copies reels[g_CurrentAttractModeId] and then advances the
// index, so reel N is what the next title timeout would play. The 0xFC00
// black fade-in of the g_titleMode 1 "options" path is NOT part of this state.
//
// The opening FMV is not requested (MSF_FMV_REQUEST stays clear), so
// game_start runs the reel's recorded room directly.
// ---------------------------------------------------------------------------
void TestHooks_StartAttractDemo(void)
{
    g_CurrentAttractModeId = (WORD)s_demoId;
    g_main_state_flags2 |= MSF2_ATTRACT_DEMO;

    g_titleSelectionId = 0;
    g_titleLoopFlag = 0;
    g_main_state_flags = (g_main_state_flags & ~MSF_SCREEN_MODE_MASK) | MSF_SCREEN_STANDALONE;
    g_fading_state = 0x7FFF;
    g_fading_counter = 0;

    nullsub_0047eb80();
    TestHooks_SilenceSound();

    // The attract demo is normally entered from the title screen, whose menu
    // path leaves the screen origin at the menu offset; the room draw expects
    // the game origin. Center it here like the game start does so the room
    // background quad lands full-screen in oracle captures.
    CenterScreenOrigin();

    // The background quad is modulated by the global sprite colour. The normal
    // boot leaves it white: logos_state calls setSomeColor(128,128,128) before
    // chaining title_state, and nothing on the title/attract path changes it.
    // This hook skips the logos state, so without this call g_color_* stays at
    // its 0.0 default and OT_InsertPrimitive builds the background quad with a
    // black colour: the dark base layer of every room background is hidden,
    // leaving only the lit mask overlays visible on black. TestHooks_StartNewGame
    // reproduces the same side effect for the new-game boot.
    extern void setSomeColor(int r, int g, int b);
    setSomeColor(128, 128, 128);

    // The attract path's own PRNG normalisation is game_loop's srand(0) once
    // MSF2_ATTRACT_DEMO is set (GameLoop.cpp:61-64) - the PC attract reels were
    // recorded against that seed. Pinning it here as well makes anything
    // InitializeGame draws before game_loop runs reproducible too.
    // (LoadAttractModePlayerData itself does not reseed.)
    re1_srand(0);

    Task_chain((void*)game_start);
}

// ---------------------------------------------------------------------------
// TestHooks_OnDemoDataLoaded - called from InitializeGame right after
// LoadAttractModePlayerData scattered pdemoN.dat over the globals. Logs the
// reel identity (for the run report) and applies --test-demo-max, which must
// land after the file load because the header's demoTimerMax is what the load
// copies into g_DemoTimerMax. No-op without --test-demo.
// ---------------------------------------------------------------------------
void TestHooks_OnDemoDataLoaded(void)
{
    if (!s_demoArmed) return;

    fprintf(stderr,
            "[oracle] demo reel %d loaded: stage=%u room=0x%02X char=%u "
            "pos=(%d,%d) dir=%d demoTimerCur=%d demoTimerMax=%d\n",
            s_demoId,
            (unsigned)g_AttractDemoData.stageId,
            (unsigned)g_AttractDemoData.roomId,
            (unsigned)g_AttractDemoData.characterId,
            (int)g_AttractDemoData.playerPosX,
            (int)g_AttractDemoData.playerPosZ,
            (int)g_AttractDemoData.playerDirAngle,
            (int)g_DemoTimerCur,
            (int)g_DemoTimerMax);

    if (s_demoMaxOverride > 0) {
        g_DemoTimerMax = (short)s_demoMaxOverride;
        fprintf(stderr, "[oracle] --test-demo-max: demoTimerMax=%d\n", (int)g_DemoTimerMax);
    }
}

// ---------------------------------------------------------------------------
// --test-room: a new game that boots directly into the requested room. The
// room override lands before init_room, the spawn placement after it, so the
// load uses the requested room and the player starts at the room's first walk
// zone centre (the port's headless `--room` spawn).
// ---------------------------------------------------------------------------
int TestHooks_RoomArmed(void)
{
    return s_roomArmed;
}

void TestHooks_ApplyRoomOverride(void)
{
    if (!s_roomArmed) return;
    g_stageId = (unsigned char)s_roomStage;
    g_roomId = (unsigned char)s_roomId;
}

void TestHooks_PlaceRoomSpawn(void)
{
    if (!s_roomArmed) return;

    int px = 0;
    int pz = 0;
    if (g_RdtPointer != NULL && g_RdtPointer->walk_zones != NULL) {
        unsigned char* base = (unsigned char*)g_RdtPointer->walk_zones;
        if (base[0] > 0) {
            short x1 = *(short*)(base + 2);
            short z1 = *(short*)(base + 4);
            short x2 = *(short*)(base + 6);
            short z2 = *(short*)(base + 8);
            px = ((int)x1 + (int)x2) / 2;
            pz = ((int)z1 + (int)z2) / 2;
        }
    }

    g_playerEntity.scaMatrixData.localMatrix.t[0] = px;
    g_playerEntity.scaMatrixData.localMatrix.t[1] = 0;
    g_playerEntity.scaMatrixData.localMatrix.t[2] = pz;
    g_playerEntity.position.x = (short)px;
    g_playerEntity.position.y = 0;
    g_playerEntity.position.z = (short)pz;
    g_playerEntity.posY = 0;
    g_playerEntity.directionAngle = 0;

    fprintf(stderr, "[oracle] --test-room spawn (%d,%d)\n", px, pz);
}

// ---------------------------------------------------------------------------
// TestHooks_OnGameplayTick
// ---------------------------------------------------------------------------
void TestHooks_OnGameplayTick(void)
{
    if (s_traceFile == NULL) return;

    if (!s_traceArmed) {
        // Tick 0 is the first gameplay tick with the traced room live and the
        // room transition finished: the room `--test-room` asked for, the
        // attract reel's own recorded room under --test-demo, or the new-game
        // room (mansion 1F, room 6 = ROOM106) without either flag.
        int stageMatches;
        int roomMatches;
        if (s_roomArmed) {
            stageMatches = (g_stageId == (unsigned char)s_roomStage);
            roomMatches = (g_roomId == (unsigned char)s_roomId);
        } else if (s_demoArmed) {
            // g_AttractDemoData still holds the pdemoN.dat header the reel was
            // loaded from (LoadAttractModePlayerData only advances the
            // g_CurrentAttractModeId cycle, not the loaded header).
            stageMatches = (g_stageId == g_AttractDemoData.stageId);
            roomMatches = (g_roomId == g_AttractDemoData.roomId);
        } else {
            stageMatches = (g_stageId == (unsigned char)STAGE_MANSION_1F);
            roomMatches = (g_roomId == (unsigned char)ROOM_MAIN_HALL);
        }
        if (stageMatches && roomMatches &&
            (g_main_state_flags & MSF_ROOM_TRANSITION) == 0) {
            s_traceArmed = 1;
        } else {
            return;
        }
    }
    // TEMP DIAGNOSTIC: why does the attract watchdog end the reel at tick 0?
    if (s_demoArmed && s_tick < 60) {
        fprintf(stderr,
                "[oracle-debug] t%u cur=%d max=%d pos=(%d,%d) ang=%d beh=%d as=%d anim=%d frame=%d "
                "j5=(%d,%d,%d) j3=(%d,%d,%d) blend=%d\n",
                s_tick, (int)g_DemoTimerCur, (int)g_DemoTimerMax,
                g_playerEntity.scaMatrixData.localMatrix.t[0],
                g_playerEntity.scaMatrixData.localMatrix.t[2],
                (int)g_playerEntity.directionAngle,
                (int)g_playerEntity.action_behavior, (int)g_playerEntity.action_state,
                (int)g_playerEntity.animationId, (int)g_playerEntity.animation_frame_id,
                (int)g_playerEntity.jointsStructs[5].rotation.x,
                (int)g_playerEntity.jointsStructs[5].rotation.y,
                (int)g_playerEntity.jointsStructs[5].rotation.z,
                (int)g_playerEntity.jointsStructs[3].rotation.x,
                (int)g_playerEntity.jointsStructs[3].rotation.y,
                (int)g_playerEntity.jointsStructs[3].rotation.z);
    }
    s_pendingTick = 1;
}

// ---------------------------------------------------------------------------
// FNV-1a 32-bit over (w u32 LE, h u32 LE, RGBA bytes) - trace-v1.md.
// ---------------------------------------------------------------------------
static unsigned int Fnv1a(const unsigned char* data, size_t size, unsigned int h)
{
    for (size_t i = 0; i < size; i++) {
        h ^= data[i];
        h *= 0x01000193u;
    }
    return h;
}

static unsigned int FrameHash(unsigned int w, unsigned int h, const unsigned char* rgba)
{
    unsigned char dims[8];
    dims[0] = (unsigned char)(w & 0xFF);
    dims[1] = (unsigned char)((w >> 8) & 0xFF);
    dims[2] = (unsigned char)((w >> 16) & 0xFF);
    dims[3] = (unsigned char)((w >> 24) & 0xFF);
    dims[4] = (unsigned char)(h & 0xFF);
    dims[5] = (unsigned char)((h >> 8) & 0xFF);
    dims[6] = (unsigned char)((h >> 16) & 0xFF);
    dims[7] = (unsigned char)((h >> 24) & 0xFF);

    unsigned int hash = 0x811c9dc5u;
    hash = Fnv1a(dims, sizeof(dims), hash);
    hash = Fnv1a(rgba, (size_t)w * h * 4, hash);
    return hash;
}

// ---------------------------------------------------------------------------
// Line building helper (appends to buf, returns the new length; a truncated
// buffer keeps growing past capacity and is clamped at the end).
// ---------------------------------------------------------------------------
static int Append(char* buf, size_t cap, int used, const char* fmt, ...)
{
    if (used < 0) return used;
    if ((size_t)used >= cap) return (int)cap;

    va_list args;
    va_start(args, fmt);
    int wrote = vsnprintf(buf + used, cap - (size_t)used, fmt, args);
    va_end(args);

    if (wrote < 0) return used;
    if ((size_t)wrote >= cap - (size_t)used) return (int)cap;
    return used + wrote;
}

// ---------------------------------------------------------------------------
// Capture file: RE1CAP <w> <h>\n followed by top-down RGBA bytes.
// CaptureBackbufferToRGBA copies D3D11 staging rows in order, and staging row
// 0 is the top row, so the returned buffer is already top-down: no flip.
// ---------------------------------------------------------------------------
static void WriteCapture(const void* pixels, unsigned int w, unsigned int h)
{
    char path[1600];
    size_t n = strlen(s_captureDir);
#ifdef _WIN32
    const char* sep = "\\";
#else
    const char* sep = "/";
#endif
    if (n > 0 && (s_captureDir[n - 1] == '\\' || s_captureDir[n - 1] == '/')) {
        sep = "";
    }
    snprintf(path, sizeof(path), "%s%sf%05u.rgba", s_captureDir, sep, s_tick);

    FILE* f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "[oracle] cannot write capture %s\n", path);
        return;
    }
    fprintf(f, "RE1CAP %u %u\n", w, h);
    fwrite(pixels, 1, (size_t)w * h * 4, f);
    fclose(f);
    s_capturesWritten++;
}

// ---------------------------------------------------------------------------
// TestHooks_OnBeforePresent - capture this tick's rendered frame while the
// back buffer is still valid. Called from FrameRateGovernor immediately before
// MarniPresent. With a DXGI discard swap chain the buffer read after Present
// is undefined (observed as an all-black frame), which is why the capture
// lives here and not in TestHooks_OnFrameCompleted.
//
// A tick whose screen is masked (StMask holds g_ScreenAccessReady low, so
// FrameRateGovernor presents nothing) keeps the previous stash: the display
// still shows the previous frame, and the trace's f= carries its hash.
// ---------------------------------------------------------------------------
void TestHooks_OnBeforePresent(void)
{
    if (s_traceFile == NULL || !s_traceArmed || !s_pendingTick) return;
    if (s_capturedThisTick) return;

    void* pixels = NULL;
    DWORD w = 0, h = 0;
    MarniDX* dx = Marni_DX();
    if (dx != NULL && dx->CaptureBackbufferToRGBA(&pixels, &w, &h) && pixels != NULL) {
        if (s_capturedPixels != NULL) free(s_capturedPixels);
        s_capturedPixels = pixels;
        s_capturedW = w;
        s_capturedH = h;
        s_capturedHash = FrameHash((unsigned int)w, (unsigned int)h, (const unsigned char*)pixels);
        s_lastHash = s_capturedHash;
        s_capturedThisTick = 1;

        if (w != 320 || h != 240) {
            static int warned = 0;
            if (!warned) {
                warned = 1;
                fprintf(stderr, "[oracle] captured backbuffer is %ux%u, expected 320x240\n",
                        (unsigned)w, (unsigned)h);
            }
        }
        if (s_captureEvery > 0 && (s_tick % (unsigned int)s_captureEvery) == 0) {
            WriteCapture(pixels, (unsigned int)w, (unsigned int)h);
        }
    } else {
        static int warned = 0;
        if (!warned) {
            warned = 1;
            fprintf(stderr, "[oracle] backbuffer capture failed\n");
        }
    }
}

// ---------------------------------------------------------------------------
// TestHooks_OnFrameCompleted - emit one trace line for the tick that just ran.
// Called from RunMessageLoop immediately after main_loop() returns; the frame
// pixels come from the pre-present stash.
// ---------------------------------------------------------------------------
void TestHooks_OnFrameCompleted(void)
{
    if (s_traceFile == NULL || !s_traceArmed || !s_pendingTick) return;
    s_pendingTick = 0;

    unsigned int frameHash = s_capturedThisTick ? s_capturedHash : s_lastHash;

    char line[16384];
    int used = 0;

    // The room token is the RDT identity the Rust side prints as
    // `RoomId::room3()`-style `stage:room`: 1-based stage digit + the room
    // byte (ROOM106 = stage 1, g_roomId 0x06 -> "1:6"). The leading stage
    // digit is what names the RDT file; the room itself stays the byte.
    used = Append(line, sizeof(line), used,
                  "t=%u room=%u:%u rng=%u rseed=%u fade=%d:%d p=%d,%d,%d,%u,%u,%u,%d cam=%u",
                  s_tick,
                  (unsigned int)(g_stageId + 1),
                  (unsigned int)g_roomId,
                  re1_rand_state(),
                  (unsigned int)(unsigned short)g_RandSeed,
                  (int)g_fading_counter,
                  (int)g_fading_state,
                  // The live player position is the matrix translation: the
                  // `position` mirror is only synced by the init and some walk
                  // handlers, so a grabbed/bitten player keeps a stale value
                  // there. Entity slots use the matrix for the same reason.
                  g_playerEntity.scaMatrixData.localMatrix.t[0],
                  g_playerEntity.scaMatrixData.localMatrix.t[1],
                  g_playerEntity.scaMatrixData.localMatrix.t[2],
                  (unsigned int)(unsigned short)g_playerEntity.directionAngle & 0x0FFF,
                  (unsigned int)g_playerEntity.action_behavior,
                  (unsigned int)g_playerEntity.action_state,
                  (int)g_playerEntity.health,
                  (unsigned int)g_roomCameraId);

    // Entity slots: 0 = player (always), i >= 1 = g_EnemiesList[i-1] only when
    // active (status_flags & 1). The player is read through the shared Entity
    // base layout (PlayerEntity is layout-compatible there: 0x84 state/animationId,
    // 0x8A hit_state/isBeingAttackedFlag, 0x00 status_flags/flags, 0xC4
    // action_ticks_counter/attackDirection).
    //
    // Every slot, the player included, emits the live matrix translation: the
    // `position` field is only synced by the init and some walk handlers, so a
    // grabbed/bitten player or a scripted NPC keeps a stale value there. The
    // matrix is what arklay's Entity::pos carries.
    for (int slot = 0; slot <= 30; slot++) {
        const Entity* e;
        if (slot == 0) {
            e = (const Entity*)&g_playerEntity;
        } else {
            e = &g_EnemiesList[slot - 1];
            if ((e->status_flags & 0x01) == 0) continue;
        }
        int px = e->scaMatrixData.localMatrix.t[0];
        int py = e->scaMatrixData.localMatrix.t[1];
        int pz = e->scaMatrixData.localMatrix.t[2];
        used = Append(line, sizeof(line), used,
                      " e=%d,%u,%d,%d,%d,%u,%u,%u,%u,%d,%u,%u,%u",
                      slot,
                      (unsigned int)e->id,
                      px, py, pz,
                      (unsigned int)(unsigned short)e->angle & 0x0FFF,
                      (unsigned int)e->state,
                      (unsigned int)e->action_behavior,
                      (unsigned int)e->action_state,
                      (int)e->health,
                      (unsigned int)e->hit_state,
                      (unsigned int)e->status_flags,
                      (unsigned int)e->action_ticks_counter);
    }

    used = Append(line, sizeof(line), used, " f=%u\n", frameHash);
    if ((size_t)used > sizeof(line) - 1) used = (int)sizeof(line) - 1;

    // The stash is consumed by this line; the next tick captures afresh.
    if (s_capturedPixels != NULL) {
        free(s_capturedPixels);
        s_capturedPixels = NULL;
    }
    s_capturedThisTick = 0;

    fwrite(line, 1, (size_t)used, s_traceFile);
    fflush(s_traceFile);

    s_tick++;

    // Exit cleanly once the requested number of traced ticks has been emitted.
    // The trace is already flushed and closed; no window/D3D teardown is
    // needed for the trace to be intact.
    if (s_exitAfter > 0 && s_tick >= (unsigned int)s_exitAfter) {
        fflush(s_traceFile);
        fclose(s_traceFile);
        s_traceFile = NULL;
        fprintf(stderr, "[oracle] done: %u traced ticks, %u captures\n",
                s_tick, s_capturesWritten);
        exit(0);
    }
}
