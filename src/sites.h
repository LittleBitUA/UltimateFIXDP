// Patch sites of DP.exe 1.01b (Steam). Every site carries the exact bytes expected there. The DLL
// refuses to touch a site whose bytes differ, and tests/verify_sites.cpp checks against a real DP.exe on
// the host every site listed in kAllSites, the longer byte runs in kAllTails, the shadow literals and the
// import slots in kImports. This header must stay free of Windows headers.
#pragma once
#include <stdint.h>

namespace ufx {

struct SiteDef {
    const char* name;
    uint32_t va;
    uint8_t len;
    uint8_t bytes[16];
};

struct ImportDef {
    const char* dll;
    const char* func;
    uint32_t slotVa;  // where the IAT slot lives in DP.exe 1.01b
};

constexpr uint32_t kExeTimeDateStamp = 0x529721DC;
constexpr uint32_t kExeSizeOfImage = 0x010B5000;
constexpr uint32_t kSaveFileSize = 0x7A2620;  // header 0x120 + 28 records of 0x45CC0

// frameDelta divide: fld [esp+10h] / fdiv [014AFFE0h] / fstp [esi+4E4h]
constexpr SiteDef kSiteZeroDelta = {"zero-delta divide", 0x58CB09, 16,
    {0xD9, 0x44, 0x24, 0x10, 0xD8, 0x35, 0xE0, 0xFF, 0x4A, 0x01, 0xD9, 0x9E, 0xE4, 0x04, 0x00, 0x00}};
constexpr uint32_t kZeroDeltaReturn = 0x58CB19;
constexpr uint32_t kFrameDeltaVa = 0x014AFFE0;

// HOUSE_LIST.NOD preprocessor: push 51h / push <format string> / push eax / call FUN_0070BBE0
constexpr SiteDef kSiteHouseListFormat = {"HOUSE_LIST format push", 0x408272, 5,
    {0x68, 0x20, 0xF1, 0x76, 0x00}};

// Extra joyGetPosEx(0, <uninitialised stack struct>) in the frame tick; the result is never read.
constexpr SiteDef kSiteExtraJoyPoll = {"extra joyGetPosEx", 0x401CA2, 13,
    {0x8D, 0x4C, 0x24, 0x08, 0x51, 0x6A, 0x00, 0xFF, 0x15, 0x50, 0xE2, 0x76, 0x00}};

// Skip logos: mov dword [014736D8h], 0B3h  ->  0
constexpr SiteDef kSiteSkipIntro = {"skip intro", 0x643F2D, 10,
    {0xC7, 0x05, 0xD8, 0x36, 0x47, 0x01, 0xB3, 0x00, 0x00, 0x00}};

// Right after CreateDevice the game copies the desktop refresh rate into the parameter block it later
// passes to Reset(): mov [0148BBA0h], edx. In windowed mode Reset must get 0 there, or it fails with
// D3DERR_INVALIDCALL and the game freezes after Alt+Tab. Without the store the block keeps the value
// CreateDevice was given (0 windowed, 59/60 fullscreen).
constexpr SiteDef kSiteResetRefresh = {"refresh store after CreateDevice", 0x6CC5BB, 6,
    {0x89, 0x15, 0xA0, 0xBB, 0x48, 0x01}};

// The one place the game calls IDirect3DDevice9::Present:  mov ecx,[edx+44h] / call ecx
constexpr SiteDef kSitePresentCall = {"Present call", 0x6CC9F5, 5, {0x8B, 0x4A, 0x44, 0xFF, 0xD1}};

// Inside the device-recovery routine: call FUN_006CC030 (release every default-pool resource), then Reset().
// Anything of ours that lives in the default pool must be released before that call.
constexpr SiteDef kSitePreResetCall = {"pre-Reset release call", 0x6CCF4A, 5, {0xE8, 0xE1, 0xF0, 0xFF, 0xFF}};
constexpr uint32_t kPreResetTarget = 0x6CC030;

constexpr uint32_t kGameWindowVa = 0x014AFFD8;      // HWND of the game window
constexpr uint32_t kFullscreenFlagVa = 0x0148BBC0;  // 0 = windowed, 1 = fullscreen
// D3DPRESENT_PARAMETERS the game hands to Reset(); width and height are 0 in windowed mode, which means
// "size of the window" and would make Reset() build a back buffer as big as our enlarged window.
constexpr uint32_t kResetParamsVa = 0x0148BB70;

// Texture filtering: the game sets MAXANISOTROPY (sampler state 0Ah) to 8 for 12 samplers every frame:
//     push 8 / push 0Ah / push <sampler> ...   (two copies, one per branch)
constexpr SiteDef kSiteAniso1 = {"max anisotropy (branch 1)", 0x6CC71A, 4, {0x6A, 0x08, 0x6A, 0x0A}};
constexpr SiteDef kSiteAniso2 = {"max anisotropy (branch 2)", 0x6CC811, 4, {0x6A, 0x08, 0x6A, 0x0A}};

// Reflection bias matrix (FUN_006E07D0) asks for the width twice: the second call at 006E07F9 should be
// the height getter (409F70) instead of the width getter (409F60).
constexpr SiteDef kSiteReflectionBias = {"reflection bias height", 0x6E07F9, 5, {0xE8, 0x62, 0x97, 0xD2, 0xFF}};
constexpr uint8_t kReflectionBiasFixed[5] = {0xE8, 0x72, 0x97, 0xD2, 0xFF};

// Start-up: the scene buffers are created by the call at 00401902 (FUN_006D1610) and the post-processing
// buffers by the call at 00401913 (FUN_006E8930). Both size everything from the logical resolution, two
// float globals (width 01480974, height 01480970) that are 1280x720 for the whole game otherwise.
constexpr SiteDef kSiteSceneInitCall = {"scene buffers init call", 0x401902, 5, {0xE8, 0x09, 0xFD, 0x2C, 0x00}};
constexpr SiteDef kSitePostInitCall = {"post-processing init call", 0x401913, 5, {0xE8, 0x18, 0x70, 0x2E, 0x00}};
constexpr uint32_t kSceneInitFn = 0x6D1610;
constexpr uint32_t kPostInitFn = 0x6E8930;
constexpr uint32_t kLogicalWidthVa = 0x01480974;
constexpr uint32_t kLogicalHeightVa = 0x01480970;

// Storage buffers of the blur/bloom passes are sized by literals, not by the logical resolution:
// push 504 / push 896 and push 252 / push 448 in front of two calls of the buffer-creation wrapper.
constexpr SiteDef kSiteStorageH1 = {"blur storage height 504", 0x6E8A72, 5, {0x68, 0xF8, 0x01, 0x00, 0x00}};
constexpr SiteDef kSiteStorageW1 = {"blur storage width 896", 0x6E8A77, 5, {0x68, 0x80, 0x03, 0x00, 0x00}};
constexpr SiteDef kSiteStorageH2 = {"blur storage height 252", 0x6E8A97, 5, {0x68, 0xFC, 0x00, 0x00, 0x00}};
constexpr SiteDef kSiteStorageW2 = {"blur storage width 448", 0x6E8A9C, 5, {0x68, 0xC0, 0x01, 0x00, 0x00}};

// Shadow maps: twelve `push 200h` / `push 400h` literals in FUN_006D1610, each right before a call of the
// buffer-creation wrapper (width and height of four 512x512 and eight 1024x1024 buffers).
struct ShadowSite {
    uint32_t va;
    uint32_t value;
};
constexpr ShadowSite kShadowSites[12] = {
    {0x6D1E02, 0x200}, {0x6D1E07, 0x200}, {0x6D1E40, 0x200}, {0x6D1E45, 0x200}, {0x6D1E7E, 0x400}, {0x6D1E83, 0x400},
    {0x6D1EFA, 0x400}, {0x6D1EFF, 0x400}, {0x6D1F38, 0x400}, {0x6D1F3D, 0x400}, {0x6D1F76, 0x400}, {0x6D1F7B, 0x400},
};

// ---- Output resolution (back buffer of the real output size, sharp 2D) ----------------------------------------
// A: the back buffer and its depth buffer.
constexpr SiteDef kOutPresentParams = {"back buffer size in the device parameters", 0x6CC55A, 15,
    {0xB9, 0x0E, 0x00, 0x00, 0x00, 0x8D, 0x75, 0xBC, 0xBF, 0x70, 0xBB, 0x48, 0x01, 0xF3, 0xA5}};
constexpr uint32_t kOutPresentParamsReturn = 0x6CC569;
constexpr SiteDef kOutDepthH = {"final depth buffer height", 0x6D1EBC, 5, {0x68, 0xD0, 0x02, 0x00, 0x00}};
constexpr SiteDef kOutDepthW = {"final depth buffer width", 0x6D1EC1, 5, {0x68, 0x00, 0x05, 0x00, 0x00}};
// B: the 2D state ("H state") = real viewport of the output size, logical resolution 1280x720.
constexpr SiteDef kOutFrameBegin1 = {"frame begin (branch 1)", 0x6CC7E9, 5, {0xE8, 0xF2, 0x81, 0xEE, 0xFF}};
constexpr SiteDef kOutFrameBegin2 = {"frame begin (branch 2)", 0x6CC8C0, 5, {0xE8, 0x1B, 0x81, 0xEE, 0xFF}};
constexpr SiteDef kOutPopViewport = {"viewport restore after a target pop", 0x6CCC0F, 5, {0xE8, 0x8C, 0xE1, 0xD7, 0xFF}};
constexpr SiteDef kOutHudDepthBind = {"HUD start (mov [0BD7798h], esi)", 0x401733, 6, {0x89, 0x35, 0x98, 0x77, 0xBD, 0x00}};
constexpr SiteDef kOutViewport1 = {"explicit viewport (fade)", 0x44A0FF, 5, {0xE8, 0x9C, 0x0C, 0x00, 0x00}};
constexpr SiteDef kOutViewport2 = {"explicit viewport (2D 1)", 0x5F6972, 5, {0xE8, 0x29, 0x44, 0xE5, 0xFF}};
constexpr SiteDef kOutViewport3 = {"explicit viewport (2D 2)", 0x600CF4, 5, {0xE8, 0xA7, 0xA0, 0xE4, 0xFF}};
constexpr SiteDef kOutReset1 = {"viewport reset (1)", 0x5F7304, 5, {0xE8, 0x27, 0x11, 0x00, 0x00}};
constexpr SiteDef kOutReset2 = {"viewport reset (2)", 0x601B47, 5, {0xE8, 0xE4, 0x68, 0xFF, 0xFF}};
constexpr SiteDef kOutMap1 = {"map viewport (1)", 0x65A1C3, 6, {0x8B, 0x92, 0xBC, 0x00, 0x00, 0x00}};
constexpr SiteDef kOutMap2 = {"map viewport (2)", 0x65D378, 6, {0x8B, 0x81, 0xBC, 0x00, 0x00, 0x00}};
constexpr SiteDef kOutMap3 = {"map viewport (3)", 0x65DD98, 6, {0x8B, 0x81, 0xBC, 0x00, 0x00, 0x00}};
constexpr SiteDef kOutMap4 = {"map viewport (4)", 0x65DEE0, 6, {0x8B, 0x81, 0xBC, 0x00, 0x00, 0x00}};
constexpr uint32_t kFrameBeginFn = 0x5B49E0;       // void FUN_005B49E0(void)
constexpr uint32_t kViewportFn = 0x44ADA0;         // void __cdecl FUN_0044ADA0(x, y, w, h, minZ, maxZ)
constexpr uint32_t kViewportResetFn = 0x5F8430;    // void FUN_005F8430(void): full 1280x720 viewport
constexpr uint32_t kLogicalRectFn = 0x44AD00;      // void __cdecl FUN_0044AD00(x, y, w, h, char setConstant)
constexpr uint32_t kRenderStackLevelPtr = 0xBD9E40;
constexpr uint32_t kDeviceVa = 0x01480984;         // IDirect3DDevice9* of the game
constexpr uint32_t kHudDepthSlotVa = 0xBD7798;

// Camera far planes: six `fld dword [abs]` that load the culling distance of each visibility class
// (200000, 80000, 20000, 5000, 1000, 500). Objects without any class bit land in the last class, so the
// 500-unit limit is what makes huts and vending machines vanish. The constants are shared with other code,
// so we point each instruction at our own slot instead of changing them.
constexpr SiteDef kFarPlane[6] = {
    {"far plane class 0", 0x6B654E, 6, {0xD9, 0x05, 0x14, 0x69, 0x82, 0x00}},
    {"far plane class 1", 0x6B655A, 6, {0xD9, 0x05, 0xDC, 0x9A, 0x77, 0x00}},
    {"far plane class 2", 0x6B6566, 6, {0xD9, 0x05, 0x08, 0x73, 0x77, 0x00}},
    {"far plane class 3", 0x6B6572, 6, {0xD9, 0x05, 0x58, 0x3C, 0x77, 0x00}},
    {"far plane class 4", 0x6B657E, 6, {0xD9, 0x05, 0x48, 0x26, 0x77, 0x00}},
    {"far plane class 5", 0x6B658A, 6, {0xD9, 0x05, 0x04, 0x2F, 0x77, 0x00}},
};

// ---- Gameplay -------------------------------------------------------------------------------------------------
// Difficulty byte: manager (pointer in 0BDA004h) + 8C60Bh. Low 7 bits = 0 Easy / 1 Normal / 2 Hard, bit 7 = "finished".
// The PC port starts every new game with `mov [eax+8C60B], bl` (bl = 0) and zeroes it again after loading a slot.
constexpr SiteDef kSiteDifficultyNewGame = {"difficulty store (new game)", 0x643AB7, 6, {0x88, 0x98, 0x0B, 0xC6, 0x08, 0x00}};
constexpr SiteDef kSiteDifficultyLoadClear = {"difficulty clear (load slot)", 0x4549C2, 7,
    {0xC6, 0x85, 0x0B, 0xC6, 0x08, 0x00, 0x00}};
// The title code sets state 6 (skip the selection) at these two places; state 3 shows the selection screen.
constexpr SiteDef kSiteDifficultyDialog1 = {"title: skip selection (no save)", 0x64328F, 10,
    {0xC7, 0x05, 0xD8, 0x36, 0x47, 0x01, 0x06, 0x00, 0x00, 0x00}};
constexpr uint32_t kDifficultyDialog1Return = 0x643299;
constexpr SiteDef kSiteDifficultyDialog2 = {"title: skip selection (with save)", 0x6435C4, 16,
    {0xC7, 0x05, 0xD8, 0x36, 0x47, 0x01, 0x06, 0x00, 0x00, 0x00, 0x89, 0x1D, 0xE0, 0x36, 0x47, 0x01}};
constexpr uint32_t kDifficultyDialog2Return = 0x6435D4;
constexpr uint32_t kTitleStateVa = 0x014736D8;
constexpr uint32_t kTitleCursorVa = 0x014736E0;
constexpr uint32_t kGameManagerPtrVa = 0x00BDA004;
constexpr uint32_t kDifficultyOffset = 0x8C60B;

// Frame time: `push 0 / call FUN_00701040 (QPC seconds as double in st0) / add esp,4`. Only the call at 00700A1A is ours.
constexpr SiteDef kSiteFrameClockCall = {"frame time call", 0x700A1A, 5, {0xE8, 0x21, 0x06, 0x00, 0x00}};
constexpr uint32_t kRealTimeFn = 0x701040;

// Wet reflections: the immediate of `mov dword [eax+670Ch], 0` (10 bytes, the immediate starts at +6).
constexpr SiteDef kSiteWetGate = {"wet reflection gate", 0x6D86E9, 10, {0xC7, 0x80, 0x0C, 0x67, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

// Aim: the camera clamps pitch/yaw with exact float comparisons against an x87 register, which fails by one
// ulp whenever the precision control differs. Both places get a comparison with a tolerance.
constexpr SiteDef kSiteAimH = {"aim limit (horizontal)", 0x53C18C, 16,
    {0xD9, 0x86, 0xA0, 0x00, 0x00, 0x00, 0xDA, 0xE9, 0xDF, 0xE0, 0xF6, 0xC4, 0x44, 0x7B, 0x29, 0xD9}};
// All 30 bytes of the horizontal sequence (SiteDef holds only 16): two fld/fucompp/fnstsw/test/jnp groups.
constexpr uint8_t kAimHFull[30] = {0xD9, 0x86, 0xA0, 0x00, 0x00, 0x00, 0xDA, 0xE9, 0xDF, 0xE0, 0xF6, 0xC4, 0x44, 0x7B, 0x29,
                                   0xD9, 0x86, 0xA0, 0x00, 0x00, 0x00, 0xDA, 0xE9, 0xDF, 0xE0, 0xF6, 0xC4, 0x44, 0x7B, 0x1C};
constexpr uint32_t kAimHNotEqual = 0x53C1AA;
constexpr uint32_t kAimHEqual = 0x53C1C6;
constexpr SiteDef kSiteAimV = {"aim limit (vertical)", 0x53C3B6, 8, {0xD9, 0x44, 0x24, 0x18, 0xD8, 0xCA, 0xDA, 0xE9}};
// The original continues with `fnstsw ax` (53C3BE) and `test ah,44h` (53C3C0); our code sets AH itself and returns to the test.
constexpr uint32_t kAimVExit = 0x53C3C0;

// ---- Stick profile ----------------------------------------------------------------------------------------
// FUN_00709400(this = CInput, slot, rec) turns the four raw axes into the floats the game reads (dead zone +-16/128,
// slew 0.5 per poll). The poll calls it once per slot; its first 6 bytes are `push ebp / mov ebp,esp / sub esp,14h`.
constexpr SiteDef kSiteStickEval = {"stick evaluator entry", 0x709400, 6, {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14}};
constexpr uint32_t kStickEvalReturn = 0x709406;
constexpr uint32_t kUseJoyVa = 0x014810F0;  // 0 = keyboard/mouse mode, 1 = controller mode
constexpr uint32_t kInputStickOffset = 0x9B8;  // LX, LY, RX, RY at this + 9B8h + slot * 4Ch
constexpr uint32_t kStickGetterFn = 0x708A30;  // float getter(this, pad, stick, axis)
// The four reads of the right stick in the aiming camera (FUN_0053B8B0): call 0x708A30.
constexpr SiteDef kSiteAimStick[4] = {
    {"aim camera stick read 1", 0x53C00B, 5, {0xE8, 0x20, 0xCA, 0x1C, 0x00}},
    {"aim camera stick read 2", 0x53C03E, 5, {0xE8, 0xED, 0xC9, 0x1C, 0x00}},
    {"aim camera stick read 3", 0x53C263, 5, {0xE8, 0xC8, 0xC7, 0x1C, 0x00}},
    {"aim camera stick read 4", 0x53C296, 5, {0xE8, 0x95, 0xC7, 0x1C, 0x00}},
};

// Input latency: the frame tick commits the input snapshot (0x708350) BEFORE polling (0x7099D0), so what is polled
// reaches the game one tick late. Swapping the two call targets polls first. ECX is the same object in both.
constexpr SiteDef kSiteInputCommit = {"input commit call", 0x401AB0, 5, {0xE8, 0x9B, 0x68, 0x30, 0x00}};
constexpr SiteDef kSiteInputPoll = {"input poll call", 0x401AF0, 5, {0xE8, 0xDB, 0x7E, 0x30, 0x00}};
constexpr uint8_t kInputCommitSwapped[5] = {0xE8, 0x1B, 0x7F, 0x30, 0x00};  // call 0x7099D0
constexpr uint8_t kInputPollSwapped[5] = {0xE8, 0x5B, 0x68, 0x30, 0x00};    // call 0x708350

// ---- World extras ---------------------------------------------------------------------------------------------
// Object activation radius: `fld dword [0x773EB4]` (1000000.0 = 1000^2) in the active-list builder; the operand
// is redirected to our own slot so that the 7 other users of the constant keep theirs.
constexpr SiteDef kSiteActivation = {"object activation radius", 0x6C6296, 12,
    {0xD9, 0x05, 0xB4, 0x3E, 0x77, 0x00, 0xD9, 0x9D, 0x20, 0xD9, 0xFF, 0xFF}};
constexpr uint32_t kActivationOperandVa = 0x6C6298;
// Mesh LOD metric at 006DD42A: fld [model+54h] / fmul qword [0x772A50] (25.0) / fdivr [distance] / fstp [model+20h], so
// metric = distance / (model+54h * factor): a larger factor keeps the detailed model farther away.
constexpr SiteDef kSiteLodFactor = {"LOD factor", 0x6DD42D, 6, {0xDC, 0x0D, 0x50, 0x2A, 0x77, 0x00}};
constexpr uint32_t kLodOperandVa = 0x6DD42F;
// High detail streaming ring: three `1` immediates that make the outer cells use the reduced resource set.
constexpr SiteDef kSiteHighDetail1 = {"high detail class (1)", 0x5E6D7F, 5, {0xB8, 0x01, 0x00, 0x00, 0x00}};
constexpr SiteDef kSiteHighDetail2 = {"high detail class (2)", 0x5E6DA0, 5, {0xB8, 0x01, 0x00, 0x00, 0x00}};
constexpr SiteDef kSiteHighDetail3 = {"high detail class (3)", 0x5EC37F, 10,
    {0xC7, 0x86, 0xB4, 0x87, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00}};
// Interior visibility-volume test (FUN_006DBE40, ret 10h) wrongly rejects visible props near walls and mirrors.
constexpr SiteDef kSiteVisibility1 = {"visibility volume test (call 1)", 0x6D396F, 5, {0xE8, 0xCC, 0x84, 0x00, 0x00}};
constexpr SiteDef kSiteVisibility2 = {"visibility volume test (call 2)", 0x6D3D34, 5, {0xE8, 0x07, 0x81, 0x00, 0x00}};
constexpr uint32_t kVisibilityTestFn = 0x6DBE40;
// Its prologue (push ebp / mov ebp,esp / sub esp,20h): world_extra.cpp checks it before it hooks the two calls.
constexpr SiteDef kSiteVisibilityTestEntry = {"visibility volume test entry", 0x6DBE40, 6, {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x20}};

// ---- Analog vehicle triggers ----------------------------------------------------------------------------------
// Three car functions reduce the controller's two shoulder inputs to 0/1 (`neg / sbb / neg / fild`): gas and brake
// become on/off. Each starts its controller branch with `cmp byte [USEJOY], 0`; that is where we step in.
constexpr SiteDef kSiteTrig1 = {"car triggers (FUN_0054BFC0)", 0x54C13B, 9, {0x80, 0x3D, 0xF0, 0x10, 0x48, 0x01, 0x00, 0x74, 0x59}};
constexpr uint32_t kTrig1Keyboard = 0x54C19D, kTrig1Pad = 0x54C144, kTrig1Merge = 0x54C1D1;
constexpr SiteDef kSiteTrig2 = {"car triggers (FUN_00555B50)", 0x555F2F, 7, {0x80, 0x3D, 0xF0, 0x10, 0x48, 0x01, 0x00}};
constexpr uint32_t kTrig2Resume = 0x555F36, kTrig2Merge = 0x556008;
constexpr SiteDef kSiteTrig3 = {"car triggers (FUN_00558820)", 0x559444, 9, {0x80, 0x3D, 0xF0, 0x10, 0x48, 0x01, 0x00, 0x74, 0x5B}};
constexpr uint32_t kTrig3Keyboard = 0x5594A8, kTrig3Pad = 0x55944D, kTrig3Merge = 0x55951D;

// ---- Crash guards (NPC navigation table; the crash class CrashFix addresses) -------------------------------------
// FUN_005E1ED0(this = nav table, out, index) reads node[index] with no bounds check; a bad index (an NPC sent to a
// "no waypoint" -1 after a cutscene) reads outside the table and crashes at 005E1F3D. Sibling accessors reject
// index > [this+8]. A second crash site dereferences a NULL node pointer at 0047F1CB.
constexpr SiteDef kSiteNavLookup = {"navigation node lookup", 0x5E1ED0, 6, {0x80, 0x79, 0x04, 0x00, 0x75, 0x05}};
constexpr uint32_t kNavLookupContinue = 0x5E1EDB;
constexpr SiteDef kSiteNavNullCheck = {"navigation node NULL check", 0x47F1CB, 5, {0x66, 0x83, 0x7F, 0x04, 0x00}};
constexpr uint32_t kNavNullContinue = 0x47F1D0;

// ---- Residents (read-only: nothing here is patched) --------------------------------------------------------------
// The residents on the pause map and the schedule viewer (modules/npc_map.cpp) only read the game's data and call none of its
// functions. These are the instructions and tables that prove the offsets and constants they rely on (NPC registry and kind,
// what the pause map asks of a resident, FUN_004C18D0 area of a resident, FUN_004C0010 block of a resident, FUN_004C75A0 and
// FUN_004C0930 choice of the timetable, FUN_004DBB20 entry for a time, the clock). The module stays off unless all are in place.
constexpr SiteDef kNpcFacts[] = {
    {"npc registry [main+114h+i*4]", 0x4AC042, 7, {0x8B, 0xB4, 0x83, 0x14, 0x01, 0x00, 0x00}},
    {"npc kind call [vtable+A4h]", 0x4AC057, 6, {0x8B, 0x90, 0xA4, 0x00, 0x00, 0x00}},
    {"resident kind: mov al,1", 0x4D4830, 3, {0xB0, 0x01, 0xC3}},
    {"resident vtable slot +A4h", 0x7756E0, 4, {0x30, 0x48, 0x4D, 0x00}},
    {"resident id [npc+D54h]", 0x4AC183, 6, {0x38, 0x98, 0x54, 0x0D, 0x00, 0x00}},
    {"map: resident byte +38h", 0x65B76E, 4, {0x80, 0x7F, 0x38, 0x00}},
    {"map: resident area test", 0x65B839, 6, {0x3B, 0x88, 0xE0, 0x4F, 0x83, 0x00}},
    {"map: resident position", 0x65B845, 9, {0x8B, 0x47, 0x5C, 0x8B, 0x57, 0x58, 0x8B, 0x4F, 0x60}},
    {"map: resident flags +1Ch", 0x65B852, 6, {0x8B, 0x47, 0x1C, 0xC1, 0xE8, 0x09}},
    {"npc area: entry +D34h", 0x4C18FD, 6, {0x8B, 0x86, 0x34, 0x0D, 0x00, 0x00}},
    {"npc area: mode +622h", 0x4C1926, 7, {0x0F, 0xBE, 0x86, 0x22, 0x06, 0x00, 0x00}},
    {"npc area: saved byte", 0x4C195C, 8, {0x0F, 0xBE, 0x84, 0xD0, 0x7B, 0x90, 0x09, 0x00}},
    {"npc area: entry +D38h", 0x4C1983, 6, {0x8B, 0x8E, 0x38, 0x0D, 0x00, 0x00}},
    {"npc area: cases -1..2", 0x4C19A8, 16, {0x71, 0x19, 0x4C, 0x00, 0x71, 0x19, 0x4C, 0x00, 0x95, 0x19, 0x4C, 0x00, 0x95, 0x19, 0x4C, 0x00}},
    {"npc area: cases 3..4", 0x4C19B8, 8, {0x95, 0x19, 0x4C, 0x00, 0x83, 0x19, 0x4C, 0x00}},
    {"timetables [main+D1Ch]", 0x4AB560, 14, {0x8B, 0x44, 0x24, 0x04, 0x8B, 0x84, 0x81, 0x1C, 0x0D, 0x00, 0x00, 0xC2, 0x04, 0x00}},
    {"NPC_LIST [main+D4Ch], A8h", 0x4AB570, 16, {0x0F, 0xBE, 0x44, 0x24, 0x04, 0x69, 0xC0, 0xA8, 0x00, 0x00, 0x00, 0x03, 0x81, 0x4C, 0x0D, 0x00}},
    {"timetables in effect: 800h", 0x4B1ADE, 7, {0x81, 0x4E, 0x04, 0x00, 0x08, 0x00, 0x00}},
    {"block number [list+33h]", 0x4C004C, 9, {0x8B, 0x8D, 0x30, 0x0D, 0x00, 0x00, 0x8A, 0x51, 0x33}},
    {"block switch: chapters 5..8", 0x4C0214, 16, {0x89, 0x00, 0x4C, 0x00, 0x89, 0x00, 0x4C, 0x00, 0xE8, 0x00, 0x4C, 0x00, 0xE8, 0x00, 0x4C, 0x00}},
    {"block switch: chapters 9..12", 0x4C0224, 16, {0xDD, 0x01, 0x4C, 0x00, 0x57, 0x01, 0x4C, 0x00, 0xE8, 0x00, 0x4C, 0x00, 0x89, 0x00, 0x4C, 0x00}},
    {"block switch: byte CED66h", 0x4C00CB, 7, {0x80, 0xB8, 0x66, 0xED, 0x0C, 0x00, 0x00}},
    {"block switch: byte 8CA11h", 0x4C0185, 7, {0x80, 0xB8, 0x11, 0xCA, 0x08, 0x00, 0x00}},
    {"block search: 54h records", 0x4DB454, 15, {0x7C, 0x15, 0x3A, 0xC1, 0x74, 0x0B, 0x84, 0xD2, 0x75, 0x02, 0xFE, 0xC0, 0x83, 0xC6, 0x54}},
    {"block set: place not empty", 0x4DB497, 16, {0x83, 0xC0, 0x04, 0x8D, 0x50, 0x01, 0x8D, 0x49, 0x00, 0x8A, 0x08, 0x40, 0x84, 0xC9, 0x75, 0xF9}},
    {"forced timetable +9907Eh", 0x4C75B6, 8, {0x80, 0xBC, 0xD0, 0x7E, 0x90, 0x09, 0x00, 0x04}},
    {"day variant [game+8C60Ah]", 0x44F7F0, 7, {0x8A, 0x81, 0x0A, 0xC6, 0x08, 0x00, 0xC3}},
    {"variant table: 4 6 8 10", 0x4C09BC, 16, {0x62, 0x09, 0x4C, 0x00, 0x69, 0x09, 0x4C, 0x00, 0x70, 0x09, 0x4C, 0x00, 0x77, 0x09, 0x4C, 0x00}},
    {"chapter [game+8C609h]", 0x44F7D0, 7, {0x8A, 0x81, 0x09, 0xC6, 0x08, 0x00, 0xC3}},
    {"rain flag [weather+1270h]", 0x470E70, 12, {0x33, 0xC0, 0x38, 0x81, 0x70, 0x12, 0x00, 0x00, 0x0F, 0x94, 0xC0, 0xC3}},
    {"dry day: block +D80h -> 2", 0x4C098E, 15, {0x8B, 0x86, 0x80, 0x0D, 0x00, 0x00, 0xF7, 0xD8, 0x1A, 0xC0, 0x5E, 0x24, 0x02, 0x5B, 0xC3}},
    {"rain: block +D8Ch -> 3", 0x4C099D, 14, {0x83, 0xBE, 0x8C, 0x0D, 0x00, 0x00, 0x00, 0x74, 0x05, 0x5E, 0xB0, 0x03, 0x5B, 0xC3}},
    {"rain: block +D74h -> 1", 0x4C09AB, 13, {0x83, 0xBE, 0x74, 0x0D, 0x00, 0x00, 0x00, 0xB0, 0x01, 0x75, 0x02, 0x32, 0xC0}},
    {"entry lookup: hour, minute", 0x4DBB86, 8, {0x0F, 0xBE, 0x43, 0xAE, 0x0F, 0xBE, 0x53, 0xAF}},
    {"entry lookup: wrap", 0x4DBBA3, 16, {0x80, 0x3B, 0x00, 0x8B, 0xC3, 0x75, 0x09, 0x0F, 0xBE, 0x4B, 0x01, 0x6B, 0xC9, 0x54, 0x2B, 0xC1}},
    {"time range test", 0x470450, 16, {0x8B, 0x44, 0x24, 0x08, 0x8B, 0x4C, 0x24, 0x0C, 0x3B, 0xC1, 0x7D, 0x11, 0x8B, 0x54, 0x24, 0x04}},
    {"clock: hour, minute", 0x470300, 16, {0x8B, 0x41, 0x0C, 0x8B, 0xD0, 0xC1, 0xE2, 0x04, 0x2B, 0xD0, 0x8B, 0x41, 0x10, 0x8D, 0x04, 0x90}},
    {"clock: second", 0x470310, 14, {0x8B, 0xD0, 0xC1, 0xE2, 0x04, 0x2B, 0xD0, 0x8B, 0x41, 0x14, 0x8D, 0x04, 0x90, 0xC3}},
};
constexpr int kNpcFactCount = (int)(sizeof(kNpcFacts) / sizeof(kNpcFacts[0]));
static_assert(kNpcFactCount == 38, "a new fact must also be listed in kAllSites, so that the host test compares it with DP.exe");

// ---- Map (ideas and addresses from MapFix by Behelit09, MIT; implementation is ours) ----------------------------
// The in-game map shows a 3x3 window of 64 overworld tiles, floors the zoom at 1.0 and drops tiles it no longer needs.
constexpr SiteDef kMapZoomFloor = {"map zoom floor skip", 0x65919F, 2, {0x75, 0x0D}};
constexpr SiteDef kMapZoomClampCall = {"map zoom clamp call", 0x6591F0, 5, {0xE8, 0x2B, 0x74, 0x0A, 0x00}};
constexpr SiteDef kMapZoomInit = {"map initial zoom store", 0x661860, 6, {0xD9, 0x9E, 0xE0, 0x8E, 0x00, 0x00}};
// Entry of the game's text draw (thiscall, CMessage): sub esp,58h / push esi / mov esi,ecx. The waypoint label is drawn from here
// at most once per frame, on frames where the game draws text (not in cutscenes), in the game's own font and HUD pass, the way
// the game draws its "%d Yards" label (FUN_0043C200).
constexpr SiteDef kSiteTextDrawEntry = {"text draw entry", 0x45B820, 6, {0x83, 0xEC, 0x58, 0x56, 0x8B, 0xF1}};
// Entry of the game's 2D sprite draw FUN_00459520 (thiscall, 3 stack args): sub esp,70h / push esi / mov esi,[esp+80h]. The HUD (health bars,
// minimap frame) draws sprites on every gameplay frame, so the waypoint label is also drawn from here: it no longer depends on the game
// drawing some text in that frame.
constexpr SiteDef kSiteSpriteDrawEntry = {"sprite draw entry", 0x459520, 11, {0x83, 0xEC, 0x70, 0x56, 0x8B, 0xB4, 0x24, 0x80, 0x00, 0x00, 0x00}};
// The call of the pause map's draw function (ECX = the screen manager object, the map centre and zoom are in it).
constexpr SiteDef kSiteMapDrawCall = {"map screen draw call", 0x6541FC, 5, {0xE8, 0x3F, 0x59, 0x00, 0x00}};
// The map points (stage-object manager 0143C078h): count at +2E838h, 4-byte entries at +2E83Ch (u16 id, s8 type, s8 floor). The
// minimap and the pause map read a point's position through the accessor FUN_005E71B0 (thiscall: float* out, index; returns the
// type in AL). Its first 10 bytes are  mov eax,[esp+8] / cmp eax,[ecx+2E838h], then 005E71BA: push esi / jge. FUN_005EFF20 rebuilds
// the list (count = 0, then appends), called from three places.
constexpr SiteDef kSiteSpotAccessor = {"map point accessor", 0x5E71B0, 10, {0x8B, 0x44, 0x24, 0x08, 0x3B, 0x81, 0x38, 0xE8, 0x02, 0x00}};
constexpr SiteDef kSiteSpotRebuild[3] = {
    {"map point rebuild call (refresh)", 0x5D3677, 5, {0xE8, 0xA4, 0xC8, 0x01, 0x00}},
    {"map point rebuild call (stage load)", 0x5D58CB, 5, {0xE8, 0x50, 0xA6, 0x01, 0x00}},
    {"map point rebuild call (pause map)", 0x652B75, 5, {0xE8, 0xA6, 0xD3, 0xF9, 0xFF}},
};
// The game's objective beacon (the red arrows of GOAL.XPC with "%d Yards" under them). FUN_005B1120 draws one icon (thiscall:
// ECX = the 38h-byte icon, one float argument = the size, ret 4 at 005B155A), FUN_005B0DD0 moves it and runs its bobbing (thiscall,
// one argument = a pointer to 3 floats, ret 4 at 005B0F88). The 12h (2D draw) branch of the stage manager's handler FUN_005D26B0 is
// only  mov ecx,014375F4h / call 005EE3F0 (the four fixed target icons; takes ECX alone, plain ret at 005EE45D) / pop edi / pop ebx /
// pop esi: the waypoint's beacon is drawn from that call, in the pass and the render state the game draws its own icons in.
constexpr SiteDef kSiteBeaconCall = {"target icons draw (mov ecx + call)", 0x5D4A6F, 10, {0xB9, 0xF4, 0x75, 0x43, 0x01, 0xE8, 0x77, 0x99, 0x01, 0x00}};
constexpr SiteDef kSiteBeaconDraw = {"objective icon draw entry", 0x5B1120, 16,
    {0x83, 0xEC, 0x48, 0x56, 0x8B, 0xF1, 0x83, 0x7E, 0x04, 0x00, 0x0F, 0x84, 0x26, 0x04, 0x00, 0x00}};
constexpr SiteDef kSiteBeaconMove = {"objective icon move entry", 0x5B0DD0, 16,
    {0x83, 0xEC, 0x14, 0x8B, 0x44, 0x24, 0x18, 0xD9, 0x40, 0x04, 0x56, 0xD9, 0x5C, 0x24, 0x1C, 0x8B}};
constexpr uint32_t kTargetIconsDrawFn = 0x5EE3F0, kIconDrawFn = 0x5B1120, kIconMoveFn = 0x5B0DD0;
// Story page of the menu (read-only, see story.h): the places in the game's own code that fix every offset the page reads. Nothing
// here is patched or called; the page stays empty when one of them differs.
constexpr SiteDef kStorySites[11] = {
    {"story: chapter store", 0x4526D7, 6, {0x88, 0x86, 0x09, 0xC6, 0x08, 0x00}},              // mov [esi+8C609h],al (FUN_004526D0)
    {"story: episode store", 0x4526E8, 6, {0x88, 0x86, 0x08, 0xC6, 0x08, 0x00}},              // mov [esi+8C608h],al
    {"story: sub-step store", 0x44F7E4, 6, {0x88, 0x81, 0x0A, 0xC6, 0x08, 0x00}},             // mov [ecx+8C60Ah],al (FUN_0044F7E0)
    {"story: side quest group", 0x43D2B5, 5, {0x83, 0x7C, 0x24, 0x14, 0x28}},                 // cmp group,28h (FUN_0043D270)
    {"story: side quest count", 0x43D2C0, 9, {0x8B, 0x4C, 0x24, 0x10, 0x83, 0xF9, 0x32, 0x7D, 0x2E}},  // local < 50 = taken, else done
    {"story: finished flag", 0x43D3D7, 12, {0x83, 0x7C, 0x24, 0x14, 0x44, 0x75, 0x14, 0x83, 0x7C, 0x24, 0x10, 0x3C}},  // group 44h, flag 3Ch
    {"story: destination group", 0x452281, 9, {0x6A, 0x48, 0x8B, 0xC8, 0xE8, 0x26, 0xBE, 0x2B, 0x00}},  // push 48h / call 0070E0B0 (FUN_00452270)
    {"story: destination window", 0x4522B7, 7, {0x8D, 0x84, 0xB7, 0x68, 0xD3, 0x0C, 0x00}},   // lea eax,[edi+esi*4+CD368h]
    {"story: destination table", 0x5E6E16, 7, {0x80, 0xB9, 0x60, 0xF5, 0x8B, 0x00, 0x01}},    // cmp byte [ecx+8BF560h],1 (FUN_005E6E10)
    {"story: flag array pointer", 0x70FEAA, 3, {0x8B, 0x51, 0x3C}},                           // mov edx,[ecx+3Ch] (FUN_0070FE90)
    {"story: flag group table", 0x70DFF0, 6, {0x8B, 0x91, 0xB4, 0x12, 0x00, 0x00}},           // mov edx,[ecx+12B4h] (FUN_0070DFE0)
};
constexpr SiteDef kMapPanX = {"map pan limit X", 0x659534, 6, {0xD9, 0x44, 0x24, 0x3C, 0xD9, 0xC0}};
constexpr SiteDef kMapPanZ = {"map pan limit Z", 0x659574, 6, {0xD9, 0x44, 0x24, 0x38, 0xD9, 0xC0}};
constexpr SiteDef kMapTileLookup = {"map tile lookup (first 16 of 20 bytes)", 0x65A2ED, 16,
    {0x0F, 0xB7, 0x84, 0x54, 0x68, 0x07, 0x00, 0x00, 0x66, 0x85, 0xC0, 0x0F, 0x8C, 0x39, 0x01, 0x00}};
constexpr uint8_t kMapTileLookupTail[4] = {0x00, 0x0F, 0xBF, 0xD8};  // bytes 16..19 of the lookup
constexpr SiteDef kMapTileLimit = {"map tile count", 0x65A43C, 3, {0x83, 0xF8, 0x09}};
constexpr SiteDef kMapDeleteResource = {"resource delete entry", 0x6B3AA0, 6, {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C}};
constexpr SiteDef kMapReleaseResource = {"resource release entry", 0x6B2830, 6, {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08}};
constexpr SiteDef kMapFindResource = {"map tile lookup by name", 0x65A32A, 5, {0xE8, 0x01, 0x89, 0x05, 0x00}};
constexpr SiteDef kMapHeading[7] = {
    {"map heading 1", 0x659499, 8, {0xE8, 0x32, 0xCB, 0x06, 0x00, 0xD9, 0x40, 0x7C}},
    {"map heading 2", 0x6594D6, 8, {0xE8, 0xF5, 0xCA, 0x06, 0x00, 0xD9, 0x40, 0x7C}},
    {"map heading 3", 0x65A246, 8, {0xE8, 0x85, 0xBD, 0x06, 0x00, 0xD9, 0x40, 0x7C}},
    {"map heading 4", 0x65A4E9, 8, {0xE8, 0xE2, 0xBA, 0x06, 0x00, 0xD9, 0x40, 0x7C}},
    {"map heading 5", 0x65BA14, 8, {0xE8, 0xB7, 0xA5, 0x06, 0x00, 0xD9, 0x40, 0x7C}},
    {"map heading 6", 0x65C1EE, 8, {0xE8, 0xDD, 0x9D, 0x06, 0x00, 0xD9, 0x40, 0x7C}},
    {"map heading 7", 0x65D6B3, 8, {0xE8, 0x18, 0x89, 0x06, 0x00, 0xD9, 0x40, 0x7C}},
};
constexpr SiteDef kMapArrow = {"map player arrow angle", 0x65C27B, 7, {0xD9, 0xEE, 0x8B, 0xD8, 0xD9, 0x5B, 0x48}};
constexpr uint32_t kMapTileFirstId = 0x937, kMapTileLastId = 0x976;
constexpr uint32_t kLoadResourceFn = 0x40B4C0;  // int cdecl(id, flags, mode)
constexpr uint32_t kFindResourceFn = 0x6B2C30;  // void* thiscall(mgr, name, 0), ret 8
constexpr uint32_t kPlayerLookupFn = 0x6C5FD0;  // object* (handle), ret 4; the player's angle is at +7Ch
constexpr uint32_t kPlayerHandleVa = 0x8A9BA4;

// The game's mouse wheel reader (push ebp; mov ebp,esp; mov ax,[14AFFC0h]; pop ebp; ret): the weapon switch reads it too.
constexpr SiteDef kSiteWheelRead = {"mouse wheel reader", 0x700610, 11, {0x55, 0x8B, 0xEC, 0x66, 0xA1, 0xC0, 0xFF, 0x4A, 0x01, 0x5D, 0xC3}};
// Free camera: the main tick calls FUN_006B62E0 (builds the view matrices of the render camera at BD9648h, eye +4,
// target +14h, up +24h, field of view +54h as a float) right after loading ECX with BD9648h.
constexpr SiteDef kSiteCameraCall = {"render camera build call", 0x401BD1, 5, {0xE8, 0x0A, 0x47, 0x2B, 0x00}};
constexpr uint32_t kCameraBuildFn = 0x6B62E0, kRenderCameraVa = 0xBD9648, kHudFlagVa = 0x147D88C;
// The one DrawPrimitive of the final composite (FUN_006E9F60): mov ecx,[edx+144h] / call ecx (stdcall, 4 arguments).
constexpr SiteDef kSiteCompositeDraw = {"final composite draw", 0x6EA39C, 8, {0x8B, 0x8A, 0x44, 0x01, 0x00, 0x00, 0xFF, 0xD1}};
// FUN_006EA470(table, width, height, taps, sigma) (cdecl) builds the gaussian blur offsets `i / size` in UV units; the six
// calls pass the width or the height of the buffer being blurred (the other one is 0). With bigger buffers the blur
// shrinks on the screen in proportion; handing in a smaller size keeps its screen size.
constexpr uint32_t kGaussTableFn = 0x6EA470;
constexpr SiteDef kGaussCalls[6] = {
    {"gauss table call 1", 0x6E99D2, 5, {0xE8, 0x99, 0x0A, 0x00, 0x00}}, {"gauss table call 2", 0x6E9A00, 5, {0xE8, 0x6B, 0x0A, 0x00, 0x00}},
    {"gauss table call 3", 0x6E9C15, 5, {0xE8, 0x56, 0x08, 0x00, 0x00}}, {"gauss table call 4", 0x6E9C40, 5, {0xE8, 0x2B, 0x08, 0x00, 0x00}},
    {"gauss table call 5", 0x6EA6C3, 5, {0xE8, 0xA8, 0xFD, 0xFF, 0xFF}}, {"gauss table call 6", 0x6EA6ED, 5, {0xE8, 0x7E, 0xFD, 0xFF, 0xFF}},
};
// Byte-string text (credits, "%s" formats): FUN_0045B820 reads each character with movsx and compares with 0A9h as a
// signed value, so every byte of 80h or more (all of Cyrillic in a single-byte code page) lands above the font atlas.
// movzx plus a byte compare makes them index the atlas rows 1 and 2 like any other character.
constexpr SiteDef kSiteByteText = {"byte text sign extension", 0x45BB0D, 7, {0x0F, 0xBE, 0x04, 0x3E, 0x83, 0xF8, 0xA9}};
constexpr uint8_t kByteTextFixed[7] = {0x0F, 0xB6, 0x04, 0x3E, 0x3C, 0xA9, 0x90};
// Translator tools (modules/text_overlay.cpp).
// The text module's setup FUN_0045F610 asks the resource manager for the data of MES_ALL.MES (id 2400h): push 0 / push 2400h /
// call FUN_006B2BE0 (thiscall, ECX = CRdData, ret 8), and stores the result in CMessage+44h right after (mov [esi+44h],eax). All 18
// reads of the text go through that pointer, so this one call is where the text overlay hands over its merged copy.
constexpr SiteDef kSiteMesData = {"MES data call", 0x45F679, 5, {0xE8, 0x62, 0x35, 0x25, 0x00}};
constexpr SiteDef kSiteMesDataStore = {"MES data pointer store", 0x45F67E, 3, {0x89, 0x46, 0x44}};
// Entry of the text drawer FUN_0045C9F0 (thiscall, ten stack arguments, ret 28h): sub esp,0CCh / push ebx / push esi, then
// mov esi,[esp+0E8h] (the message id, the fifth argument). Wrapped for the message id label and so that the glyph log knows
// which message is being drawn.
constexpr SiteDef kSiteMesDrawEntry = {"text drawer entry", 0x45C9F0, 8, {0x81, 0xEC, 0xCC, 0x00, 0x00, 0x00, 0x53, 0x56}};
constexpr SiteDef kSiteMesDrawNext = {"text drawer id load", 0x45C9F8, 7, {0x8B, 0xB4, 0x24, 0xE8, 0x00, 0x00, 0x00}};
// Entry of FUN_0045A760, which every glyph quad goes through (thiscall, three pointers, ret 0Ch): mov eax,[ecx+12070h] /
// cmp eax,300h; the jge that follows reads the flags of that compare (a full batch drops the quad).
constexpr SiteDef kSiteGlyphPushEntry = {"glyph quad entry", 0x45A760, 11, {0x8B, 0x81, 0x70, 0x20, 0x01, 0x00, 0x3D, 0x00, 0x03, 0x00, 0x00}};
constexpr SiteDef kSiteGlyphPushNext = {"glyph quad full-batch jump", 0x45A76B, 6, {0x0F, 0x8D, 0x8E, 0x00, 0x00, 0x00}};
// WinMain: any command-line argument (the stock launcher passes one when its "Full Screen mode" box is ticked) sets the
// fullscreen flag at 0148BBC0h to 1. The exact argument "-full1280" (FUN_0040ECB0 is a strcmp, 0 = equal) takes the
// branch at 007006CE, which also sets the 1280x1024 flag at 0148BBC4h to 1; any other argument takes 007006E4 and sets
// it to 0. Our borderless window replaces real fullscreen, so with it on the three stores of 1 keep 0 (the immediate is
// the byte at +6).
constexpr SiteDef kSiteFullscreenArg[3] = {
    {"fullscreen flag store 1", 0x7006CE, 10, {0xC7, 0x05, 0xC0, 0xBB, 0x48, 0x01, 0x01, 0x00, 0x00, 0x00}},
    {"fullscreen flag store 2", 0x7006E4, 10, {0xC7, 0x05, 0xC0, 0xBB, 0x48, 0x01, 0x01, 0x00, 0x00, 0x00}},
    {"1280x1024 flag store", 0x7006D8, 10, {0xC7, 0x05, 0xC4, 0xBB, 0x48, 0x01, 0x01, 0x00, 0x00, 0x00}},
};
// Combat strafe (the console had it, the PC code lost it): at the tail of the player update (esi = player) a shoulder
// button edge asks the player state machine for state 9 (strafe left) or 0Ah (right) while the lock-on flag
// (player+638h bit 10h) and the capability 2000h are set. FUN_004FEE60 (stdcall, 1 argument) answers capability
// questions, FUN_00528F40 (thiscall, 1 argument) changes the player state.
constexpr SiteDef kSiteStrafeTail = {"player update tail", 0x504497, 7, {0x83, 0xBE, 0x54, 0x06, 0x00, 0x00, 0x00}};
constexpr uint32_t kStrafeReturn = 0x50449E, kCapabilityFn = 0x4FEE60, kStateChangeFn = 0x528F40;
// Quick turn (player state 0Bh: FUN_00524BF0 turns the heading +7Ch to the target +6Ch and goes back to the state kept in
// +0A11h). FUN_00503C60 (thiscall, `mov esi,ecx` at 00503C64: esi = the player for the whole function) starts it from two
// branches. The idle branch (state 0) at 00503FA7 asks for flag 4 of the game manager (low word, manager+8C5C8h), the
// capability 100h and a press of the raw button 100h (L3), then writes heading + pi into +6Ch and calls FUN_00528F40(0Bh)
// at 00504040. The console asked for the logical X there and set flag 4 in the idle code from the stick pulled back; on PC
// the only place that sets the flag is the weapon branch (0050439F), and in the idle handler the flag means "walk
// backwards" (00523176). The weapon branch (states 14h, 16h, 18h, 24h, 26h) at 005042CD is left alone. The gate replaces
// the flag test of the idle branch: its 11 bytes are `push 0 / push 4 / mov ecx,esi / call 4FD860` (the flag test: ret 8,
// answer in edx:eax, ecx not read). The exit is the epilogue behind the game's own quick-turn call (pop edi / pop esi /
// add esp,8 / ret), which undoes the frame of the entry (sub esp,8 / push esi / ... / push edi).
constexpr SiteDef kSiteQuickTurnGate = {"idle quick-turn flag test", 0x503FA7, 11,
    {0x6A, 0x00, 0x6A, 0x04, 0x8B, 0xCE, 0xE8, 0xAE, 0x98, 0xFF, 0xFF}};
constexpr uint32_t kQuickTurnResume = 0x503FB2, kQuickTurnExit = 0x504045, kFlagTestFn = 0x4FD860;
// What the gate and its worker rely on without patching it: the frame of FUN_00503C60, the epilogue, the handler slot of
// state 0Bh in the table at 008A9758h, and the entries of the three input getters the worker calls:
// FUN_004547B0 (thiscall, ecx = game manager, one argument = logical mask, ret 4: 1 on the rising edge of the button the
// game's control options give to that action), FUN_00708910 (thiscall, ecx = CInput, pad, kind 0 held / 1 pressed, mask,
// ret 0Ch) and FUN_00708A30 (kStickGetterFn; thiscall, ecx = CInput, pad, stick, axis, ret 0Ch, float in st0, Y up = plus).
constexpr SiteDef kQuickTurnChecks[] = {
    {"player transitions entry", 0x503C60, 13, {0x83, 0xEC, 0x08, 0x56, 0x8B, 0xF1, 0x8B, 0x86, 0x54, 0x06, 0x00, 0x00, 0x57}},
    {"quick-turn exit epilogue", 0x504045, 6, {0x5F, 0x5E, 0x83, 0xC4, 0x08, 0xC3}},
    {"state 0Bh handler slot", 0x8A9784, 4, {0xF0, 0x4B, 0x52, 0x00}},
    {"logical press getter", 0x4547B0, 15, {0x56, 0x57, 0x8B, 0xF1, 0xE8, 0x77, 0xD3, 0x28, 0x00, 0x8B, 0x3D, 0x10, 0x9E, 0xBD, 0x00}},
    {"button mask getter", 0x708910, 16, {0x55, 0x8B, 0xEC, 0x51, 0x89, 0x4D, 0xFC, 0x0F, 0xB6, 0x45, 0x08, 0x69, 0xC0, 0xCC, 0x00, 0x00}},
    {"stick getter", 0x708A30, 16, {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x89, 0x4D, 0xF4, 0xC6, 0x45, 0xF8, 0x00, 0xC6, 0x45, 0xF9}},
};
constexpr uint32_t kLogicalPressFn = 0x4547B0, kInputMaskFn = 0x708910;
constexpr uint32_t kInputPtrVa = 0xBD9E10;   // CInput*
constexpr uint32_t kPadIndexVa = 0xBD77B4;   // byte: the pad slot gameplay reads (always 0)
constexpr uint32_t kCameraPtrVa = 0xBE1EA4;  // CCamera*: yaw +70h, mode +154h (2 = the aiming camera)
// Costume change. FUN_0044E730(suit, variant) (thiscall with ecx = game manager, two stack arguments, ret 8) is the
// engine's own "York changes clothes" path used by the cutscene scripts. The manager keeps the current suit digit at
// +8C5E4h, the variant 1..10 at +8C5E5h and the kind of the playable character at +8CA12h / +8CA13h (0 = York). While the
// player is busy (player+1Ch bit 8) a request goes into +8C5E6h / +8C5E7h and bit 2000000h of player+1Ch asks the engine
// to apply it later.
constexpr SiteDef kSiteSuitChange = {"costume change function", 0x44E730, 16, {0x81, 0xEC, 0x80, 0x03, 0x00, 0x00, 0xA1, 0xCC, 0x5E, 0xBD, 0x00, 0x33, 0xC4, 0x89, 0x84, 0x24}};
constexpr uint32_t kSuitChangeFn = 0x44E730;
// Player state machine entry FUN_00528F40 (thiscall, ecx = player, one stack argument = the new state): the first 7
// instructions-worth of bytes are position independent (push ebx / push ebp / mov ebp,[esp+0Ch] / push esi), so an
// entry hook can run a worker first and then replay them. Helpers of the "set a state and show its motion" idiom:
// FUN_00520860 (cdecl, no arguments: picks the default motion for the state) and FUN_004FEF40 (fastcall, ecx = player:
// applies the requested motion).
constexpr SiteDef kSiteStateEntry = {"player state change entry", 0x528F40, 7, {0x53, 0x55, 0x8B, 0x6C, 0x24, 0x0C, 0x56}};
constexpr uint32_t kStateEntryReturn = 0x528F47, kDefaultMotionFn = 0x520860, kApplyMotionFn = 0x4FEF40;
constexpr uint32_t kGameplayModeVa = 0x1393700, kActionGateVa = 0x13936F8, kEventModeVa = 0x8A6074;
// Alternate 3D representation of far objects (the "pop-in in front of me"). FUN_005F19B0 is the residency state machine of
// the streaming manager (thiscall, two stack arguments, ret 8); its three callers load ECX with `mov ecx,esi`
// and call it with a 5-byte `call`. After its sweep, objects whose cell is outside the 4x4 near block (manager+2801Ch,
// 16 cell numbers of the 64x64 grid, cell = trunc((coordinate + 40000) * 0.0008)) are shown by a low detail package. A
// hook after the call can promote objects of a wider ring back to the full model through FUN_005F1790(manager, index,
// target) (thiscall, ret 8): target 0 = full.
constexpr SiteDef kAltPhaseCalls[3] = {
    {"residency call 1", 0x5F23AD, 5, {0xE8, 0xFE, 0xF5, 0xFF, 0xFF}},
    {"residency call 2", 0x5F2413, 5, {0xE8, 0x98, 0xF5, 0xFF, 0xFF}},
    {"residency call 3", 0x5F24DC, 5, {0xE8, 0xCF, 0xF4, 0xFF, 0xFF}},
};
constexpr SiteDef kSiteResidencyEntry = {"residency state machine", 0x5F19B0, 12, {0xB8, 0x8C, 0x12, 0x00, 0x00, 0xE8, 0x36, 0xF6, 0x15, 0x00, 0x53, 0x55}};
constexpr SiteDef kSiteSetTargetEntry = {"residency set target", 0x5F1790, 12, {0x8B, 0x54, 0x24, 0x04, 0x56, 0x8B, 0x34, 0x91, 0x85, 0xF6, 0x0F, 0x84}};
constexpr SiteDef kSiteResourceOfSlot = {"slot resource lookup", 0x5E2240, 8, {0x80, 0x79, 0x04, 0x00, 0x75, 0x06, 0x83, 0xC8}};
constexpr uint32_t kResidencyFn = 0x5F19B0, kSetTargetFn = 0x5F1790, kSlotResourceFn = 0x5E2240;
// Physics step. The main thread steps the PhysX scene from the object update phase: FUN_006C5FF0 calls FUN_006EB3C0
// ("Simulate", THISCALL: ECX = the physics object, one float argument on the stack, ret 4; its only caller) at 006C6CA7. When the call is skipped no actor moves and the
// scene is not fetched, as the game's own pause does. NxScene::simulate(0) is not a pause: the car kept rolling.
constexpr SiteDef kSitePhysicsStepCall = {"physics step call", 0x6C6CA7, 5, {0xE8, 0x14, 0x47, 0x02, 0x00}};
constexpr uint32_t kPhysicsStepFn = 0x6EB3C0;
// Whole physics steps at a high frame rate (HighFpsPhysics, stepplan.h): a frame shorter than one step of 1/60 s still gets a whole
// PhysX step from the game, so above 120 frames/s a car is frames/60 times too fast. The plan of a frame is made where the tick
// (FUN_00401A70) calls the world update FUN_006C5FF0 (THISCALL: ECX = the world, one float argument = the frame delta, ret 4 at
// 006C706A; these two calls are its only callers, and the code after them uses no value left in EAX/ECX/EDX: the `push ecx` at
// 00401B75 only makes room for a float). One call of it runs every phase in a row: message 1 (phase 2), message 5 (phase 0Ah),
// the physics step (phase 0Eh, the call above), message 6 (phase 8).
constexpr SiteDef kSiteWorldUpdateCalls[2] = {
    {"world update call (event mode)", 0x401B6E, 5, {0xE8, 0x7D, 0x44, 0x2C, 0x00}},
    {"world update call", 0x401B9F, 5, {0xE8, 0x4C, 0x44, 0x2C, 0x00}},
};
constexpr uint32_t kWorldUpdateFn = 0x6C5FF0;
// A warm-up of the smaller scenes (20 steps of 0.05 s in place of the normal step, FUN_0040B850) is asked for through a byte: set
// at 006C6C63 when `FUN_00401300() || event mode` changes (and at 00528FEA and 007262E0), passed to the two calls right after
// the physics step (`movzx ecx, byte [01481340h]` at 006C6CAC, the same at 006C6CCC) and cleared at 006C6CE7 in the same frame,
// whether or not a scene was stepped. In a frame without a step the request would be lost, so it is handed to the next frame
// that has one.
constexpr SiteDef kSiteWarmUpFlagRead = {"physics warm-up flag read", 0x6C6CAC, 7, {0x0F, 0xB6, 0x0D, 0x40, 0x13, 0x48, 0x01}};
constexpr uint32_t kPhysicsWarmUpFlagVa = 0x1481340;
// The car's control, message 5 of its handler FUN_00544AB0: `mov ecx,esi / call FUN_0054AAE0 / jmp` at 00546B51. FUN_0054AAE0
// (THISCALL: ECX = the car, no stack argument, plain ret at 0054AB13; this is its only caller) jumps to FUN_00555B50 (the player
// drives), FUN_0055AC30 or FUN_00550F00, which all end in a plain ret. They set the wheel torques as `torque * dt`, push the car
// down, burn at least 0.1 fuel per call and count ticks with (int)dt: all of it once per call, before the physics step.
constexpr SiteDef kSiteCarDriveCall = {"car control call", 0x546B53, 5, {0xE8, 0x88, 0x3F, 0x00, 0x00}};
constexpr uint32_t kCarDriveFn = 0x54AAE0;
// The car after the physics step, message 6: `mov ecx,esi / call FUN_005502C0` at 0054721E. FUN_005502C0 (THISCALL: ECX = the
// car, no stack argument, plain ret; this is its only caller) ends in FUN_005577D0, FUN_0055ED50 or FUN_00554070, which copy the
// position from the PhysX actor into car+58h (4 floats) and store `position - previous position` in car+127Ch (4 floats: 005579FE,
// 0055EF73, 00554299). That movement is read in the next frame by the odometer and the fuel (00555C60), the sleep check (00557065),
// the hit effects (0054FA4C) and by FUN_0054A700, the player states' test of whether the car moves (against 2.0).
constexpr SiteDef kSiteCarAfterStepCall = {"car after-step call", 0x547220, 5, {0xE8, 0x9B, 0x90, 0x00, 0x00}};
constexpr uint32_t kCarAfterStepFn = 0x5502C0;
constexpr uint32_t kCarPositionOffset = 0x58, kCarMovementOffset = 0x127C;
// Tick counters of the car outside its control function: `fld dword [014AFFE0h] / call _ftol2 (0074ED60)`, i.e. (int)dt, which is
// 0 in every short frame, so they stand still above 120 frames/s. The fld (6 bytes) becomes a call of a stub that loads the steps
// of the frame's plan, or the frame delta itself when the frame has no plan (BuildCarTicksStub). The 11 bytes are the fld and the
// call; _ftol2 begins with a compare of its own (`cmp dword [014B26F8h],0`), so it takes nothing from the flags. These ten and the
// four inside the control functions (00556638, 005571A1, 005571D8, 00551380, which see the delta CarDriveHook sets) are every
// `fld [frameDelta] / call _ftol2` pair in the car code 00541000..00560000.
constexpr uint32_t kFtolFn = 0x74ED60;
constexpr SiteDef kSiteCarTicks[10] = {
    {"car ticks: car+1314h down (FUN_00558820)", 0x558F11, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x44, 0x5E, 0x1F, 0x00}},
    {"car ticks: car+1314h up (FUN_00558820)", 0x559067, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xEE, 0x5C, 0x1F, 0x00}},
    {"car ticks: car+12F4h, limit 300 (FUN_00558820)", 0x559078, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xDD, 0x5C, 0x1F, 0x00}},
    {"car ticks: car+131Ch after a wreck (FUN_00558820)", 0x5598D6, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x7F, 0x54, 0x1F, 0x00}},
    {"car ticks: car+1208h, limit 50 (FUN_005577D0)", 0x557F9D, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xB8, 0x6D, 0x1F, 0x00}},
    {"car ticks: car+1230h, limit 30 (FUN_005577D0)", 0x557FCD, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x88, 0x6D, 0x1F, 0x00}},
    {"car ticks: car+718h (FUN_00551570)", 0x552B28, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x2D, 0xC2, 0x1F, 0x00}},
    {"car ticks: car+11D0h (FUN_0055B230)", 0x55B400, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x55, 0x39, 0x1F, 0x00}},
    {"car ticks: car+1318h (FUN_0055B230)", 0x55C61D, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x38, 0x27, 0x1F, 0x00}},
    {"car ticks: car+718h (FUN_0055B230)", 0x55C88F, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xC6, 0x24, 0x1F, 0x00}},
};
// The other `fld dword [frameDelta] / call _ftol2` pairs of the game (92 in all: 10 are the car's above, 4 more are in the car
// control FUN_00555B50 and FUN_00550F00, which run with a delta of exactly 1.0 under the plan and are left alone). Each takes the
// whole frames of a delta, so above 120 frames/s, with frames shorter than half a step, it gives 0 and the counter it feeds stands
// still (timers of NPCs, enemies, effects, doors, menus). Under the whole-step plan they get the plan's ticks like the car's
// counters; outside a plan the stub loads the frame delta itself, so nothing changes at 60 frames/s and below.
constexpr SiteDef kSiteGameTicks[78] = {
    {"game ticks (FUN_00468800)", 0x4688D8, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x7D, 0x64, 0x2E, 0x00}},
    {"game ticks (FUN_0053d960)", 0x53E5F1, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x64, 0x07, 0x21, 0x00}},
    {"game ticks (FUN_0053f740)", 0x53F761, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xF4, 0xF5, 0x20, 0x00}},
    {"game ticks (FUN_0056c140)", 0x56C16C, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xE9, 0x2B, 0x1E, 0x00}},
    {"game ticks (FUN_0056c140)", 0x56C326, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x2F, 0x2A, 0x1E, 0x00}},
    {"game ticks (FUN_0057bfa0)", 0x57C111, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x44, 0x2C, 0x1D, 0x00}},
    {"game ticks (FUN_0057c370)", 0x57C4A5, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xB0, 0x28, 0x1D, 0x00}},
    {"game ticks (FUN_0057cc90)", 0x57CCE4, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x71, 0x20, 0x1D, 0x00}},
    {"game ticks (FUN_0057d380)", 0x57D4F5, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x60, 0x18, 0x1D, 0x00}},
    {"game ticks (FUN_0057e300)", 0x57E326, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x2F, 0x0A, 0x1D, 0x00}},
    {"game ticks (FUN_0057f8b0)", 0x57FD98, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xBD, 0xEF, 0x1C, 0x00}},
    {"game ticks (FUN_00581270)", 0x581C01, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x54, 0xD1, 0x1C, 0x00}},
    {"game ticks (FUN_00581c70)", 0x5821CE, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x87, 0xCB, 0x1C, 0x00}},
    {"game ticks (FUN_00586150)", 0x586AB6, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x9F, 0x82, 0x1C, 0x00}},
    {"game ticks (FUN_00586150)", 0x586D7F, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xD6, 0x7F, 0x1C, 0x00}},
    {"game ticks (FUN_00597a30)", 0x597B31, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x24, 0x72, 0x1B, 0x00}},
    {"game ticks (FUN_00597a30)", 0x597B6D, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xE8, 0x71, 0x1B, 0x00}},
    {"game ticks (FUN_0059dc40)", 0x59DCFC, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x59, 0x10, 0x1B, 0x00}},
    {"game ticks (FUN_0059dc40)", 0x59DEBC, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x99, 0x0E, 0x1B, 0x00}},
    {"game ticks (FUN_0059e9d0)", 0x59EA8A, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xCB, 0x02, 0x1B, 0x00}},
    {"game ticks (FUN_0059e9d0)", 0x59EC41, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x14, 0x01, 0x1B, 0x00}},
    {"game ticks (FUN_005a03a0)", 0x5A03D2, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x83, 0xE9, 0x1A, 0x00}},
    {"game ticks (FUN_005a03a0)", 0x5A06A4, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xB1, 0xE6, 0x1A, 0x00}},
    {"game ticks (FUN_005a7d30)", 0x5A7D67, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xEE, 0x6F, 0x1A, 0x00}},
    {"game ticks (FUN_005a7d30)", 0x5A7F52, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x03, 0x6E, 0x1A, 0x00}},
    {"game ticks (FUN_005aace0)", 0x5AAD49, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x0C, 0x40, 0x1A, 0x00}},
    {"game ticks (FUN_005b2e80)", 0x5B31B9, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x9C, 0xBB, 0x19, 0x00}},
    {"game ticks (FUN_005b8080)", 0x5B80A6, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xAF, 0x6C, 0x19, 0x00}},
    {"game ticks (FUN_005b8080)", 0x5B8258, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xFD, 0x6A, 0x19, 0x00}},
    {"game ticks (FUN_005b8ef0)", 0x5B8F12, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x43, 0x5E, 0x19, 0x00}},
    {"game ticks (FUN_005b98a0)", 0x5BA11E, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x37, 0x4C, 0x19, 0x00}},
    {"game ticks (FUN_005baad0)", 0x5BAF33, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x22, 0x3E, 0x19, 0x00}},
    {"game ticks (FUN_005bb6a0)", 0x5BB6C0, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x95, 0x36, 0x19, 0x00}},
    {"game ticks (FUN_005bc940)", 0x5BC952, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x03, 0x24, 0x19, 0x00}},
    {"game ticks (FUN_005bc940)", 0x5BC9C5, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x90, 0x23, 0x19, 0x00}},
    {"game ticks (FUN_005c10f0)", 0x5C1122, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x33, 0xDC, 0x18, 0x00}},
    {"game ticks (FUN_005c10f0)", 0x5C130F, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x46, 0xDA, 0x18, 0x00}},
    {"game ticks (FUN_005c5250)", 0x5C5280, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xD5, 0x9A, 0x18, 0x00}},
    {"game ticks (FUN_005c5250)", 0x5C5423, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x32, 0x99, 0x18, 0x00}},
    {"game ticks (FUN_005cbc80)", 0x5CBCF8, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x5D, 0x30, 0x18, 0x00}},
    {"game ticks (FUN_005ffe80)", 0x6007F7, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x5E, 0xE5, 0x14, 0x00}},
    {"game ticks (FUN_00614f00)", 0x6168E1, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x74, 0x84, 0x13, 0x00}},
    {"game ticks (FUN_00642640)", 0x644A5A, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xFB, 0xA2, 0x10, 0x00}},
    {"game ticks (FUN_00674600)", 0x67486E, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xE7, 0xA4, 0x0D, 0x00}},
    {"game ticks (FUN_00674600)", 0x6748CD, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x88, 0xA4, 0x0D, 0x00}},
    {"game ticks (FUN_00679880)", 0x6798C9, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x8C, 0x54, 0x0D, 0x00}},
    {"game ticks (FUN_00679880)", 0x67994E, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x07, 0x54, 0x0D, 0x00}},
    {"game ticks (FUN_0067f020)", 0x67FCD0, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x85, 0xF0, 0x0C, 0x00}},
    {"game ticks (FUN_0067f020)", 0x67FCE1, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x74, 0xF0, 0x0C, 0x00}},
    {"game ticks (FUN_0067f020)", 0x67FCF2, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x63, 0xF0, 0x0C, 0x00}},
    {"game ticks (FUN_00685180)", 0x685E78, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xDD, 0x8E, 0x0C, 0x00}},
    {"game ticks (FUN_00685180)", 0x685E89, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xCC, 0x8E, 0x0C, 0x00}},
    {"game ticks (FUN_00685180)", 0x685E9A, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xBB, 0x8E, 0x0C, 0x00}},
    {"game ticks (FUN_0068b9c0)", 0x68CA0E, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x47, 0x23, 0x0C, 0x00}},
    {"game ticks (FUN_0068b9c0)", 0x68CA1F, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x36, 0x23, 0x0C, 0x00}},
    {"game ticks (FUN_0068b9c0)", 0x68CA30, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x25, 0x23, 0x0C, 0x00}},
    {"game ticks (FUN_0068fa00)", 0x690C2D, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x28, 0xE1, 0x0B, 0x00}},
    {"game ticks (FUN_0068fa00)", 0x690ED6, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x7F, 0xDE, 0x0B, 0x00}},
    {"game ticks (FUN_00691750)", 0x69208E, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xC7, 0xCC, 0x0B, 0x00}},
    {"game ticks (FUN_00691750)", 0x69209F, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xB6, 0xCC, 0x0B, 0x00}},
    {"game ticks (FUN_00691750)", 0x6920B0, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xA5, 0xCC, 0x0B, 0x00}},
    {"game ticks (FUN_00694d70)", 0x696A26, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x2F, 0x83, 0x0B, 0x00}},
    {"game ticks (FUN_00697120)", 0x697963, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xF2, 0x73, 0x0B, 0x00}},
    {"game ticks (FUN_00697120)", 0x697974, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xE1, 0x73, 0x0B, 0x00}},
    {"game ticks (FUN_00697120)", 0x697985, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xD0, 0x73, 0x0B, 0x00}},
    {"game ticks (FUN_0069bef0)", 0x69C349, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x0C, 0x2A, 0x0B, 0x00}},
    {"game ticks (FUN_0069f3b0)", 0x69F65A, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xFB, 0xF6, 0x0A, 0x00}},
    {"game ticks (FUN_006a4620)", 0x6A474C, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x09, 0xA6, 0x0A, 0x00}},
    {"game ticks (FUN_006a66b0)", 0x6A68C9, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x8C, 0x84, 0x0A, 0x00}},
    {"game ticks (FUN_006a66b0)", 0x6A692C, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x29, 0x84, 0x0A, 0x00}},
    {"game ticks (FUN_006a84a0)", 0x6A8E86, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xCF, 0x5E, 0x0A, 0x00}},
    {"game ticks (FUN_006a84a0)", 0x6A8EC8, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x8D, 0x5E, 0x0A, 0x00}},
    {"game ticks (FUN_006a84a0)", 0x6A8ED9, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x7C, 0x5E, 0x0A, 0x00}},
    {"game ticks (FUN_006a84a0)", 0x6A8EEA, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x6B, 0x5E, 0x0A, 0x00}},
    {"game ticks (FUN_006ab770)", 0x6ABC14, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x41, 0x31, 0x0A, 0x00}},
    {"game ticks (FUN_006ace50)", 0x6ACE8F, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0xC6, 0x1E, 0x0A, 0x00}},
    {"game ticks (FUN_006aced0)", 0x6ACEDE, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x77, 0x1E, 0x0A, 0x00}},
    {"game ticks (FUN_006ae740)", 0x6AE7DB, 11, {0xD9, 0x05, 0xE0, 0xFF, 0x4A, 0x01, 0xE8, 0x7A, 0x05, 0x0A, 0x00}},
};
// Sound ramps of the car in FUN_0054BFC0, moved by a constant per frame with no frame delta: the tyre squeal (brake sound) goes to
// its target by 0.05 (`fld qword [00770310h]` at 0054C877: the one load serves the compare and the add or subtract), the engine
// volume rises by 0.05 (`fadd qword [00770310h]`) and the idle volume falls by 0.01 (`fsub qword [007702D8h]`). The operand
// (bytes 2..5) is pointed at a double of the mod: the game's constant times the frame's share of a step. Both constants are shared
// with other code, so they are read, never written. The fall of the engine volume by 0.1 (0054C23B) subtracts a value that is also
// a threshold for the triggers: it is left as it is.
constexpr SiteDef kSiteCarRamps[3] = {
    {"tyre squeal step", 0x54C877, 6, {0xDD, 0x05, 0x10, 0x03, 0x77, 0x00}},
    {"engine volume rise", 0x54C35B, 6, {0xDC, 0x05, 0x10, 0x03, 0x77, 0x00}},
    {"idle volume fall", 0x54C336, 6, {0xDC, 0x25, 0xD8, 0x02, 0x77, 0x00}},
};
constexpr SiteDef kSiteRampConstants[2] = {
    {"sound ramp constant 0.05 (double)", 0x770310, 8, {0x00, 0x00, 0x00, 0xA0, 0x99, 0x99, 0xA9, 0x3F}},
    {"sound ramp constant 0.01 (double)", 0x7702D8, 8, {0x00, 0x00, 0x00, 0x40, 0xE1, 0x7A, 0x84, 0x3F}},
};
// Effects with bit 2 of effect+1C0h (muzzle flashes, splashes, hits) take a constant step of 1.0 instead of the frame step:
// half speed on the console (30 frames/s, step 2) but full speed at the PC's 60 frames/s. Writing 0.5 puts them back at the
// console's speed. FUN_0071C190 (16 bytes at 0071C211) and its twin FUN_00674F40 (24 bytes at 00674F72, tyre effects).
constexpr SiteDef kSiteEffectStepA = {"effect step A", 0x71C211, 16, {0x8B, 0x88, 0xC0, 0x01, 0x00, 0x00, 0x83, 0xE1, 0x02, 0x74, 0x05, 0xD9, 0xE8, 0xD9, 0x5D, 0xFC}};
constexpr uint8_t kEffectStepAFixed[16] = {0xF6, 0x80, 0xC0, 0x01, 0x00, 0x00, 0x02, 0x74, 0x07, 0xC7, 0x45, 0xFC, 0x00, 0x00, 0x00, 0x3F};
constexpr SiteDef kSiteEffectStepB = {"effect step B", 0x674F72, 16, {0xB0, 0x02, 0x84, 0x86, 0xC0, 0x01, 0x00, 0x00, 0x74, 0x06, 0xD9, 0xE8, 0xD9, 0x5C, 0x24, 0x08}};
constexpr uint8_t kEffectStepBTail[8] = {0x84, 0x86, 0xB4, 0x01, 0x00, 0x00, 0x75, 0x07};  // the 8 bytes after the 16 above
constexpr uint8_t kEffectStepBFixed[24] = {0xF6, 0x86, 0xC0, 0x01, 0x00, 0x00, 0x02, 0x74, 0x08, 0xC7, 0x44, 0x24, 0x08, 0x00, 0x00, 0x00,
                                           0x3F, 0xF6, 0x86, 0xB4, 0x01, 0x00, 0x00, 0x02};
