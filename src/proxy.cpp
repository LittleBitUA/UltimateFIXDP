// The two functions DP.exe imports from X3DAudio1_7.dll, and what the mod does with the sound on the way through.
//
// In any other program both exports go straight to the real system DLL: the stubs keep the stack exactly as the caller left
// it, so any calling convention works. In DP.exe 1.01b (InstallAudioGuard) they pass through C functions of the cdecl shape
// the game calls them with (0072DC7D: five arguments, add esp,14h; 0072DB0B: three arguments, add esp,0Ch).
//
// 1. The guard, always on in the game. X3DAudioCalculate gets the game's static listener and emitter unchecked (FUN_0072CB00,
//    FUN_0072DB80): a position that is not a number would come back as a distance and a matrix that are not numbers, and the
//    game multiplies that distance into the volume of the cue. And the matrix buffer the game passes holds 16 floats
//    (2 sources x 8 speakers) and ends 8 bytes before the static CSdCore object: on a mix of more than 9 channels the
//    matrix would run over that object's vtable pointer.
//
// 2. Surround ([Audio] Surround), only when the mix has more than two channels. The game positions a 3D cue once, when it
//    starts (FUN_0072DCB0, the XACT3DApply of the SDK: the full X3DAudio matrix). Every tick after that FUN_0072CB00 gives
//    each cue cue->SetMatrixCoefficients(2, 2, ...): {s,0,0,s} for a 3D voice (s = volume x its own distance fade),
//    {v,0,0,v} for a 2D one. That has the shape of a stereo mix only. On a 5.1 / 7.1 mix XACT is expected to refuse it
//    (not checked on such a device; players of the game report a looping brake sound that stays on there), and then a voice
//    keeps the place and the volume it started with. The Xbox 360 code in the same place (Function_82555F10) sends the
//    whole X3DAudio matrix times the volume for a 3D voice, and for a 2D one a stereo pan into six channels with the rear
//    pair at 0.8, or one channel per speaker for a six-channel wave. The two helpers below do that from the two call
//    sites. The sites are rewritten only when the mix has more than two channels and the setting is on; switched off
//    afterwards, the helpers make the game's own call with the game's own matrix.
//
//    Left and right: the game draws its world through a left-handed view matrix and a projection whose X scale is negated
//    (FUN_006B62E0 multiplies it by -1.0 when its second stack argument is set, and the frame loop sets it: 00401BC2
//    push 1), so what X3DAudio, left-handed, takes for the right side is the left side of the picture. The Xbox code negates
//    the emitter's X (Function_82566D60). Here the X of every listener and emitter vector is negated in copies while
//    surround is live, which mirrors the whole scene and so swaps the sides; Surround=2 leaves them as the game has them.
//
// Nothing here has run in the game yet: the helpers log the first matrix XACT accepts or refuses of each kind.
#include "base.h"
#include "sites.h"
#include "stubs.h"
#include "surround.h"

extern "C" void* __cdecl UfxResolve(int index) {
    static void* cache[2];
    static HMODULE real;
    if (cache[index]) return cache[index];
    if (!real) {
        // Several mods can sit in a chain under the same DLL name: a copy named X3DAudio1_7.next.dll in the game
        // folder is loaded instead of the system DLL, and is expected to forward the calls in its turn.
        wchar_t path[MAX_PATH];
        DWORD m = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (m > 0 && m < MAX_PATH) {
            wchar_t* slash = nullptr;
            for (wchar_t* p = path; *p; ++p)
                if (*p == L'\\') slash = p;
            if (slash && (slash + 1 - path) + 24 < MAX_PATH) {
                lstrcpyW(slash + 1, L"X3DAudio1_7.next.dll");
                if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) real = LoadLibraryW(path);
            }
        }
    }
    if (!real) {
        wchar_t path[MAX_PATH];
        UINT n = GetSystemDirectoryW(path, MAX_PATH);  // SysWOW64 for a 32-bit process
        if (n > 0 && n < MAX_PATH - 24) {
            lstrcatW(path, L"\\X3DAudio1_7.dll");
            real = LoadLibraryW(path);
        }
    }
    static const char* const names[2] = {"X3DAudioCalculate", "X3DAudioInitialize"};
    void* fn = real ? (void*)GetProcAddress(real, names[index]) : nullptr;
    if (!fn) {
        MessageBoxW(nullptr,
                    L"Не знайдено системний X3DAudio1_7.dll. Встановіть DirectX (червень 2010).\n"
                    L"The system X3DAudio1_7.dll was not found. Install the DirectX June 2010 runtime.",
                    L"UltimateFIXDP", MB_OK | MB_ICONERROR);
        ExitProcess(1);
    }
    cache[index] = fn;
    return fn;
}

