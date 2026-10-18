// TestHooks.h - oracle test hooks (port-only, test-gated)
//
// Deterministic oracle support for the arklay-rs trace contract
// (M:\git\arklay-rs\oracle\trace-v1.md). Everything here is inert unless the
// matching --test-* flag was passed on the command line, so the default
// behaviour of the build is unchanged.
//
//   --test-newgame <0|1>       boot straight into a new game (0 Chris, 1 Jill)
//   --test-room <3hex>         boot the new game directly into this room
//                              (stage digit + room byte, e.g. 101), spawning
//                              the player at the first walk zone's centre and
//                              arming the trace on that room instead of 106
//   --test-demo <0..3>         boot straight into attract demo reel N
//                              (JPN/DATA/pdemoN.dat), reproducing the title's
//                              idle-timeout entry; the trace arms on the
//                              reel's recorded room
//   --test-demo-max <ticks>    override the reel's recorded length (the
//                              pdemo header's demoTimerMax) after the load
//   --test-load <path>         boot from the save block at <path> (a
//                              `savedatN.dat` image); the trace arms on the
//                              room and character the save records
//   --test-ending <1..7>       boot straight into the ending sequence with
//                              this ending id, skipping the ending films and
//                              the staff roll; the trace arms on the first
//                              frame (the ending has no gameplay ticks)
//   --test-ending-char <0|1>   character the --test-ending run shows (0 Chris,
//                              1 Jill); selects the RESULT screen's backdrop
//   --test-trace <path>        write the per-tick oracle trace to <path>
//   --test-capture-every <N>   write a captured frame every N traced ticks
//   --test-capture-dir <path>  directory for the f%05d.rgba captures
//   --test-exit-after <N>      exit(0) after N traced ticks
//   --test-input <path>        scripted per-tick pad playback (one
//                              `<tick> <button>...` entry per line; buttons:
//                              up/down/left/right, confirm/action, cancel,
//                              aim, start/inventory, options/select). Arms a
//                              per-frame trace so pause-menu frames are
//                              captured too, not just gameplay ticks.
//   --test-nolimit             disable the 16/33 ms frame limiter
//
// None of this exists in the original binary; it is test scaffolding for the
// Rust port's oracle harness.
#pragma once

// Parse the WinMain command line once. Call before the game starts (WinMain).
void TestHooks_Init(const char* cmdLine);

// TRUE when --test-newgame was passed.
int TestHooks_NewGameArmed(void);

// Selected character id (0 = Chris, 1 = Jill).
int TestHooks_NewGameCharacter(void);

// Reproduces CharacterSelectionScreen case 6 + DisplayIntroAndStartGame minus
// the intro FMV, then chains task 0 to game_start. Call from task context
// (the tail of load_global_assets).
void TestHooks_StartNewGame(void);

// TRUE when --test-demo was passed.
int TestHooks_DemoArmed(void);

// Boots straight into attract demo reel N: pins g_CurrentAttractModeId before
// game_start so LoadAttractModePlayerData loads pdemoN.dat, sets
// MSF2_ATTRACT_DEMO and restores the state the title's idle timeout leaves
// (title_state case 0 reached from update_title_options state 3), then chains
// task 0 to game_start. Call from task context (the tail of
// load_global_assets).
void TestHooks_StartAttractDemo(void);

// Called from InitializeGame immediately after LoadAttractModePlayerData:
// logs the loaded reel's identity and applies --test-demo-max to
// g_DemoTimerMax. No-op unless --test-demo was passed.
void TestHooks_OnDemoDataLoaded(void);

// TRUE when --test-load was passed.
int TestHooks_LoadArmed(void);

// Restores the save block --test-load named (DebugQuick_LoadFile), captures
// its stage/room for the trace arming, rebuilds the state the title's load
// flow leaves and chains game_start, whose InitializeGame continue branch
// rebuilds the saved stage. Call from task context (the tail of
// load_global_assets). A refused save (missing/short/DC-in-OG) logs and does
// not start the game.
void TestHooks_StartLoad(void);

// TRUE when --test-ending was passed.
int TestHooks_EndingArmed(void);

// The --test-ending ending id (1..7).
int TestHooks_EndingId(void);

// Boots straight into the ending sequence: sets the completed run's state the
// ending reads (the character, the clear-time snapshot and the save counter)
// and chains ending_state. The ending films and the staff roll are skipped
// while the flag is armed (see EndingScreen.cpp). Call from task context (the
// tail of load_global_assets).
void TestHooks_StartEnding(void);

// TRUE when --test-room was passed.
int TestHooks_RoomArmed(void);

// Apply the --test-room stage/room override. Called from InitializeGame just
// before init_room(), after the new-game path has seeded the character.
void TestHooks_ApplyRoomOverride(void);

// Place the player at the overridden room's first walk zone centre (the spawn
// the port's headless `--room` boot uses). Called from InitializeGame right
// after init_room() has loaded the room.
void TestHooks_PlaceRoomSpawn(void);

// TRUE when --test-nolimit was passed (RunMessageLoop focus/pacing bypass).
int TestHooks_NoLimit(void);

// Raw PSX pad bits to OR into the frame's held word, for the ticks selected by
// --test-press <tick> <hold> (the action/Cross bit 0x0040). Called from
// PlayerPad_Update right after the raw held word is read. 0 when not armed or
// outside the window.
unsigned int TestHooks_PadOverride(void);

// TRUE when any --test-* flag is armed: the oracle harness runs without a
// human, so the live pad must read idle (see TestHooks.cpp). ReadPadBoth
// returns 0 while this is set, which keeps a stale DirectInput keyboard mask
// from ending an attract reel through StartAttractDemo's held-button check.
int TestHooks_LivePadDisabled(void);

// Called once per completed gameplay tick from the bottom of game_loop's inner
// frame loop. Arms the trace on the first tick of the new-game room 106 and
// marks a line as pending for TestHooks_OnFrameCompleted.
void TestHooks_OnGameplayTick(void);

// Called once per frame from main_loop just before FrameRateGovernor. With a
// --test-input script armed, arms the trace and marks every frame past the arm
// point - including the frames a pause menu owns, when game_loop is suspended
// and TestHooks_OnGameplayTick never fires. No-op without --test-input.
void TestHooks_OnFrameEnd(void);

// Called from FrameRateGovernor immediately before MarniPresent, while the
// back buffer still holds this tick's rendered frame (after Present the
// DXGI discard swap chain's buffer is undefined). Captures and hashes the
// frame for the pending tick. A tick whose screen is masked (no present)
// keeps the previous capture, exactly as the screen keeps the previous frame.
void TestHooks_OnBeforePresent(void);

// Called from RunMessageLoop right after main_loop() returns, i.e. after the
// tick's frame has been presented. Emits the pending trace line from the
// pixels stashed by TestHooks_OnBeforePresent; exits the process once
// --test-exit-after ticks are done.
void TestHooks_OnFrameCompleted(void);

// oracle hook (port-only): the platform PRNG state (Globals.cpp g_randState),
// for the trace's `rng=` field.
extern "C" unsigned int re1_rand_state(void);