// Controller layout with aim and fire on the triggers. The binding globals (codes of FUN_006B1780: 0..31 button numbers) hold
// aim at BE5904h (Space), attack at BE58FCh (left mouse), run at BE5900h and hold breath at BE58F8h. L2 and R2 are given to the
// game as the buttons 10 and 11 so that both can be held together (the shared Z axis cannot). FUN_00459F70 turns a binding
// into the number of an icon in the atlas; its only call (in the text drawer) is redirected so that buttons 10 and 11 show the
// LT and RT icons (cells 13h and 14h).
constexpr uint32_t kBindAim = 0xBE5904, kBindAttack = 0xBE58FC, kBindRun = 0xBE5900, kBindBreath = 0xBE58F8;
constexpr SiteDef kSiteGlyphIndexCall = {"icon index call", 0x45E568, 5, {0xE8, 0x03, 0xBA, 0xFF, 0xFF}};
constexpr uint32_t kGlyphIndexFn = 0x459F70;
// Exposure of the composite pass: the key is multiplied by 0.85 (and by 0.75 more in one cutscene state). The console
// did neither. Each `fmul qword [const]` has its operand pointed at our own 1.0 when the Xbox exposure is on (the
// constants themselves are shared with other code and stay as they are).
constexpr SiteDef kSiteExposure85 = {"exposure x0.85", 0x6EA202, 6, {0xDC, 0x0D, 0xA0, 0xF9, 0x77, 0x00}};
constexpr SiteDef kSiteExposure75 = {"exposure x0.75", 0x6EA1A7, 6, {0xDC, 0x0D, 0x50, 0x35, 0x77, 0x00}};
// The pixel shader of the final composite: variant 30 of the tone-map blob at BCEE68h (FUN_006EA8E0 picks n*30 with
// n = [post+248h]; variant 14 at BD1628h has the same bytes). ps_3_0, 1000 bytes, samplers s0..s4, reads c9, c10, c15 and
// c16 only: the contrast, pitch, chroma and colour offset the game sets in c11..c14 are not read by any of the 32 variants.
// The Xbox 360 look swaps it for one draw; the bound shader is recognised by the size and the FNV-1a of its byte code, and
// tests/verify_sites.cpp checks both copies in DP.exe against the same two numbers.
constexpr SiteDef kCompositeShader = {"composite pixel shader", 0xBD4638, 16,
    {0x00, 0x03, 0xFF, 0xFF, 0xFE, 0xFF, 0x6A, 0x00, 0x43, 0x54, 0x41, 0x42, 0x1C, 0x00, 0x00, 0x00}};