extern "C" {
unsigned char UfxAudioHost = 0;  // 1 in DP.exe 1.01b once the guard is installed: the two stubs at the end then take the C path
}

namespace ufx {
namespace {

// X3DAUDIO_DSP_SETTINGS of X3DAudio 1.7. The game's copy is at 014B02F0; FUN_0072DCB0 reads +0, +8, +0Ch, +1Ch, +20h, +24h.
struct X3dDsp {
    float* matrix;      // +00h pMatrixCoefficients: the level from source S to destination D is at [sources * D + S]
    float* delays;      // +04h
    u32 sources;        // +08h SrcChannelCount (the game: 2)
    u32 destinations;   // +0Ch DstChannelCount (the game: channels of the final mix)
    float lpfDirect;    // +10h
    float lpfReverb;    // +14h
    float reverb;       // +18h
    float doppler;      // +1Ch DopplerFactor
    float angle;        // +20h EmitterToListenerAngle
    float distance;     // +24h EmitterToListenerDistance: the only result the per-tick update of the game reads
    float emitterSpeed; // +28h
    float listenerSpeed;// +2Ch
};
static_assert(sizeof(X3dDsp) == 0x30, "X3DAUDIO_DSP_SETTINGS is 30h bytes");

// X3DAUDIO_LISTENER (34h bytes, the game's at 014B0320): OrientFront +00h, OrientTop +0Ch, Position +18h, Velocity +24h, pCone.
// X3DAUDIO_EMITTER (64h bytes, the game's at 014B0354): pCone, then the same four vectors from +04h, ..., ChannelCount +3Ch.
constexpr u32 kListenerSize = 0x34;
constexpr u32 kEmitterSize = 0x64;
constexpr u32 kEmitterVectors = 0x04;
constexpr u32 kEmitterChannels = 0x3C;
constexpr u32 kVectorFloats = 12;  // four vectors of three floats

constexpr u32 kCalcMatrix = 0x1, kCalcDoppler = 0x20, kCalcEmitterAngle = 0x40;  // X3DAUDIO_CALCULATE_* (the game passes 61h)
constexpr float kFarAway = 1.0e9f;  // a distance beyond any range of the game: its fade (0072CC55..0072CC9B) clamps to silence
constexpr u32 kWideFloats = 128;    // 2 sources x 64 speakers, the most XAudio2 mixes

typedef void(__cdecl* CalculateFn)(const void* instance, const void* listener, const void* emitter, u32 flags, void* dsp);
typedef void(__cdecl* InitializeFn)(u32 speakerMask, float speedOfSound, void* instance);
// IXACT3Cue::SetMatrixCoefficients, vtable +10h (the game: 0072CCE5 mov eax,[edx+10h] / call eax, the callee pops 16 bytes).
typedef HRESULT(__stdcall* SetMatrixFn)(void* cue, u32 sources, u32 destinations, const float* matrix);

SpeakerLayout g_layout;      // from the speaker mask the game gave X3DAudioInitialize: the dwChannelMask of the final mix
u32 g_mixChannels = 0;       // channels of the final mix as the game stored them in its DSP settings
bool g_mixKnown = false;     // the game has called X3DAudioInitialize
bool g_sitesPatched = false;
bool g_wine = false;
bool g_sixSources = false;   // a six-channel wave gets its own matrix (the layout has the speakers and XACT can hold it)
u8 g_goodListener[kListenerSize];   // the last listener that had finite numbers
bool g_haveGoodListener = false;
float g_wide[kWideFloats];          // where a matrix too big for the game's buffer is calculated

// One log line per kind of event: these run for every voice every tick.
bool g_saidListener = false, g_saidEmitter = false, g_saidWide = false, g_saidTooWide = false;
bool g_said3d = false, g_said3dMono = false, g_said3dRejected = false;
bool g_said2d = false, g_said2dRejected = false, g_saidSix = false, g_saidSixRejected = false;

u32 Word(const void* base, u32 offset) { return *(const volatile u32*)((const u8*)base + offset); }

u32 Bits(float value) {
    union {
        float f;
        u32 u;
    } x;
    x.f = value;
    return x.u;
}

bool Finite(u32 bits) { return (bits & 0x7F800000u) != 0x7F800000u; }

bool AllFinite(const void* base, u32 offset, u32 floats) {
    for (u32 i = 0; i < floats; ++i)
        if (!Finite(Word(base, offset + i * 4))) return false;
    return true;
}

void CopyBytes(u8* to, const void* from, u32 size) {
    const u8* f = (const u8*)from;
    for (u32 i = 0; i < size; ++i) to[i] = f[i];
}

// Negates the X of the four vectors that start at `vectors` (the sign bit of every third float).
void MirrorX(u8* vectors) {
    for (u32 i = 0; i < kVectorFloats; i += 3) vectors[i * 4 + 3] ^= 0x80;
}

X3dDsp* GameDsp() { return (X3dDsp*)(uintptr_t)kX3dDspVa; }

// Surround acts now: the setting is on, the mix is wider than stereo with a layout we know, and both call sites are ours.
bool SurroundLive() {
    return g_sitesPatched && g_cfg.surround && g_layout.known && GameDsp()->destinations == g_mixChannels;
}

// What the game gets instead of a calculation that cannot be made: no level on any speaker and a distance that fades the
// voice out. The game's buffer holds kX3dMatrixFloats floats, so never more than that is written.
void SilentResult(X3dDsp* dsp, u32 flags) {
    if ((flags & kCalcMatrix) && dsp->matrix) {
        u32 n = dsp->sources <= 64 && dsp->destinations <= 64 ? dsp->sources * dsp->destinations : kX3dMatrixFloats;
        if (n > kX3dMatrixFloats) n = kX3dMatrixFloats;
        for (u32 i = 0; i < n; ++i) dsp->matrix[i] = 0.0f;
    }
    if (flags & kCalcDoppler) dsp->doppler = 1.0f;
    if (flags & kCalcEmitterAngle) dsp->angle = 0.0f;
    dsp->distance = kFarAway;
}

HRESULT SetMatrix(void* cue, u32 sources, u32 destinations, const float* matrix) {
    return (*(SetMatrixFn**)cue)[4](cue, sources, destinations, matrix);
}

// Replaces cue->SetMatrixCoefficients(2, 2, {s,0,0,s}) of a 3D voice (0072CCCE; BuildCueMatrixCall in stubs.h). The game has
// just run X3DAudioCalculate for this voice (0072CC19), so its DSP settings hold the matrix of this emitter: 2 sources (the
// emitter the game sets up has two channels) by the channels of the mix, the same shape the game itself applies when the
// cue starts. Every coefficient is scaled by s, as on the Xbox. XACT is asked with that shape first, then with one source
// (the mean of the two columns) in case it wants the channel count of a mono wave, and if it refuses both the game's own
// call is made, so the result is never worse than without the mod.
void __stdcall Cue3dMatrix(void* cue, const float* params, const float* game2x2) {
    (void)params;
    if (!cue) return;  // the game checks this before the site
    const X3dDsp* dsp = GameDsp();
    if (SurroundLive() && dsp->sources == 2 && dsp->matrix == (float*)(uintptr_t)kX3dMatrixVa) {
        u32 dst = dsp->destinations;  // 3..8 (SurroundLive), so at most 16 coefficients: what the game's buffer holds
        float s = Finite(Bits(game2x2[0])) ? game2x2[0] : 0.0f;  // a NaN volume must not reach the mixer
        float full[2 * kSurroundMaxSpeakers], mono[kSurroundMaxSpeakers];
        ScaleVoiceMatrix(dsp->matrix, dst, s, full, mono);
        HRESULT two = SetMatrix(cue, 2, dst, full);
        if (two >= 0) {
            if (!g_said3d) {
                g_said3d = true;
                Log("audio: first 3D voice updated with a 2x%u matrix, accepted by XACT", dst);
            }
            return;
        }
        HRESULT one = SetMatrix(cue, 1, dst, mono);
        if (one >= 0) {
            if (!g_said3dMono) {
                g_said3dMono = true;
                Log("audio: XACT refused a 2x%u matrix for a 3D voice (%08X) and took a 1x%u one", dst, (u32)two, dst);
            }
            return;
        }
        if (!g_said3dRejected) {
            g_said3dRejected = true;
            Log("audio: XACT refused both a 2x%u (%08X) and a 1x%u (%08X) matrix for a 3D voice: the game's own 2x2 call is made for it",
                dst, (u32)two, dst, (u32)one);
        }
    }
    SetMatrix(cue, 2, 2, game2x2);
}

// Replaces cue->SetMatrixCoefficients(2, 2, {v,0,0,v}) of a 2D voice (0072CD57). params is the voice parameter block the
// game passes to FUN_0072CB00, ten dwords FUN_007203A0 copied into the voice: the pan at +20h and the kind of wave at +24h.
// Every 2D play path fills both: FUN_0046AC40 takes the kind from a byte of the sound table that reads like a channel
// count (2 gives 1, 6 gives 2, anything else 0), the music player FUN_0046BF70 always stores 2, the scene music 1. As the
// Xbox code does for the same two fields:
//   kinds 0 and 1: two sources, left = |1 - pan| / 2, right = |-1 - pan| / 2, each to its front speaker at 1.0 and to its
//                  rear speakers at 0.8 (centre and subwoofer silent);
//   kinds 2 and 3: six sources, one per speaker. If XACT refuses that, the stereo pan is tried: the refusal most likely
//                  means the wave of the PC sound bank is not a six-channel one (a guess, the banks were not examined).
// A kind the Xbox code leaves alone, a layout without the speakers a 5.1 wave needs, or a refusal of the pan matrix end in
// the game's own call.
void __stdcall Cue2dMatrix(void* cue, const float* params, const float* game2x2) {
    if (!cue) return;
    if (SurroundLive()) {
        u32 dst = g_mixChannels;  // 3..8
        float v = Finite(Bits(game2x2[0])) ? game2x2[0] : 0.0f;
        int kind = (int)Word(params, 0x24);
        float m[6 * kSurroundMaxSpeakers];
        if ((kind == 2 || kind == 3) && g_sixSources) {
            BuildSixMatrix(g_layout, v, m);
            HRESULT hr = SetMatrix(cue, 6, dst, m);
            if (hr >= 0) {
                if (!g_saidSix) {
                    g_saidSix = true;
                    Log("audio: first six-channel sound (music) updated with a 6x%u matrix, accepted by XACT", dst);
                }
                return;
            }
            if (!g_saidSixRejected) {
                g_saidSixRejected = true;
                Log("audio: XACT refused a 6x%u matrix for a six-channel sound (%08X): the stereo pan is tried for such sounds", dst, (u32)hr);
            }
            kind = 1;
        }
        if (kind == 0 || kind == 1) {
            BuildPanMatrix(g_layout, Finite(Word(params, 0x20)) ? params[8] : 0.0f, v, m);
            HRESULT hr = SetMatrix(cue, 2, dst, m);
            if (hr >= 0) {
                if (!g_said2d) {
                    g_said2d = true;
                    Log("audio: first 2D sound updated with a 2x%u pan matrix, accepted by XACT", dst);
                }
                return;
            }
            if (!g_said2dRejected) {
                g_said2dRejected = true;
                Log("audio: XACT refused a 2x%u pan matrix for a 2D sound (%08X): the game's own 2x2 call is made for it", dst, (u32)hr);
            }
        }
    }
    SetMatrix(cue, 2, 2, game2x2);
}

bool PatchCueSites() {
    const SiteDef& a = kSiteCueMatrix3d;
    const SiteDef& b = kSiteCueMatrix2d;
    if (!BytesMatch(a.va, a.bytes, a.len) || !BytesMatch(a.va + 16, kCueMatrix3dTail, sizeof(kCueMatrix3dTail)) ||
        !BytesMatch(b.va, b.bytes, b.len) || !BytesMatch(b.va + 16, kCueMatrix2dTail, sizeof(kCueMatrix2dTail)) ||
        !BytesMatch(kSiteCueUpdateDsp.va, kSiteCueUpdateDsp.bytes, kSiteCueUpdateDsp.len)) {
        Log("audio: surround: the cue update at %08X / %08X differs from DP.exe 1.01b, left off", a.va, b.va);
        return false;
    }
    u8 code3d[kCueMatrixLen], code2d[kCueMatrixLen];
    BuildCueMatrixCall(code3d, a.va, (u32)(uintptr_t)Cue3dMatrix, kCueMatrix3dDisp, kCueMatrixLen);
    BuildCueMatrixCall(code2d, b.va, (u32)(uintptr_t)Cue2dMatrix, kCueMatrix2dDisp, kCueMatrixLen);
    // Each site works alone (a helper that is not live makes the game's own call), so a failure of the second write leaves
    // nothing half done; surround only goes live with both.
    if (!WriteCode(a.va, code3d, kCueMatrixLen) || !WriteCode(b.va, code2d, kCueMatrixLen)) {
        Log("audio: surround: could not write the cue update sites, left off");
        return false;
    }
    return true;
}

// After the game's X3DAudioInitialize. The CSdCore constructor FUN_0072BA00 runs twice (for the static object at 014B0400
// and for the one the getter FUN_0046F9D0 allocates), so this comes twice with the same mask: the second time says nothing.
void OnInitialize(u32 mask) {
    u32 mix = GameDsp()->destinations;  // stored at 0072BEDF, before the game calls X3DAudioInitialize
    if (g_mixKnown && g_layout.mask == mask && g_mixChannels == mix) return;
    g_layout = ReadSpeakerLayout(mask, mix);
    g_mixChannels = mix;
    g_mixKnown = true;
    // Wine's XACT (FAudio) keeps the matrix of a cue in a 2 x 8 array (as its source reads; not checked here): six sources
    // would not fit, so there a six-channel sound keeps the game's own call.
    g_sixSources = g_layout.six && !g_wine;
    Log("audio: the final mix has %u channels, speaker mask %X (%u speakers)", mix, mask, g_layout.speakers);
    ApplySurround();
}

}  // namespace

// Brings the surround fix in line with the setting: at the game's X3DAudioInitialize and after a change in the menu (both on
// the main thread, which is also the only one that runs the cue update being patched). The call sites are written the
// first time surround is wanted on a mix of more than two channels and stay; after that the helpers follow the setting.
void ApplySurround() {
    if (!g_mixKnown) return;
    if (g_mixChannels <= 2) {
        Log("audio: stereo mix: surround does nothing, the game's sound code is untouched");
        return;
    }
    if (!g_layout.known) {
        Log("audio: surround left off: it needs 3 to 8 speakers with a front pair and one speaker per channel of the mix");
        return;
    }
    if (!g_cfg.surround) {
        Log(g_sitesPatched ? "audio: Surround=0: the cue updates make the game's own 2x2 calls again"
                           : "audio: Surround=0: the game's sound code is untouched");
        return;
    }
    if (g_cfg.safeMode) {
        Log("audio: SafeMode: surround left off");
        return;
    }
    if (!g_sitesPatched) g_sitesPatched = PatchCueSites();
    if (!g_sitesPatched) return;
    Log("audio: surround on for %u channels: every tick a 3D voice gets the full X3DAudio matrix (left and right %s) "
        "and a 2D sound the Xbox stereo pan%s",
        g_mixChannels, g_cfg.surround == 2 ? "as the game computes them" : "mirrored to match the picture",
        g_sixSources ? ", a six-channel sound one channel per speaker" : "; six-channel sounds keep the game's call");
}

// In DP.exe 1.01b only: from now on the two exports go through UfxCalculate / UfxInitialize. The two compared sites prove
// the address and the size of the game's matrix buffer (sites.h); nothing is written.
bool InstallAudioGuard() {
    const SiteDef& clear = kSiteX3dMatrixClear;
    const SiteDef& setup = kSiteX3dDspSetup;
    if (!BytesMatch(clear.va, clear.bytes, clear.len) || !BytesMatch(setup.va, setup.bytes, setup.len)) {
        Log("audio: the X3DAudio setup at %08X / %08X differs from DP.exe 1.01b: calls are forwarded without the guard", clear.va, setup.va);
        return false;
    }
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    g_wine = ntdll && GetProcAddress(ntdll, "wine_get_version") != nullptr;
    UfxAudioHost = 1;
    Log("audio: guard on X3DAudioCalculate (non-finite listener or emitter, a matrix larger than the game's %u-float buffer)%s",
        kX3dMatrixFloats, g_wine ? "; running under Wine" : "");
    return true;
}

}  // namespace ufx

// X3DAudioCalculate as DP.exe calls it (FUN_0072DB80, 0072DC7D): cdecl, five arguments, nothing returned.
extern "C" __attribute__((used)) void __cdecl UfxCalculate(const void* instance, const void* listener, const void* emitter,
                                                           ufx::u32 flags, void* dspArg) {
    using namespace ufx;
    CalculateFn real = (CalculateFn)UfxResolve(0);
    X3dDsp* dsp = (X3dDsp*)dspArg;
    if (!listener || !emitter || !dsp) {  // the game never does this: FUN_0072DB80 returns E_POINTER before the call
        real(instance, listener, emitter, flags, dspArg);
        return;
    }

    // A listener that is not a number: the last good one stands in, so the voices keep their places for that tick.
    if (AllFinite(listener, 0, kVectorFloats)) {
        CopyBytes(g_goodListener, listener, kListenerSize);
        g_haveGoodListener = true;
    } else {
        if (!g_saidListener) {
            g_saidListener = true;
            Log("audio: guard: the listener given to X3DAudioCalculate is not finite, the last good one is used (logged once)");
        }
        if (!g_haveGoodListener) {
            SilentResult(dsp, flags);
            return;
        }
        listener = g_goodListener;
    }
    // An emitter that is not a number has no place: the voice is silent until it has one.
    if (!AllFinite(emitter, kEmitterVectors, kVectorFloats)) {
        if (!g_saidEmitter) {
            g_saidEmitter = true;
            Log("audio: guard: an emitter given to X3DAudioCalculate is not finite, that voice is muted meanwhile (logged once)");
        }
        SilentResult(dsp, flags);
        return;
    }

    // X3DAudio writes (emitter channels) x (speakers of its mask) coefficients, whatever the DSP settings say.
    u32 sources = Word(emitter, kEmitterChannels), speakers = g_layout.speakers;
    if (sources < dsp->sources) sources = dsp->sources;
    if (speakers < dsp->destinations) speakers = dsp->destinations;
    float* gameMatrix = dsp->matrix;
    bool wide = false;
    if ((flags & kCalcMatrix) && gameMatrix == (float*)(uintptr_t)kX3dMatrixVa) {
        if (sources > 64 || speakers > 64 || sources * speakers > kWideFloats) {
            if (!g_saidTooWide) {
                g_saidTooWide = true;
                Log("audio: guard: a %ux%u matrix cannot be calculated safely, 3D voices are muted (logged once)", sources, speakers);
            }
            SilentResult(dsp, flags);
            return;
        }
        wide = sources * speakers > kX3dMatrixFloats;
    }

    u8 mirroredListener[kListenerSize], mirroredEmitter[kEmitterSize];
    if (g_cfg.surround == 1 && dsp == GameDsp() && SurroundLive()) {
        CopyBytes(mirroredListener, listener, kListenerSize);
        CopyBytes(mirroredEmitter, emitter, kEmitterSize);
        MirrorX(mirroredListener);
        MirrorX(mirroredEmitter + kEmitterVectors);
        listener = mirroredListener;
        emitter = mirroredEmitter;
    }

    if (!wide) {
        real(instance, listener, emitter, flags, dsp);
        return;
    }
    // More coefficients than the game's buffer holds: calculate into ours and hand the game the part that fits (the first
    // 8 speakers). When a cue starts, the game still reads the full count from its own buffer for XACT; that reads past the
    // buffer but writes nothing.
    if (!g_saidWide) {
        g_saidWide = true;
        Log("audio: guard: a %ux%u matrix does not fit the game's %u-float buffer, it is calculated outside it (logged once)",
            sources, speakers, kX3dMatrixFloats);
    }
    for (u32 i = 0; i < kWideFloats; ++i) g_wide[i] = 0.0f;
    X3dDsp local = *dsp;
    local.matrix = g_wide;
    real(instance, listener, emitter, flags, &local);
    local.matrix = gameMatrix;
    *dsp = local;
    for (u32 i = 0; i < kX3dMatrixFloats; ++i) gameMatrix[i] = g_wide[i];
}

// X3DAudioInitialize as DP.exe calls it (FUN_0072DA80, 0072DB0B): cdecl, three arguments; the game ignores eax (the
// function of X3DAudio 1.7 returns nothing).
extern "C" __attribute__((used)) void __cdecl UfxInitialize(ufx::u32 speakerMask, float speedOfSound, void* instance) {
    ((ufx::InitializeFn)UfxResolve(1))(speakerMask, speedOfSound, instance);
    ufx::OnInitialize(speakerMask);
}

extern "C" __attribute__((naked)) void ProxyCalculate(void) {
    __asm__ volatile(
        "cmpb $0, _UfxAudioHost\n\t"
        "jne _UfxCalculate\n\t"
        "push $0\n\t"
        "call _UfxResolve\n\t"
        "add $4, %esp\n\t"
        "jmp *%eax\n\t");
}

extern "C" __attribute__((naked)) void ProxyInitialize(void) {
    __asm__ volatile(
        "cmpb $0, _UfxAudioHost\n\t"
        "jne _UfxInitialize\n\t"
        "push $1\n\t"
        "call _UfxResolve\n\t"
        "add $4, %esp\n\t"
        "jmp *%eax\n\t");
}