constexpr uint32_t kCompositeShaderCopyVa = 0xBD1628, kCompositeShaderSize = 1000, kCompositeShaderFnv = 0x62D2C59B;
inline uint32_t Fnv1a(const uint8_t* data, uint32_t length) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < length; ++i) h = (h ^ data[i]) * 16777619u;
    return h;
}

// ---- Controller bindings (configJ.cnf / configJex.cnf) ---------------------------------------------------------
// In controller mode every action reads its binding code from these globals, filled from the two files at start.
// Without the files all are 0, i.e. every action sits on button A. Codes (FUN_006B1780): 0..31 = button number,
// 29h..2Ch = hat up/down/left/right, 2Dh/2Eh X-/X+, 2Fh/30h Y-/Y+, 31h/32h Z-/Z+, 33h/34h U-/U+, 35h/36h V-/V+,
// 37h/38h R-/R+ (31 is used for "unbound": button 31 never exists).
struct PadBinding {
    uint32_t va;
    uint32_t code;
};
// configJ.cnf: the Xbox 360 layout shipped with DPfix (A interact, B reload, X light, Y observe, LB aim, RB attack,
// Z- hold breath, Z+ run).
constexpr PadBinding kPadBindingsJ[8] = {
    {0xBE58E8, 1}, {0xBE58EC, 0}, {0xBE58F0, 2}, {0xBE58F4, 3}, {0xBE58F8, 0x31}, {0xBE58FC, 5}, {0xBE5900, 0x32}, {0xBE5904, 4},
};
// configJex.cnf: Back, Start, hat, left stick, right stick (key of the keyboard layout each one stands for in brackets).
constexpr PadBinding kPadBindingsJex[16] = {
    {0xBE5908, 6},    // Back (M)
    {0xBE590C, 7},    // Start (Esc)
    {0xBE5910, 0x2B}, // hat left (Z)
    {0xBE5914, 0x2C}, // hat right (X)
    {0xBE5918, 0x29}, // hat up (PgUp)
    {0xBE591C, 0x2A}, // hat down (PgDn)
    {0xBE5920, 0x2D}, // left stick X- (A)
    {0xBE5924, 0x2E}, // left stick X+ (D)
    {0xBE5928, 0x2F}, // left stick Y- (W)
    {0xBE592C, 0x30}, // left stick Y+ (S)
    {0xBE5930, 31},   // (7) unbound
    {0xBE5934, 0x33}, // right stick X- (J)
    {0xBE5938, 0x34}, // right stick X+ (L)
    {0xBE593C, 0x37}, // right stick Y- (I)
    {0xBE5940, 0x38}, // right stick Y+ (K)
    {0xBE5944, 31},   // (8) unbound
};

// Language: the start-up code calls FUN_006B1D80 (reads config.cnf and sets the language slot 0BE58E4h).
// A translation lives in the US slot (6): we can force it after that call.
constexpr SiteDef kSiteLanguageCall = {"language setup call", 0x6B25FD, 5, {0xE8, 0x7E, 0xF7, 0xFF, 0xFF}};
constexpr uint32_t kLanguageSetupFn = 0x6B1D80;
constexpr uint32_t kLanguageSlotVa = 0xBE58E4;

// ---- Sound: XACT3 cues and the game's X3DAudio statics ---------------------------------------------------------
// FUN_0072CB00 (thiscall, ret 8) updates one playing cue every tick. Its frame: [ebp-8] = the sound record with the
// IXACT3Cue at +14h (checked for null right before both sites), [ebp+0Ch] = the voice parameters. Both branches end in
// cue->SetMatrixCoefficients(2, 2, matrix) (vtable +10h, stdcall), whatever the number of channels of the mix is:
//   3D voice: lea eax,[ebp-18h] / push eax / push 2 / push 2 / mov ecx,[ebp-8] / mov edx,[ecx+14h] / mov eax,[ebp-8] /
//             mov ecx,[eax+14h] / mov edx,[edx] / push ecx / mov eax,[edx+10h] / call eax        (next: 0072CCEA push "pitch")
//   2D voice: the same with the matrix at [ebp-54h] and the registers swapped around             (next: 0072CD73 epilogue)
// 28 bytes each; no jump lands inside either run.
constexpr uint32_t kCueMatrixLen = 28;
constexpr SiteDef kSiteCueMatrix3d = {"cue matrix 2x2 (3D voice)", 0x72CCCE, 16,
    {0x8D, 0x45, 0xE8, 0x50, 0x6A, 0x02, 0x6A, 0x02, 0x8B, 0x4D, 0xF8, 0x8B, 0x51, 0x14, 0x8B, 0x45}};
constexpr uint8_t kCueMatrix3dTail[12] = {0xF8, 0x8B, 0x48, 0x14, 0x8B, 0x12, 0x51, 0x8B, 0x42, 0x10, 0xFF, 0xD0};
constexpr SiteDef kSiteCueMatrix2d = {"cue matrix 2x2 (2D voice)", 0x72CD57, 16,
    {0x8D, 0x4D, 0xAC, 0x51, 0x6A, 0x02, 0x6A, 0x02, 0x8B, 0x55, 0xF8, 0x8B, 0x42, 0x14, 0x8B, 0x4D}};
constexpr uint8_t kCueMatrix2dTail[12] = {0xF8, 0x8B, 0x51, 0x14, 0x8B, 0x00, 0x52, 0x8B, 0x48, 0x10, 0xFF, 0xD1};
constexpr int8_t kCueMatrix3dDisp = -0x18;  // where each branch builds its 2x2 matrix, relative to ebp
constexpr int8_t kCueMatrix2dDisp = -0x54;
// The X3DAudio statics, set up once by the CSdCore constructor FUN_0072BA00. The three sites below are only compared, never
// patched: they are the proof of the addresses and of the size of the matrix buffer.
//   0072BE7F: push 40h / push 0 / push 014B03B8h (memset)       the matrix buffer is 40h bytes = 16 floats (2 sources x 8)
//   0072BEC4: mov [014B02F0h], 014B03B8h / mov [014B02F8h], ... the DSP settings point at it (+0), source channels at +8
//   0072CC05: push 014B02F0h                                    the per-tick update calculates into the same DSP settings
// The static CSdCore object starts at 014B0400 (0076D9B3: mov ecx, 014B0400h / call FUN_0072BA00), 8 bytes after the
// buffer: a matrix of more than 18 floats written there would run over its vtable pointer.
constexpr SiteDef kSiteX3dMatrixClear = {"X3DAudio matrix buffer clear", 0x72BE7F, 9,
    {0x6A, 0x40, 0x6A, 0x00, 0x68, 0xB8, 0x03, 0x4B, 0x01}};
constexpr SiteDef kSiteX3dDspSetup = {"X3DAudio DSP settings setup", 0x72BEC4, 16,
    {0xC7, 0x05, 0xF0, 0x02, 0x4B, 0x01, 0xB8, 0x03, 0x4B, 0x01, 0xC7, 0x05, 0xF8, 0x02, 0x4B, 0x01}};
constexpr SiteDef kSiteCueUpdateDsp = {"cue update: DSP settings pushed", 0x72CC05, 5, {0x68, 0xF0, 0x02, 0x4B, 0x01}};
constexpr uint32_t kX3dDspVa = 0x014B02F0;     // X3DAUDIO_DSP_SETTINGS, 30h bytes
constexpr uint32_t kX3dMatrixVa = 0x014B03B8;  // its pMatrixCoefficients
constexpr uint32_t kX3dMatrixFloats = 16;

// ---- Resource read guard (resource_guard.cpp) ---------------------------------------------------------------------
// Two readers unpack an "XZP1" record (16-byte header: magic, next, packed size, unpacked size; then a zlib stream):
//   FUN_006B0000 (thiscall, ecx = resource index, arguments id and u32* sizeOut, `ret 8` at 006B022F) reads a record of
//     the DPSERIAL archives; header at [ebp-120h], so the unpacked size is [ebp-114h];
//   FUN_006AF370 (cdecl: path, u32* sizeOut, root; plain `ret` at 006AF581) reads a loose file; header at [ebp-13Ch],
//     unpacked size [ebp-130h].
// Both allocate the unpacked size and call FUN_006AF5B0(void* out, u32 packed, CSafeRead* file) (cdecl, returns AL,
// `add esp,0Ch` after each call). When it returns 0 they free the buffer and jump back to read the same record again,
// with no limit: a record that cannot be unpacked is a silent endless loop of the loading thread. The jump is led
// through a counter; after the last attempt it goes to the function's own exit for a missing resource instead
// (*sizeOut = 0, return 0; the caller FUN_006B33F0 then returns -1 at 006B3685 and creates no slot).
constexpr SiteDef kSiteArchiveRetry = {"archive unpack retry jump", 0x6B0162, 5, {0xE9, 0xEC, 0xFE, 0xFF, 0xFF}};
constexpr uint32_t kArchiveRetryTarget = 0x6B0053;
constexpr SiteDef kSiteArchiveMissing = {"archive reader: missing exit", 0x6B003D, 16,
    {0x83, 0x7D, 0x0C, 0x00, 0x74, 0x09, 0x8B, 0x4D, 0x0C, 0xC7, 0x01, 0x00, 0x00, 0x00, 0x00, 0x33}};
constexpr SiteDef kSiteLooseRetry = {"loose file unpack retry jump", 0x6AF45A, 5, {0xE9, 0x4E, 0xFF, 0xFF, 0xFF}};
constexpr uint32_t kLooseRetryTarget = 0x6AF3AD;
constexpr SiteDef kSiteLooseMissing = {"loose file reader: missing exit", 0x6AF538, 16,
    {0x83, 0x7D, 0x0C, 0x00, 0x74, 0x09, 0x8B, 0x4D, 0x0C, 0xC7, 0x01, 0x00, 0x00, 0x00, 0x00, 0xC7}};
// Inside FUN_006AF5B0 (frame: ebp; z_stream at [ebp-3Ch], so avail_out is [ebp-2Ch] and total_out [ebp-28h]) the room
// for output is set to 20000h before the loop and again whenever it reaches 0, while next_out just keeps advancing: the
// stream may write any amount into a buffer of the unpacked size. Both `mov dword [ebp-2Ch], 20000h` are replaced by a
// call that gives min(20000h, what is left of the caller's buffer). The caller is told by the return address (the two
// calls below) and its buffer size is read from its frame (the two `mov ecx,[ebp-..] / push ecx / call operator new`).
constexpr SiteDef kSiteInflateRoomFirst = {"inflate output room (first)", 0x6AF61D, 7, {0xC7, 0x45, 0xD4, 0x00, 0x00, 0x02, 0x00}};
constexpr uint32_t kInflateRoomFirstReturn = 0x6AF624;
constexpr SiteDef kSiteInflateRoomRefill = {"inflate output room (refill)", 0x6AF6D9, 7, {0xC7, 0x45, 0xD4, 0x00, 0x00, 0x02, 0x00}};
constexpr uint32_t kInflateRoomRefillReturn = 0x6AF6E0;
constexpr SiteDef kSiteArchiveInflateCall = {"archive reader: unpack call", 0x6B0138, 5, {0xE8, 0x73, 0xF4, 0xFF, 0xFF}};
constexpr SiteDef kSiteLooseInflateCall = {"loose file reader: unpack call", 0x6AF425, 5, {0xE8, 0x86, 0x01, 0x00, 0x00}};
constexpr SiteDef kSiteArchiveBufferSize = {"archive reader: buffer size", 0x6B00F2, 8, {0x8B, 0x8D, 0xEC, 0xFE, 0xFF, 0xFF, 0x51, 0xE8}};
constexpr SiteDef kSiteLooseBufferSize = {"loose file reader: buffer size", 0x6AF3EF, 8, {0x8B, 0x8D, 0xD0, 0xFE, 0xFF, 0xFF, 0x51, 0xE8}};
constexpr uint32_t kInflateRoomStep = 0x20000;      // the game's own amount
constexpr int kArchiveUnpackedSizeOffset = -0x114;  // in the frame of FUN_006B0000
constexpr int kLooseUnpackedSizeOffset = -0x130;    // in the frame of FUN_006AF370

// ---- Sound load requests (diagnostics.cpp reads the number in use, nothing is patched) -----------------------------
// The main tick loads the CAudio_Data singleton from 00BD9E18h and calls its update FUN_00702890, which walks the pool
// of load requests at +1070h: a TPoolList<CAudio_Data::LOADREQUEST_ITEM,64,0> whose constructor stores the vftable
// 00827308h at +0 and whose number of requests in use is at +4 (FUN_00703AE0). Taking a request (FUN_007039F0) pops a
// free index without checking that one is left.
constexpr SiteDef kSiteAudioDataLoad = {"audio data singleton load", 0x401B28, 6, {0x8B, 0x35, 0x18, 0x9E, 0xBD, 0x00}};
constexpr SiteDef kSiteAudioPoolOffset = {"audio request pool offset", 0x7028AA, 14,
    {0x8B, 0x4D, 0xEC, 0x81, 0xC1, 0x70, 0x10, 0x00, 0x00, 0xE8, 0x68, 0x12, 0x00, 0x00}};
constexpr SiteDef kSiteAudioPoolVtable = {"audio request pool vftable", 0x70383B, 6, {0xC7, 0x00, 0x08, 0x73, 0x82, 0x00}};
constexpr uint32_t kAudioDataPtrVa = 0xBD9E18, kAudioRequestPoolOffset = 0x1070, kAudioRequestPoolVtable = 0x827308;
constexpr int kAudioRequestPoolSize = 64;

// ___security_init_cookie lives here and must keep seeing the raw counter.
constexpr uint32_t kCookieFnBegin = 0x75C4DE;
constexpr uint32_t kCookieFnEnd = 0x75C560;

// Every import the DLL hooks through the import table of DP.exe.
constexpr ImportDef kImports[] = {
    {"KERNEL32.dll", "QueryPerformanceCounter", 0x76E080},
    {"KERNEL32.dll", "QueryPerformanceFrequency", 0x76E078},
    {"KERNEL32.dll", "CreateFileA", 0x76E034},
    {"KERNEL32.dll", "WriteFile", 0x76E038},
    {"KERNEL32.dll", "CloseHandle", 0x76E01C},
    {"KERNEL32.dll", "SetUnhandledExceptionFilter", 0x76E180},
    {"WINMM.dll", "joyGetPosEx", 0x76E250},
    {"USER32.dll", "DefWindowProcA", 0x76E210},
    {"USER32.dll", "GetAsyncKeyState", 0x76E248},
    {"USER32.dll", "GetKeyboardState", 0x76E204},
    {"USER32.dll", "GetCursorPos", 0x76E1F4},
    {"USER32.dll", "SetCursorPos", 0x76E1F0},
    {"USER32.dll", "ShowCursor", 0x76E238},
    {"d3d9.dll", "Direct3DCreate9", 0x76E264},
    {"d3dx9_43.dll", "D3DXCreateTextureFromFileInMemoryEx", 0x76E2BC},
    {"X3DAudio1_7.dll", "X3DAudioCalculate", 0x76E258},   // not hooked: the two slots the game reaches our own exports through
    {"X3DAudio1_7.dll", "X3DAudioInitialize", 0x76E25C},
};

// Bytes the DLL checks beyond the 16 a SiteDef holds: tests/verify_sites.cpp compares them with DP.exe too.
struct TailDef {
    const char* name;
    uint32_t va;
    uint8_t len;
    const uint8_t* bytes;
};

// The NaN trap of the angle normalisation FUN_00409E90 (323 calls in 158 functions): after `call __isnan` the code is
// test eax,eax / jz 409EA8 / jmp $ (EB FE), so a NaN angle hangs the game for good. docs/crashes/03_nan_spin_loops.md.
constexpr SiteDef kSiteNanTrap = {"angle NaN trap", 0x409EA2, 6, {0x85, 0xC0, 0x74, 0x02, 0xEB, 0xFE}};

// The two callers of the resident route lookup FUN_004D8980 that copy its result into the resident's position (ESI = resident).
// docs/crashes/07 F3a. The third one (0047D804, the walking target) changes the AI and is left alone.
constexpr uint32_t kRouteLookupFn = 0x4D8980;
constexpr SiteDef kSiteRouteCall1 = {"resident route lookup (rider)", 0x4CCD50, 5, {0xE8, 0x2B, 0xBC, 0x00, 0x00}};
constexpr SiteDef kSiteRouteCall2 = {"resident route lookup", 0x4D1363, 5, {0xE8, 0x18, 0x76, 0x00, 0x00}};

// The stick-waggle QTE (player state 7Dh, alternate A/D or the left stick): the gauge +8C4h grows by how far the stick moved
// this frame times the frame delta (dt * 60). The travel of one A-to-D swap does not depend on the frame rate, so at 180 FPS
// a swap counts a third and the QTE runs out of time. The multiply becomes max(dt * 60, 1): as the game at 60 FPS and below.
// FUN_006D32E0, right after the scene list: the planar reflection pass (FUN_006D8B50, 006D454C far / 006D464C near) runs only
// when a reflection source was found (local [ebp-1988h] != -1: 1 water 40000h, 0 mirror), its weight [ebp-1030h] > 0, and the
// blend [ebp-3DF0h] is above 0.01 (far) or below 0.99 (near). The water diagnostic records these three here every frame.
constexpr SiteDef kSiteReflectionChoice = {"reflection source check", 0x6D44DF, 7, {0x83, 0xBD, 0x78, 0xE6, 0xFF, 0xFF, 0xFF}};
constexpr uint32_t kReflectionChoiceBack = 0x6D44E6;
// The player's own physics, once a frame from the player event (FUN_0052EBC0): FUN_004E3280 (thiscall, ecx = player) finds the
// ground, applies the fall (+958h) and the collisions, and keeps York's two PhysX shapes (+94Ch, +950h) with him. The fly cheat
// replaces it while York flies with FUN_004E31F0(player, 0.0) (thiscall, ret 4: both shapes put where York is, nothing else) and,
// once on landing, with FUN_004E3110 (the game's own sync after a teleport: the shape swept down onto the ground).
constexpr SiteDef kSitePlayerPhysicsCall = {"player physics call", 0x52F55D, 5, {0xE8, 0x1E, 0x3D, 0xFB, 0xFF}};
constexpr uint32_t kPlayerPhysicsFn = 0x4E3280, kPlayerShapesToFn = 0x4E31F0, kPlayerLandFn = 0x4E3110;
constexpr SiteDef kSiteQteGauge = {"waggle QTE gauge x frame delta", 0x4EB60E, 6, {0xD8, 0x0D, 0xE0, 0xFF, 0x4A, 0x01}};
// Lock-on (FUN_0053A5C0, the branch with a target in player+0x808): the camera pitch goes from York's head (NT_FHEAD) to the
// target's waist bone (FUN_0053A580: PM_WAIST from +0x5FE, N_BODY2 for kinds 4/5), and the height of that point is lowered by
// 0.1 x the horizontal distance (`fld qword [0076F6F8h]` = 0.1, `fmul st(1)` / `fsubp st(2)`): at 30 units 3 below the waist,
// at 60 units 6, so the lock aims at the hips and the legs. The drop site loads 0.1, the height site is
// `fld [esp+34h] / fsub [esp+14h] / fstp [esp+24h]` (target y minus eye y).
constexpr SiteDef kSiteLockOnDrop = {"lock-on aim drop (0.1 x distance)", 0x53A9AC, 6, {0xDD, 0x05, 0xF8, 0xF6, 0x76, 0x00}};
constexpr SiteDef kSiteLockOnHeight = {"lock-on aim height (target y - eye y)", 0x53A95F, 12,
                                       {0xD9, 0x44, 0x24, 0x34, 0xD8, 0x64, 0x24, 0x14, 0xD9, 0x5C, 0x24, 0x24}};
constexpr uint32_t kLockOnHeightBack = 0x53A96B;

// Full-screen 2D quads (fades, the item notice): FUN_006E3360 (thiscall, the rect {x0, y0, x1, y1} or 0 = the 2D layer's
// 1280x720 as the first stack argument, ret 14h) moves every corner half a unit to the left and up. Units are 1280x720, so
// on a 4K back buffer that is 1.5 pixels: a 1-pixel strip stays uncovered at the right and the bottom (ZachFix issue 10).
// The three full-screen callers ZachFix found: 0044A119 and 0044ACC0 (the fade manager), 0061BF43 (the item notice).
constexpr uint32_t kQuadFn = 0x6E3360;
constexpr SiteDef kSiteFullQuad[3] = {
    {"full-screen quad (fade)", 0x44A119, 5, {0xE8, 0x42, 0x92, 0x29, 0x00}},
    {"full-screen quad (fade 2)", 0x44ACC0, 5, {0xE8, 0x9B, 0x86, 0x29, 0x00}},
    {"full-screen quad (item notice)", 0x61BF43, 5, {0xE8, 0x18, 0x74, 0x0C, 0x00}},
};

// Threads of the game that share data without locks (docs/crashes/11, 2.5). SetThread builds world objects while the main thread
// already updates the next frame: the call at 00401C9D wakes it, and only the next tick (00401C7D) waits for it. RouteThread
// works out a resident's path while a level change clears the queue (call 0040BE10 at 004B1B2F and 004B6A6D) without waiting.
constexpr SiteDef kSiteSetThreadWake = {"SetThread wake", 0x401C9D, 5, {0xE8, 0xEE, 0xAF, 0x00, 0x00}};
constexpr uint32_t kSetThreadWakeFn = 0x40CC90, kSetThreadWaitFn = 0x40CC60;
constexpr SiteDef kSiteRouteFlush1 = {"route queue flush (residents cleared)", 0x4B1B2F, 5, {0xE8, 0xDC, 0xA2, 0xF5, 0xFF}};
constexpr SiteDef kSiteRouteFlush2 = {"route queue flush (level change)", 0x4B6A6D, 5, {0xE8, 0x9E, 0x53, 0xF5, 0xFF}};
constexpr SiteDef kSiteRouteFlushFn = {"route queue flush function", 0x40BE10, 11, {0xC7, 0x05, 0xDC, 0x80, 0x88, 0x00, 0x01, 0x00, 0x00, 0x00, 0xC3}};
constexpr uint32_t kRouteWakeFn = 0x40BE50;          // cdecl: marks the thread busy and wakes it
constexpr uint32_t kRouteMutexVa = 0x8880B4;         // HANDLE of the queue mutex
constexpr uint32_t kRouteQueueBegin = 0x8880D0, kRouteQueueEnd = 0x8880D4;  // std::vector of 8-byte requests
constexpr uint32_t kRouteFlushFlag = 0x8880DC;       // 1 = clear the queue; the thread sets it back to 0 after its request

// Loader thread idle wait (FUN_006B3CB0): with its queue empty it sleeps 16 ms at a time, and every synchronous load waits on
// it, so each one can cost a timer quantum. 1 ms instead (the mod sets the 1 ms timer resolution). docs/crashes/04 V1.
constexpr SiteDef kSiteLoaderSleep = {"loader idle sleep", 0x6B3CDD, 8, {0x85, 0xC0, 0x75, 0x08, 0x6A, 0x10, 0xFF, 0x15}};

// Mouse look (input_extras.cpp): the two readers of the mouse movement, FUN_0070AF50 (x) and FUN_0070B000 (y), do
// `fild sens / fmul dword [movement] / call _ftol2`; the fmul becomes a call to a stub that also multiplies by our scale.
constexpr SiteDef kSiteMouseX = {"mouse x reader: fmul [dx]", 0x70AFE8, 6, {0xD8, 0x0D, 0x40, 0x00, 0x4B, 0x01}};
constexpr SiteDef kSiteMouseY = {"mouse y reader: fmul [dy]", 0x70B098, 6, {0xD8, 0x0D, 0x44, 0x00, 0x4B, 0x01}};
constexpr uint32_t kMouseDxVa = 0x14B0040, kMouseDyVa = 0x14B0044;

// Game files from mods (game_files.cpp). In the loader FUN_006B33F0 (LoadThread only), right after the CAddon lookup: [ebp+8]
// the requested id, [ebp-38h] the id after the language redirect, [ebp-40h] the CAddon path or 0, [ebp-14h] the size out,
// [ebp-1Ch] the data. The branch `cmp [ebp-40h],0 / je 006B3502` is replaced: with no CAddon path our worker may hand over the
// data of a file from a mod, stored where both readers store theirs, then the code goes on at the common check 006B367F.
constexpr SiteDef kSiteGameFileBranch = {"loader: CAddon branch", 0x6B34E0, 6, {0x83, 0x7D, 0xC0, 0x00, 0x74, 0x1C}};
constexpr SiteDef kSiteGameFileAddon = {"loader: CAddon read", 0x6B34E6, 3, {0x8B, 0x45, 0xBC}};
constexpr SiteDef kSiteGameFileArchive = {"loader: archive or language branch", 0x6B3502, 6, {0x8B, 0x45, 0xC8, 0x3B, 0x45, 0x08}};
constexpr SiteDef kSiteGameFileStore = {"loader: CAddon data store", 0x6B34FA, 8, {0x89, 0x45, 0xE4, 0xE9, 0x7D, 0x01, 0x00, 0x00}};
constexpr SiteDef kSiteGameFileHave = {"loader: common data check", 0x6B367F, 6, {0x83, 0x7D, 0xE4, 0x00, 0x75, 0x08}};
constexpr uint32_t kGameMalloc = 0x73A8DC;        // cdecl malloc(size) of the game's C runtime; the loader frees slot data with free
constexpr uint32_t kResourceIndexVa = 0x8D8D78;   // 18340 records of 20h bytes, +1Ch the VA of the path (upper case, '/')
constexpr int kResourceCount = 18340;
constexpr uint32_t kNanTrapNumber = 0x409EA8;

constexpr const SiteDef* kAllSites[] = {
    &kSiteNanTrap,
    &kSiteMouseX, &kSiteMouseY, &kSiteLoaderSleep, &kSiteRouteCall1, &kSiteRouteCall2, &kSiteQteGauge, &kSitePlayerPhysicsCall, &kSiteReflectionChoice, &kSiteLockOnDrop, &kSiteLockOnHeight, &kSiteFullQuad[0], &kSiteFullQuad[1], &kSiteFullQuad[2], &kSiteSetThreadWake, &kSiteRouteFlush1, &kSiteRouteFlush2, &kSiteRouteFlushFn,
    &kSiteGameFileBranch, &kSiteGameFileAddon, &kSiteGameFileArchive, &kSiteGameFileStore, &kSiteGameFileHave,
    &kSiteBeaconCall, &kSiteBeaconDraw, &kSiteBeaconMove, &kStorySites[0], &kStorySites[1], &kStorySites[2], &kStorySites[3], &kStorySites[4],
    &kStorySites[5], &kStorySites[6], &kStorySites[7], &kStorySites[8], &kStorySites[9], &kStorySites[10],
    &kSiteZeroDelta, &kSiteHouseListFormat, &kSiteExtraJoyPoll, &kSiteSkipIntro,
    &kSiteResetRefresh, &kSitePresentCall, &kSitePreResetCall, &kSiteAniso1, &kSiteAniso2,
    &kSiteReflectionBias, &kSiteSceneInitCall, &kSitePostInitCall,
    &kCompositeShader,
    &kSiteStorageH1, &kSiteStorageW1, &kSiteStorageH2, &kSiteStorageW2,
    &kSiteWorldUpdateCalls[0], &kSiteWorldUpdateCalls[1], &kSiteCarDriveCall, &kSiteCarAfterStepCall, &kSiteCarTicks[0], &kSiteCarTicks[1], &kSiteCarTicks[2],
    &kSiteCarTicks[3], &kSiteCarTicks[4], &kSiteCarTicks[5], &kSiteCarTicks[6], &kSiteCarTicks[7], &kSiteCarTicks[8], &kSiteCarTicks[9],
    &kSiteCarRamps[0], &kSiteCarRamps[1], &kSiteCarRamps[2],
    &kSiteGameTicks[0], &kSiteGameTicks[1], &kSiteGameTicks[2], &kSiteGameTicks[3], &kSiteGameTicks[4], &kSiteGameTicks[5], &kSiteGameTicks[6], &kSiteGameTicks[7], &kSiteGameTicks[8], &kSiteGameTicks[9], &kSiteGameTicks[10], &kSiteGameTicks[11], &kSiteGameTicks[12], &kSiteGameTicks[13], &kSiteGameTicks[14], &kSiteGameTicks[15], &kSiteGameTicks[16], &kSiteGameTicks[17], &kSiteGameTicks[18], &kSiteGameTicks[19], &kSiteGameTicks[20], &kSiteGameTicks[21], &kSiteGameTicks[22], &kSiteGameTicks[23], &kSiteGameTicks[24], &kSiteGameTicks[25], &kSiteGameTicks[26], &kSiteGameTicks[27], &kSiteGameTicks[28], &kSiteGameTicks[29], &kSiteGameTicks[30], &kSiteGameTicks[31], &kSiteGameTicks[32], &kSiteGameTicks[33], &kSiteGameTicks[34], &kSiteGameTicks[35], &kSiteGameTicks[36], &kSiteGameTicks[37], &kSiteGameTicks[38], &kSiteGameTicks[39], &kSiteGameTicks[40], &kSiteGameTicks[41], &kSiteGameTicks[42], &kSiteGameTicks[43], &kSiteGameTicks[44], &kSiteGameTicks[45], &kSiteGameTicks[46], &kSiteGameTicks[47], &kSiteGameTicks[48], &kSiteGameTicks[49], &kSiteGameTicks[50], &kSiteGameTicks[51], &kSiteGameTicks[52], &kSiteGameTicks[53], &kSiteGameTicks[54], &kSiteGameTicks[55], &kSiteGameTicks[56], &kSiteGameTicks[57], &kSiteGameTicks[58], &kSiteGameTicks[59], &kSiteGameTicks[60], &kSiteGameTicks[61], &kSiteGameTicks[62], &kSiteGameTicks[63], &kSiteGameTicks[64], &kSiteGameTicks[65], &kSiteGameTicks[66], &kSiteGameTicks[67], &kSiteGameTicks[68], &kSiteGameTicks[69], &kSiteGameTicks[70], &kSiteGameTicks[71], &kSiteGameTicks[72], &kSiteGameTicks[73], &kSiteGameTicks[74], &kSiteGameTicks[75], &kSiteGameTicks[76], &kSiteGameTicks[77],
    &kSiteRampConstants[0], &kSiteRampConstants[1], &kSiteWarmUpFlagRead,
    &kOutPresentParams, &kOutDepthH, &kOutDepthW, &kOutFrameBegin1, &kOutFrameBegin2, &kOutPopViewport,
    &kSiteQuickTurnGate, &kQuickTurnChecks[0], &kQuickTurnChecks[1], &kQuickTurnChecks[2], &kQuickTurnChecks[3], &kQuickTurnChecks[4], &kQuickTurnChecks[5],
    &kOutHudDepthBind, &kOutViewport1, &kOutViewport2, &kOutViewport3, &kOutReset1, &kOutReset2, &kOutMap1,
    &kOutMap2, &kOutMap3, &kOutMap4, &kFarPlane[0], &kFarPlane[1], &kFarPlane[2], &kFarPlane[3], &kFarPlane[4],
    &kFarPlane[5], &kSiteLanguageCall, &kSiteDifficultyNewGame, &kSiteDifficultyLoadClear,
    &kSiteDifficultyDialog1, &kSiteDifficultyDialog2, &kSiteFrameClockCall, &kSiteWheelRead, &kSiteGlyphIndexCall, &kSiteEffectStepA, &kSiteEffectStepB, &kSitePhysicsStepCall, &kSiteMapDrawCall, &kSiteTextDrawEntry, &kSiteSpriteDrawEntry, &kSiteSpotAccessor, &kSiteSpotRebuild[0], &kSiteSpotRebuild[1], &kSiteSpotRebuild[2], &kAltPhaseCalls[0], &kAltPhaseCalls[1], &kAltPhaseCalls[2], &kSiteResidencyEntry, &kSiteSetTargetEntry, &kSiteResourceOfSlot, &kSiteStateEntry, &kSiteSuitChange, &kSiteStrafeTail, &kSiteFullscreenArg[0], &kSiteFullscreenArg[1], &kSiteByteText, &kGaussCalls[0], &kGaussCalls[1], &kGaussCalls[2], &kGaussCalls[3], &kGaussCalls[4], &kGaussCalls[5], &kSiteCompositeDraw, &kSiteCameraCall, &kSiteExposure85, &kSiteExposure75, &kMapZoomFloor, &kMapZoomClampCall, &kMapZoomInit, &kMapPanX, &kMapPanZ, &kMapTileLookup, &kMapTileLimit, &kMapDeleteResource, &kMapReleaseResource, &kMapFindResource, &kMapHeading[0], &kMapHeading[1], &kMapHeading[2], &kMapHeading[3], &kMapHeading[4], &kMapHeading[5], &kMapHeading[6], &kMapArrow, &kSiteNavLookup, &kSiteNavNullCheck, &kSiteTrig1, &kSiteTrig2, &kSiteTrig3, &kSiteActivation, &kSiteLodFactor, &kSiteHighDetail1, &kSiteHighDetail2, &kSiteHighDetail3, &kSiteVisibility1, &kSiteVisibility2, &kSiteInputCommit, &kSiteInputPoll, &kSiteStickEval, &kSiteAimStick[0], &kSiteAimStick[1], &kSiteAimStick[2], &kSiteAimStick[3], &kSiteWetGate, &kSiteAimH, &kSiteAimV, &kSiteFullscreenArg[2], &kSiteVisibilityTestEntry,
    &kSiteCueMatrix3d, &kSiteCueMatrix2d, &kSiteX3dMatrixClear, &kSiteX3dDspSetup, &kSiteCueUpdateDsp,
    &kSiteArchiveRetry, &kSiteArchiveMissing, &kSiteLooseRetry, &kSiteLooseMissing, &kSiteInflateRoomFirst, &kSiteInflateRoomRefill,
    &kSiteArchiveInflateCall, &kSiteLooseInflateCall, &kSiteArchiveBufferSize, &kSiteLooseBufferSize,
    &kSiteAudioDataLoad, &kSiteAudioPoolOffset, &kSiteAudioPoolVtable,
    &kSiteMesData, &kSiteMesDataStore, &kSiteMesDrawEntry, &kSiteMesDrawNext, &kSiteGlyphPushEntry, &kSiteGlyphPushNext,
    &kNpcFacts[0], &kNpcFacts[1], &kNpcFacts[2], &kNpcFacts[3], &kNpcFacts[4], &kNpcFacts[5], &kNpcFacts[6], &kNpcFacts[7], &kNpcFacts[8], &kNpcFacts[9], &kNpcFacts[10], &kNpcFacts[11], &kNpcFacts[12], &kNpcFacts[13], &kNpcFacts[14], &kNpcFacts[15], &kNpcFacts[16], &kNpcFacts[17], &kNpcFacts[18], &kNpcFacts[19], &kNpcFacts[20], &kNpcFacts[21], &kNpcFacts[22], &kNpcFacts[23], &kNpcFacts[24], &kNpcFacts[25], &kNpcFacts[26], &kNpcFacts[27], &kNpcFacts[28], &kNpcFacts[29], &kNpcFacts[30], &kNpcFacts[31], &kNpcFacts[32], &kNpcFacts[33], &kNpcFacts[34], &kNpcFacts[35], &kNpcFacts[36], &kNpcFacts[37],
};

constexpr TailDef kAllTails[] = {
    {"aim limit (horizontal), all 30 bytes", kSiteAimH.va, (uint8_t)sizeof(kAimHFull), kAimHFull},
    {"map tile lookup bytes 16..19", kMapTileLookup.va + 16, (uint8_t)sizeof(kMapTileLookupTail), kMapTileLookupTail},
    {"effect step B bytes 16..23", kSiteEffectStepB.va + 16, (uint8_t)sizeof(kEffectStepBTail), kEffectStepBTail},
    {"cue matrix (3D voice) bytes 16..27", kSiteCueMatrix3d.va + 16, (uint8_t)sizeof(kCueMatrix3dTail), kCueMatrix3dTail},
    {"cue matrix (2D voice) bytes 16..27", kSiteCueMatrix2d.va + 16, (uint8_t)sizeof(kCueMatrix2dTail), kCueMatrix2dTail},
};

}  // namespace ufx
