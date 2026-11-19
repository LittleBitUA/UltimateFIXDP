// Machine-code stubs, built by plain functions so that the host can disassemble and test them.
// No Windows headers here.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace ufx {

inline void StubPut32(uint8_t*& p, uint32_t v) {
    *p++ = (uint8_t)v;
    *p++ = (uint8_t)(v >> 8);
    *p++ = (uint8_t)(v >> 16);
    *p++ = (uint8_t)(v >> 24);
}

inline void StubJmp(uint8_t*& p, const uint8_t* base, uint32_t baseVa, uint32_t targetVa) {
    uint32_t nextVa = baseVa + (uint32_t)(p - base) + 5;
    *p++ = 0xE9;
    StubPut32(p, targetVa - nextVa);
}

// Replacement for the three instructions at 0058CB09:
//     fld  dword [esp+10h]          ; distance
//     fdiv dword [frameDelta]
//     fstp dword [esi+4E4h]         ; speed
// When frameDelta is +0 or -0 the division is skipped (distance / 1.0). Registers and flags are
// preserved exactly as the original sequence leaves them.
inline size_t BuildZeroDeltaStub(uint8_t* out, uint32_t stubVa, uint32_t frameDeltaVa, uint32_t returnVa) {
    uint8_t* p = out;
    *p++ = 0x9C;                            // pushfd
    *p++ = 0x50;                            // push eax
    *p++ = 0xA1; StubPut32(p, frameDeltaVa);  // mov eax, [frameDelta]
    *p++ = 0x01; *p++ = 0xC0;               // add eax, eax        ZF=1 for +0 and -0
    *p++ = 0x74; *p++ = 0x17;               // jz zero_case        (skips the 23 bytes below)
    *p++ = 0x58;                            // pop eax
    *p++ = 0x9D;                            // popfd
    *p++ = 0xD9; *p++ = 0x44; *p++ = 0x24; *p++ = 0x10;     // fld  dword [esp+10h]
    *p++ = 0xD8; *p++ = 0x35; StubPut32(p, frameDeltaVa);   // fdiv dword [frameDelta]
    *p++ = 0xD9; *p++ = 0x9E; StubPut32(p, 0x4E4);          // fstp dword [esi+4E4h]
    StubJmp(p, out, stubVa, returnVa);
    *p++ = 0x58;                            // zero_case: pop eax
    *p++ = 0x9D;                            // popfd
    *p++ = 0xD9; *p++ = 0x44; *p++ = 0x24; *p++ = 0x10;     // fld  dword [esp+10h]
    *p++ = 0xD9; *p++ = 0x9E; StubPut32(p, 0x4E4);          // fstp dword [esi+4E4h]
    StubJmp(p, out, stubVa, returnVa);
    return (size_t)(p - out);
}

// Wrapper for a call to `originalVa` that makes the game's logical resolution (two float globals) bigger
// for the duration of the call and puts 1280x720 back afterwards. It touches no register and no flag, and
// the stack is exactly what the original callee expects, so it works with any calling convention:
//     pop  [retSave]        ; take the return address off the stack
//     push [widthBits] / pop [globalW]        (and the same for the height)
//     push after / jmp original               ; the callee returns to `after` with the same arguments
//   after: mov [globalW], 1280.0f / mov [globalH], 720.0f
//     jmp  [retSave]        ; back to the caller
inline size_t BuildScaleThunk(uint8_t* out, uint32_t thunkVa, uint32_t originalVa, uint32_t retSaveVa,
                              uint32_t widthBitsVa, uint32_t heightBitsVa, uint32_t globalW, uint32_t globalH) {
    uint8_t* p = out;
    *p++ = 0x8F; *p++ = 0x05; StubPut32(p, retSaveVa);      // pop dword [retSave]
    *p++ = 0xFF; *p++ = 0x35; StubPut32(p, widthBitsVa);    // push dword [widthBits]
    *p++ = 0x8F; *p++ = 0x05; StubPut32(p, globalW);        // pop dword [globalW]
    *p++ = 0xFF; *p++ = 0x35; StubPut32(p, heightBitsVa);   // push dword [heightBits]
    *p++ = 0x8F; *p++ = 0x05; StubPut32(p, globalH);        // pop dword [globalH]
    uint8_t* pushAfter = p;
    *p++ = 0x68; StubPut32(p, 0);                            // push after   (patched below)
    StubJmp(p, out, thunkVa, originalVa);                    // jmp original
    uint32_t afterVa = thunkVa + (uint32_t)(p - out);
    uint8_t* imm = pushAfter + 1;
    imm[0] = (uint8_t)afterVa; imm[1] = (uint8_t)(afterVa >> 8); imm[2] = (uint8_t)(afterVa >> 16);
    imm[3] = (uint8_t)(afterVa >> 24);
    *p++ = 0xC7; *p++ = 0x05; StubPut32(p, globalW); StubPut32(p, 0x44A00000);   // mov [globalW], 1280.0f
    *p++ = 0xC7; *p++ = 0x05; StubPut32(p, globalH); StubPut32(p, 0x44340000);   // mov [globalH], 720.0f
    *p++ = 0xFF; *p++ = 0x25; StubPut32(p, retSaveVa);      // jmp dword [retSave]
    return (size_t)(p - out);
}

// Replaces the 15 bytes that copy the device parameters (14 dwords) into the block Reset() uses. In windowed
// mode it first stores the output size as BackBufferWidth/Height (the parameters are built at [ebp-0x44]).
//     cmp dword [fullscreen], 0 / jne skip / mov [ebp-44h], W / mov [ebp-40h], H
//   skip: mov ecx, 0Eh / lea esi,[ebp-44h] / mov edi, 0148BB70h / rep movsd / jmp back
inline size_t BuildBackbufferThunk(uint8_t* out, uint32_t thunkVa, uint32_t fullscreenFlagVa, uint32_t width,
                                   uint32_t height, uint32_t copyDstVa, uint32_t returnVa) {
    uint8_t* p = out;
    *p++ = 0x83; *p++ = 0x3D; StubPut32(p, fullscreenFlagVa); *p++ = 0x00;  // cmp dword [flag], 0
    *p++ = 0x75; *p++ = 0x0E;                                                // jne skip (14 bytes)
    *p++ = 0xC7; *p++ = 0x45; *p++ = 0xBC; StubPut32(p, width);              // mov dword [ebp-44h], width
    *p++ = 0xC7; *p++ = 0x45; *p++ = 0xC0; StubPut32(p, height);            // mov dword [ebp-40h], height
    *p++ = 0xB9; StubPut32(p, 0x0E);                                         // mov ecx, 0Eh
    *p++ = 0x8D; *p++ = 0x75; *p++ = 0xBC;                                   // lea esi, [ebp-44h]
    *p++ = 0xBF; StubPut32(p, copyDstVa);                                    // mov edi, 0148BB70h
    *p++ = 0xF3; *p++ = 0xA5;                                                // rep movsd
    StubJmp(p, out, thunkVa, returnVa);                                      // jmp back
    return (size_t)(p - out);
}

// Replacement for `mov [0BD7798h], esi` (6 bytes) that also runs a C function (the 2D state) without disturbing
// any register or flag the following code may rely on:
//     mov [slot], esi / pushad / pushfd / call fn / popfd / popad / ret
inline size_t BuildHudStub(uint8_t* out, uint32_t stubVa, uint32_t slotVa, uint32_t fnVa) {
    uint8_t* p = out;
    *p++ = 0x89; *p++ = 0x35; StubPut32(p, slotVa);   // mov dword [slot], esi
    *p++ = 0x60;                                      // pushad
    *p++ = 0x9C;                                      // pushfd
    uint32_t nextVa = stubVa + (uint32_t)(p - out) + 5;
    *p++ = 0xE8; StubPut32(p, fnVa - nextVa);         // call fn
    *p++ = 0x9D;                                      // popfd
    *p++ = 0x61;                                      // popad
    *p++ = 0xC3;                                      // ret
    return (size_t)(p - out);
}


// Stores the chosen difficulty into the game manager (eax) instead of `mov [eax+8C60Bh], bl`. Registers and
// flags are as the original leaves them (the original does not touch flags either).
//     push ecx / mov cl,[selVa] / mov [eax+offset],cl / pop ecx / ret
inline size_t BuildDifficultyStore(uint8_t* out, uint32_t selVa, uint32_t offset) {
    uint8_t* p = out;
    *p++ = 0x51;                                       // push ecx
    *p++ = 0x8A; *p++ = 0x0D; StubPut32(p, selVa);     // mov cl, [sel]
    *p++ = 0x88; *p++ = 0x88; StubPut32(p, offset);    // mov [eax+offset], cl
    *p++ = 0x59;                                       // pop ecx
    *p++ = 0xC3;                                       // ret
    return (size_t)(p - out);
}

// Title screen: show the difficulty selection (state 3) with the Normal entry highlighted, then continue where
// the original instruction sequence would have gone.
//     mov dword [state], 3 / mov dword [cursor], 1 / jmp returnVa
inline size_t BuildTitleDialogStub(uint8_t* out, uint32_t stubVa, uint32_t stateVa, uint32_t cursorVa,
                                   uint32_t returnVa) {
    uint8_t* p = out;
    *p++ = 0xC7; *p++ = 0x05; StubPut32(p, stateVa);  StubPut32(p, 3);
    *p++ = 0xC7; *p++ = 0x05; StubPut32(p, cursorVa); StubPut32(p, 1);
    StubJmp(p, out, stubVa, returnVa);
    return (size_t)(p - out);
}

// Aim limit, horizontal (replaces 30 bytes at 0053C18C). Entry: st0 = -hi, st1 = hi, st2 = C; the original asks
// "is angle == -hi or == hi" with two exact fucompp and leaves st0 = C. We ask "|angle - (+-hi)| <= eps",
// treating NaN as "not equal" like the original does. Flags and AX are not used after the exits.
//   test: fld [esi+0A0h] / fsub st,st(k) / fabs / fcomp qword [eps] / fnstsw ax / sahf / jp next / jbe equal
inline size_t BuildAimHorizontal(uint8_t* out, uint32_t stubVa, uint32_t epsVa, uint32_t notEqualVa, uint32_t equalVa) {
    uint8_t* p = out;
    for (int k = 1; k <= 2; ++k) {
        *p++ = 0xD9; *p++ = 0x86; StubPut32(p, 0xA0);   // fld dword [esi+0A0h]
        *p++ = 0xD8; *p++ = (uint8_t)(0xE0 + k);        // fsub st, st(k)
        *p++ = 0xD9; *p++ = 0xE1;                       // fabs
        *p++ = 0xDC; *p++ = 0x1D; StubPut32(p, epsVa);  // fcomp qword [eps]
        *p++ = 0xDF; *p++ = 0xE0;                       // fnstsw ax
        *p++ = 0x9E;                                    // sahf (CF=C0, PF=C2, ZF=C3)
        *p++ = 0x7A; *p++ = 0x02;                       // jp next (unordered: not equal)
        *p++ = 0x76; *p++ = (k == 1) ? 0x20 : 0x09;     // jbe equal (|diff| <= eps)
    }
    *p++ = 0xDD; *p++ = 0xD8;                           // fstp st(0)
    *p++ = 0xDD; *p++ = 0xD8;                           // fstp st(0)
    StubJmp(p, out, stubVa, notEqualVa);                // not equal: st0 = C
    *p++ = 0xDD; *p++ = 0xD8;                           // equal: fstp st(0)
    *p++ = 0xDD; *p++ = 0xD8;                           // fstp st(0)
    StubJmp(p, out, stubVa, equalVa);                   // st0 = C
    return (size_t)(p - out);
}

// Aim limit, vertical (replaces 8 bytes at 0053C3B6: fld [esp+18h] / fmul st,st(2) / fucompp). Entry st0 = |p|,
// st1 = C. Leaves st0 = C and AH = 40h (equal) or 0 (not equal) for the original `test ah,44h / jp`.
inline size_t BuildAimVertical(uint8_t* out, uint32_t stubVa, uint32_t epsVa, uint32_t exitVa) {
    uint8_t* p = out;
    *p++ = 0xD9; *p++ = 0x44; *p++ = 0x24; *p++ = 0x18;  // fld dword [esp+18h]
    *p++ = 0xD8; *p++ = 0xCA;                            // fmul st, st(2)
    *p++ = 0xDE; *p++ = 0xE9;                            // fsubp st(1), st      (st1 = |p| - lim*C, pop)
    *p++ = 0xD9; *p++ = 0xE1;                            // fabs
    *p++ = 0xDC; *p++ = 0x1D; StubPut32(p, epsVa);       // fcomp qword [eps]
    *p++ = 0xDF; *p++ = 0xE0;                            // fnstsw ax
    *p++ = 0x9E;                                         // sahf
    *p++ = 0xB4; *p++ = 0x00;                            // mov ah, 0          (flags kept)
    *p++ = 0x7A; *p++ = 0x04;                            // jp done            (unordered)
    *p++ = 0x77; *p++ = 0x02;                            // ja done            (|diff| > eps)
    *p++ = 0xB4; *p++ = 0x40;                            // mov ah, 40h        (equal)
    StubJmp(p, out, stubVa, exitVa);                     // done:
    return (size_t)(p - out);
}


// The relocated first 6 bytes of FUN_00709400 followed by a jump to the rest of the function: calling this stub
// is calling the original function (same registers and stack).
inline size_t BuildStickOriginal(uint8_t* out, uint32_t stubVa, uint32_t returnVa) {
    uint8_t* p = out;
    *p++ = 0x55;                                  // push ebp
    *p++ = 0x8B; *p++ = 0xEC;                     // mov ebp, esp
    *p++ = 0x83; *p++ = 0xEC; *p++ = 0x14;        // sub esp, 14h
    StubJmp(p, out, stubVa, returnVa);
    return (size_t)(p - out);
}

// Wrapper for a call of the stick getter (float returned in st0). When the controller mode and the Xbox profile
// are on, st0 gets the original console shaping of the right stick: |x| <= 0.25 -> 0, else (x -+ 0.25) * 4/3.
// Touches no register except through x87, and no flags that the caller uses after a call.
//     pop [retSave] / push after / jmp original
//   after: cmp byte [useJoy],0 / je done / cmp byte [profile],0 / je done
//          fld st0 / fabs / fld [quarter] / fcomip st,st1 / fstp st0 / jb outside
//          fstp st0 / fldz / jmp done
//   outside: fldz / fcomip st,st1 / jb positive / fadd [quarter] / jmp scale
//   positive: fsub [quarter]
//   scale: fmul [fourThirds]
//   done: jmp [retSave]
inline size_t BuildAimShapeThunk(uint8_t* out, uint32_t thunkVa, uint32_t originalVa, uint32_t retSaveVa,
                                 uint32_t useJoyVa, uint32_t profileVa, uint32_t quarterVa, uint32_t fourThirdsVa) {
    uint8_t* p = out;
    uint8_t* fix[8];
    int nfix = 0;
    auto rel8 = [&](int opcode) { *p++ = (uint8_t)opcode; fix[nfix++] = p; *p++ = 0; };
    auto bind = [&](int index) { fix[index][0] = (uint8_t)(p - (fix[index] + 1)); };
    *p++ = 0x8F; *p++ = 0x05; StubPut32(p, retSaveVa);           // pop dword [retSave]
    uint8_t* pushAfter = p;
    *p++ = 0x68; StubPut32(p, 0);                                 // push after (patched below)
    StubJmp(p, out, thunkVa, originalVa);                         // jmp original
    uint32_t afterVa = thunkVa + (uint32_t)(p - out);
    for (int i = 0; i < 4; ++i) pushAfter[1 + i] = (uint8_t)(afterVa >> (8 * i));
    *p++ = 0x80; *p++ = 0x3D; StubPut32(p, useJoyVa); *p++ = 0x00; // cmp byte [useJoy], 0
    rel8(0x74);                                                   // je done        (fix 0)
    *p++ = 0x80; *p++ = 0x3D; StubPut32(p, profileVa); *p++ = 0x00; // cmp byte [profile], 0
    rel8(0x74);                                                   // je done        (fix 1)
    *p++ = 0xD9; *p++ = 0xC0;                                     // fld st0
    *p++ = 0xD9; *p++ = 0xE1;                                     // fabs
    *p++ = 0xD9; *p++ = 0x05; StubPut32(p, quarterVa);            // fld dword [quarter]
    *p++ = 0xDF; *p++ = 0xF1;                                     // fcomip st, st1
    *p++ = 0xDD; *p++ = 0xD8;                                     // fstp st0
    rel8(0x72);                                                   // jb outside     (fix 2)
    *p++ = 0xDD; *p++ = 0xD8;                                     // fstp st0
    *p++ = 0xD9; *p++ = 0xEE;                                     // fldz
    rel8(0xEB);                                                   // jmp done       (fix 3)
    bind(2);                                                      // outside:
    *p++ = 0xD9; *p++ = 0xEE;                                     // fldz
    *p++ = 0xDF; *p++ = 0xF1;                                     // fcomip st, st1
    rel8(0x72);                                                   // jb positive    (fix 4)
    *p++ = 0xD8; *p++ = 0x05; StubPut32(p, quarterVa);            // fadd dword [quarter]
    rel8(0xEB);                                                   // jmp scale      (fix 5)
    bind(4);                                                      // positive:
    *p++ = 0xD8; *p++ = 0x25; StubPut32(p, quarterVa);            // fsub dword [quarter]
    bind(5);                                                      // scale:
    *p++ = 0xD8; *p++ = 0x0D; StubPut32(p, fourThirdsVa);         // fmul dword [fourThirds]
    bind(0); bind(1); bind(3);                                    // done:
    *p++ = 0xFF; *p++ = 0x25; StubPut32(p, retSaveVa);            // jmp dword [retSave]
    return (size_t)(p - out);
}


// Bridge for the interior visibility-volume test (stdcall, 4 arguments, returns AL): when bit `mask` of the flag is
// set it answers "visible" (AL = 1) without running the test, otherwise it continues in the original.
//     test byte [flag], mask / jz orig / mov al,1 / ret 10h / orig: jmp original
inline size_t BuildVisibilityBridge(uint8_t* out, uint32_t stubVa, uint32_t flagVa, uint8_t mask, uint32_t originalVa) {
    uint8_t* p = out;
    *p++ = 0xF6; *p++ = 0x05; StubPut32(p, flagVa); *p++ = mask;  // test byte [flag], mask
    *p++ = 0x74; *p++ = 0x05;                                      // jz orig
    *p++ = 0xB0; *p++ = 0x01;                                      // mov al, 1
    *p++ = 0xC2; *p++ = 0x10; *p++ = 0x00;                         // ret 10h
    StubJmp(p, out, stubVa, originalVa);                           // orig: jmp original
    return (size_t)(p - out);
}

// Bridge for the render camera: the call at 00401BD1 (`call 6B62E0`, thiscall, arguments already on the stack) goes
// through `worker` (cdecl, no arguments, may change the camera data) and then continues in the original with
// every register and the stack exactly as they were:
//     push eax / push ecx / push edx / call worker / pop edx / pop ecx / pop eax / jmp original
inline size_t BuildCameraBridge(uint8_t* out, uint32_t stubVa, uint32_t workerVa, uint32_t originalVa) {
    uint8_t* p = out;
    *p++ = 0x50; *p++ = 0x51; *p++ = 0x52;  // push eax, ecx, edx
    *p++ = 0xE8;
    StubPut32(p, workerVa - (stubVa + (uint32_t)(p - out) + 4));  // call worker
    *p++ = 0x5A; *p++ = 0x59; *p++ = 0x58;  // pop edx, ecx, eax
    StubJmp(p, out, stubVa, originalVa);
    return (size_t)(p - out);
}

// Bridge in front of the original `cmp dword [esi+654h], 0` (7 bytes) at the end of the player update: the worker
// (cdecl, one argument: the player in esi) runs with every register and the flags saved, then the original compare
// runs and execution continues after it.
inline size_t BuildStrafeBridge(uint8_t* out, uint32_t stubVa, uint32_t workerVa, uint32_t returnVa) {
    uint8_t* p = out;
    *p++ = 0x9C;  // pushfd
    *p++ = 0x60;  // pushad
    *p++ = 0x56;  // push esi
    *p++ = 0xE8;
    StubPut32(p, workerVa - (stubVa + (uint32_t)(p - out) + 4));  // call worker
    *p++ = 0x83; *p++ = 0xC4; *p++ = 0x04;  // add esp, 4
    *p++ = 0x61;  // popad
    *p++ = 0x9D;  // popfd
    const uint8_t cmp[7] = {0x83, 0xBE, 0x54, 0x06, 0x00, 0x00, 0x00};  // cmp dword [esi+654h], 0
    for (uint8_t b : cmp) *p++ = b;
    StubJmp(p, out, stubVa, returnVa);
    return (size_t)(p - out);
}

// Gate in place of the idle branch's quick-turn flag test at 00503FA7 (`push 0 / push 4 / mov ecx,esi / call flagTest`):
// the worker (cdecl, one argument: the player in esi) answers whether it has started the turn itself. If it has, execution
// leaves through the function's own epilogue behind its quick-turn call (`exitVa`), as the game's path does; otherwise the
// replaced instructions run here and the game goes on at `resumeVa` with edx:eax as the flag test left them.
// Nothing is saved around the worker: EAX, ECX, EDX and the flags are dead at the site (the replaced call overwrites all of
// them before anything reads them), EBX/ESI/EDI/EBP survive a cdecl call, and the stack is level again on both exits.
//     push esi / call worker / add esp,4 / test eax,eax / jnz started
//     push 0 / push 4 / mov ecx,esi / call flagTest / jmp resume
//   started: jmp exit
inline size_t BuildQuickTurnGate(uint8_t* out, uint32_t stubVa, uint32_t workerVa, uint32_t flagTestVa, uint32_t resumeVa,
                                 uint32_t exitVa) {
    uint8_t* p = out;
    *p++ = 0x56;  // push esi
    *p++ = 0xE8;
    StubPut32(p, workerVa - (stubVa + (uint32_t)(p - out) + 4));  // call worker
    *p++ = 0x83; *p++ = 0xC4; *p++ = 0x04;  // add esp, 4
    *p++ = 0x85; *p++ = 0xC0;               // test eax, eax
    *p++ = 0x75; *p++ = 0x10;               // jnz started (skips the 16 bytes below)
    *p++ = 0x6A; *p++ = 0x00;               // push 0
    *p++ = 0x6A; *p++ = 0x04;               // push 4
    *p++ = 0x8B; *p++ = 0xCE;               // mov ecx, esi
    *p++ = 0xE8;
    StubPut32(p, flagTestVa - (stubVa + (uint32_t)(p - out) + 4));  // call flagTest
    StubJmp(p, out, stubVa, resumeVa);
    StubJmp(p, out, stubVa, exitVa);        // started:
    return (size_t)(p - out);
}

// Entry hook of a thiscall function with one stack argument: worker(thisPtr, arg) (cdecl) runs first with everything saved,
// then the first `len` original bytes (position independent) are replayed and execution continues after them.
inline size_t BuildEntryHook(uint8_t* out, uint32_t stubVa, uint32_t workerVa, const uint8_t* orig, size_t len, uint32_t returnVa) {
    uint8_t* p = out;
    *p++ = 0x9C;  // pushfd
    *p++ = 0x60;  // pushad
    *p++ = 0xFF; *p++ = 0x74; *p++ = 0x24; *p++ = 0x28;  // push dword [esp+28h]  (the stack argument: 4 flags + 32 regs + 4 return)
    *p++ = 0x51;  // push ecx
    *p++ = 0xE8;
    StubPut32(p, workerVa - (stubVa + (uint32_t)(p - out) + 4));  // call worker
    *p++ = 0x83; *p++ = 0xC4; *p++ = 0x08;  // add esp, 8
    *p++ = 0x61;  // popad
    *p++ = 0x9D;  // popfd
    for (size_t i = 0; i < len; ++i) *p++ = orig[i];
    StubJmp(p, out, stubVa, returnVa);
    return (size_t)(p - out);
}

// ---- Analog vehicle triggers: three stubs. In each, `lt` / `rt` are float variables (0..1) and `valid` a byte.
// x87 balance: every path leaves the x87 stack as the original path does (stub 1: empty, stub 2: one value that the
// code at the merge point stores, stub 3: one value, the zero the original pushes).
inline void StubJcc(uint8_t*& p, const uint8_t* base, uint32_t baseVa, uint8_t cc, uint32_t targetVa) {
    uint32_t nextVa = baseVa + (uint32_t)(p - base) + 6;
    *p++ = 0x0F; *p++ = (uint8_t)(0x80 | cc);
    StubPut32(p, targetVa - nextVa);
}
constexpr uint8_t kCcE = 0x4, kCcNE = 0x5;

// 0054C13B: cmp byte [useJoy],0 / je keyboard   ->   gas (LT) into [esp+18h], brake (RT) into [esp+10h].
inline size_t BuildTrigStub1(uint8_t* out, uint32_t va, uint32_t useJoy, uint32_t valid, uint32_t lt, uint32_t rt,
                             uint32_t keyboardVa, uint32_t padVa, uint32_t mergeVa) {
    uint8_t* p = out;
    *p++ = 0x80; *p++ = 0x3D; StubPut32(p, useJoy); *p++ = 0x00;     // cmp byte [useJoy], 0
    StubJcc(p, out, va, kCcE, keyboardVa);                            // je keyboard
    *p++ = 0x80; *p++ = 0x3D; StubPut32(p, valid); *p++ = 0x00;      // cmp byte [valid], 0
    StubJcc(p, out, va, kCcE, padVa);                                 // je original controller branch
    *p++ = 0xD9; *p++ = 0x05; StubPut32(p, lt);                       // fld dword [lt]
    *p++ = 0xD9; *p++ = 0x5C; *p++ = 0x24; *p++ = 0x18;               // fstp dword [esp+18h]
    *p++ = 0xD9; *p++ = 0x05; StubPut32(p, rt);                       // fld dword [rt]
    *p++ = 0xD9; *p++ = 0x5C; *p++ = 0x24; *p++ = 0x10;               // fstp dword [esp+10h]
    StubJmp(p, out, va, mergeVa);
    return (size_t)(p - out);
}

// 00555F2F: cmp byte [useJoy],0 (flags used by the `je` at 00555F40, after fldz / fst / fstp that keep them).
inline size_t BuildTrigStub2(uint8_t* out, uint32_t va, uint32_t useJoy, uint32_t valid, uint32_t lt, uint32_t rt,
                             uint32_t resumeVa, uint32_t mergeVa) {
    uint8_t* p = out;
    *p++ = 0x80; *p++ = 0x3D; StubPut32(p, useJoy); *p++ = 0x00;     // cmp byte [useJoy], 0
    uint8_t* jeBack = p; *p++ = 0x74; *p++ = 0;                       // je back (keyboard: ZF=1 for the original je)
    *p++ = 0x80; *p++ = 0x3D; StubPut32(p, valid); *p++ = 0x00;      // cmp byte [valid], 0
    uint8_t* jneAnalog = p; *p++ = 0x75; *p++ = 0;                    // jne analog
    *p++ = 0x80; *p++ = 0x3D; StubPut32(p, useJoy); *p++ = 0x00;     // cmp byte [useJoy], 0 (controller, no valid analog value: ZF=0 for the original je)
    jeBack[1] = (uint8_t)(p - (jeBack + 2));
    // (je back lands right after that cmp, on the jump below: ZF is still 1 from the first cmp, which is what the
    // original je needs)
    StubJmp(p, out, va, resumeVa);                                    // back: continue the original code
    jneAnalog[1] = (uint8_t)(p - (jneAnalog + 2));                    // analog:
    *p++ = 0xD9; *p++ = 0xEE;                                         // fldz
    *p++ = 0xD9; *p++ = 0x54; *p++ = 0x24; *p++ = 0x1C;               // fst dword [esp+1Ch]
    *p++ = 0xD9; *p++ = 0x5C; *p++ = 0x24; *p++ = 0x24;               // fstp dword [esp+24h]
    *p++ = 0xD9; *p++ = 0x05; StubPut32(p, lt);                       // fld dword [lt]
    *p++ = 0xD9; *p++ = 0x5C; *p++ = 0x24; *p++ = 0x44;               // fstp dword [esp+44h]
    *p++ = 0xD9; *p++ = 0x05; StubPut32(p, rt);                       // fld dword [rt]   (left on the x87 stack)
    StubJmp(p, out, va, mergeVa);
    return (size_t)(p - out);
}

// 00559444: cmp byte [useJoy],0 / je keyboard   ->   LT into [esp+10h], RT into [esp+14h], then fldz like the original.
inline size_t BuildTrigStub3(uint8_t* out, uint32_t va, uint32_t useJoy, uint32_t valid, uint32_t lt, uint32_t rt,
                             uint32_t keyboardVa, uint32_t padVa, uint32_t mergeVa) {
    uint8_t* p = out;
    *p++ = 0x80; *p++ = 0x3D; StubPut32(p, useJoy); *p++ = 0x00;
    StubJcc(p, out, va, kCcE, keyboardVa);
    *p++ = 0x80; *p++ = 0x3D; StubPut32(p, valid); *p++ = 0x00;
    StubJcc(p, out, va, kCcE, padVa);
    *p++ = 0xD9; *p++ = 0x05; StubPut32(p, lt);                       // fld dword [lt]
    *p++ = 0xD9; *p++ = 0x5C; *p++ = 0x24; *p++ = 0x10;               // fstp dword [esp+10h]
    *p++ = 0xD9; *p++ = 0x05; StubPut32(p, rt);                       // fld dword [rt]
    *p++ = 0xD9; *p++ = 0x5C; *p++ = 0x24; *p++ = 0x14;               // fstp dword [esp+14h]
    *p++ = 0xD9; *p++ = 0xEE;                                         // fldz (left on the x87 stack)
    StubJmp(p, out, va, mergeVa);
    return (size_t)(p - out);
}


// FUN_005E1ED0 entry guard (thiscall, ecx = table, [esp+4] = out, [esp+8] = index, ret 8). Replaces its first 6 bytes.
// An index beyond [ecx+8] (unsigned: negative ones too), other than the two sentinel values the function knows,
// zeroes `out` and returns 0 instead of reading outside the table.
inline size_t BuildNavLookupGuard(uint8_t* out, uint32_t va, uint32_t continueVa) {
    uint8_t* p = out;
    *p++ = 0x80; *p++ = 0x79; *p++ = 0x04; *p++ = 0x00;     // cmp byte [ecx+4], 0
    uint8_t* jne = p; *p++ = 0x75; *p++ = 0;                // jne active
    *p++ = 0x32; *p++ = 0xC0;                               // xor al, al
    *p++ = 0xC2; *p++ = 0x08; *p++ = 0x00;                  // ret 8
    jne[1] = (uint8_t)(p - (jne + 2));                      // active:
    *p++ = 0x8B; *p++ = 0x44; *p++ = 0x24; *p++ = 0x08;     // mov eax, [esp+8]
    *p++ = 0x3B; *p++ = 0x41; *p++ = 0x08;                  // cmp eax, [ecx+8]
    StubJcc(p, out, va, 0x2 /*jb*/, continueVa);            // jb original (in range: [ecx+8] is the count, loops run i < count)
    *p++ = 0x3D; StubPut32(p, 0x7FFFFE);                    // cmp eax, 7FFFFEh
    StubJcc(p, out, va, kCcE, continueVa);                  // je original
    *p++ = 0x3D; StubPut32(p, 0x7FFFF7);                    // cmp eax, 7FFFF7h
    StubJcc(p, out, va, kCcE, continueVa);                  // je original
    *p++ = 0x8B; *p++ = 0x44; *p++ = 0x24; *p++ = 0x04;     // mov eax, [esp+4]    (out)
    *p++ = 0x85; *p++ = 0xC0;                               // test eax, eax
    uint8_t* jz = p; *p++ = 0x74; *p++ = 0;                 // jz noout
    *p++ = 0xC7; *p++ = 0x00; StubPut32(p, 0);              // mov dword [eax], 0
    *p++ = 0xC7; *p++ = 0x40; *p++ = 0x04; StubPut32(p, 0); // mov dword [eax+4], 0
    *p++ = 0xC7; *p++ = 0x40; *p++ = 0x08; StubPut32(p, 0); // mov dword [eax+8], 0
    *p++ = 0xC7; *p++ = 0x40; *p++ = 0x0C; StubPut32(p, 0); // mov dword [eax+0Ch], 0
    jz[1] = (uint8_t)(p - (jz + 2));                        // noout:
    *p++ = 0x32; *p++ = 0xC0;                               // xor al, al
    *p++ = 0xC2; *p++ = 0x08; *p++ = 0x00;                  // ret 8
    return (size_t)(p - out);
}

// 0047F1CB: `cmp word [edi+4],0` followed by `jne`. A NULL edi makes the compare fail with ZF = 0 -> clear ZF path:
//     test edi,edi / jz zero / cmp word [edi+4],0 / jmp cont / zero: xor ecx,ecx (ZF = 1) / jmp cont
inline size_t BuildNavNullGuard(uint8_t* out, uint32_t va, uint32_t continueVa) {
    uint8_t* p = out;
    *p++ = 0x85; *p++ = 0xFF;                               // test edi, edi
    uint8_t* jz = p; *p++ = 0x74; *p++ = 0;                 // jz zero
    *p++ = 0x66; *p++ = 0x83; *p++ = 0x7F; *p++ = 0x04; *p++ = 0x00;  // cmp word [edi+4], 0
    StubJmp(p, out, va, continueVa);
    jz[1] = (uint8_t)(p - (jz + 2));                        // zero:
    *p++ = 0x33; *p++ = 0xC9;                               // xor ecx, ecx   (ZF = 1; ecx is reloaded after the jne)
    StubJmp(p, out, va, continueVa);
    return (size_t)(p - out);
}

// Called in place of `fld dword [frameDelta]` (6 bytes: a 5-byte call and a nop) where the car code counts whole ticks: the next
// instruction there is always `call _ftol2`. While a step plan is in force (`steps` >= 0) the ticks of the plan are loaded,
// otherwise the frame delta itself, as before. One value is pushed on the x87 stack, as the fld did; no register is touched. The
// flags change, and nothing reads them: the instruction after the call is another call.
//     cmp dword [steps],0 / jl own / fld dword [ticks] / ret / own: fld dword [frameDelta] / ret
inline size_t BuildCarTicksStub(uint8_t* out, uint32_t stepsVa, uint32_t ticksVa, uint32_t frameDeltaVa) {
    uint8_t* p = out;
    *p++ = 0x83; *p++ = 0x3D; StubPut32(p, stepsVa); *p++ = 0x00;  // cmp dword [steps], 0
    *p++ = 0x7C; *p++ = 0x07;                                      // jl own
    *p++ = 0xD9; *p++ = 0x05; StubPut32(p, ticksVa);               // fld dword [ticks]
    *p++ = 0xC3;                                                   // ret
    *p++ = 0xD9; *p++ = 0x05; StubPut32(p, frameDeltaVa);          // own: fld dword [frameDelta]
    *p++ = 0xC3;                                                   // ret
    return (size_t)(p - out);
}

// Written over one of the two 28-byte `cue->SetMatrixCoefficients(2, 2, matrix)` runs of FUN_0072CB00 (sites.h): the same
// place calls a helper of ours instead, void __stdcall helper(cue, params, matrix2x2).
//     lea  eax,[ebp+matrixDisp]   ; the 2x2 matrix this branch has just built in its frame
//     push eax
//     push dword [ebp+0Ch]        ; the voice parameters (second stack argument of FUN_0072CB00)
//     mov  eax,[ebp-8]            ; the sound record
//     push dword [eax+14h]        ; its IXACT3Cue
//     call helper                 ; pops its 12 bytes, as the COM call popped its 16
//     jmp  short end / int3 ...   ; end = the instruction after the original run
// The original run leaves eax, ecx, edx and the flags to the COM call and nothing after it reads them (0072CCEA goes on
// with push / mov, 0072CD73 is the epilogue); ebx, esi, edi and ebp are kept by the callee in both, and the x87 stack is
// empty at both sites. The frame on ebp is used the way the replaced instructions used it.
inline size_t BuildCueMatrixCall(uint8_t* out, uint32_t siteVa, uint32_t helperVa, int8_t matrixDisp, size_t len) {
    uint8_t* p = out;
    *p++ = 0x8D; *p++ = 0x45; *p++ = (uint8_t)matrixDisp;   // lea eax, [ebp+disp]
    *p++ = 0x50;                                             // push eax
    *p++ = 0xFF; *p++ = 0x75; *p++ = 0x0C;                   // push dword [ebp+0Ch]
    *p++ = 0x8B; *p++ = 0x45; *p++ = 0xF8;                   // mov eax, [ebp-8]
    *p++ = 0xFF; *p++ = 0x70; *p++ = 0x14;                   // push dword [eax+14h]
    *p++ = 0xE8;
    StubPut32(p, helperVa - (siteVa + (uint32_t)(p - out) + 4));  // call helper
    size_t afterJmp = (size_t)(p - out) + 2;
    *p++ = 0xEB; *p++ = (uint8_t)(len - afterJmp);           // jmp short end
    while ((size_t)(p - out) < len) *p++ = 0xCC;             // int3
    return (size_t)(p - out);
}

// Resource read guard: stands in for the `jmp retry` a reader takes after a record did not unpack. The worker (cdecl, one
// argument: the reader's frame pointer) answers non-zero for "read it again" (the original jump) and 0 for "give up", which
// continues at the reader's own exit for a missing resource. Every register and the flags are as the game left them on
// both ways out (popad does not touch the flags, so the test result survives it):
//     pushfd / pushad / push ebp / call worker / add esp,4 / test eax,eax / popad / jz giveup
//     popfd / jmp retry / giveup: popfd / jmp missing
inline size_t BuildRetryGuard(uint8_t* out, uint32_t va, uint32_t workerVa, uint32_t retryVa, uint32_t missingVa) {
    uint8_t* p = out;
    *p++ = 0x9C;                            // pushfd
    *p++ = 0x60;                            // pushad
    *p++ = 0x55;                            // push ebp
    *p++ = 0xE8;
    StubPut32(p, workerVa - (va + (uint32_t)(p - out) + 4));  // call worker
    *p++ = 0x83; *p++ = 0xC4; *p++ = 0x04;  // add esp, 4
    *p++ = 0x85; *p++ = 0xC0;               // test eax, eax
    *p++ = 0x61;                            // popad
    *p++ = 0x74; *p++ = 0x06;               // jz giveup
    *p++ = 0x9D;                            // popfd
    StubJmp(p, out, va, retryVa);
    *p++ = 0x9D;                            // giveup: popfd
    StubJmp(p, out, va, missingVa);
    return (size_t)(p - out);
}

// Resource read guard: stands in for `mov dword [ebp-2Ch], 20000h` (z_stream.avail_out) in FUN_006AF5B0. The worker
// (cdecl, one argument: that function's frame pointer) returns the room to give; registers and flags are preserved:
//     pushfd / pushad / push ebp / call worker / add esp,4 / mov [ebp-2Ch],eax / popad / popfd / jmp back
inline size_t BuildInflateRoomStub(uint8_t* out, uint32_t va, uint32_t workerVa, uint32_t returnVa) {
    uint8_t* p = out;
    *p++ = 0x9C;                            // pushfd
    *p++ = 0x60;                            // pushad
    *p++ = 0x55;                            // push ebp
    *p++ = 0xE8;
    StubPut32(p, workerVa - (va + (uint32_t)(p - out) + 4));  // call worker
    *p++ = 0x83; *p++ = 0xC4; *p++ = 0x04;  // add esp, 4
    *p++ = 0x89; *p++ = 0x45; *p++ = 0xD4;  // mov [ebp-2Ch], eax
    *p++ = 0x61;                            // popad
    *p++ = 0x9D;                            // popfd
    StubJmp(p, out, va, returnVa);
    return (size_t)(p - out);
}

// The angle normalisation FUN_00409E90 (cdecl, one float, result in st0, plain ret) spins forever on a NaN: at 00409EA2
// `test eax,eax / jz 00409EA8 / jmp $` after __isnan. These 6 bytes become a jmp here. A number goes back to 00409EA8 as before;
// a NaN is reported to `worker` (cdecl, one argument: the caller's return address, the dword at [esp] at this point) and the
// function returns 0.0 instead of hanging. The x87 stack is empty at that point (the argument was stored with fstp), so fldz
// leaves exactly the one result the callers pop; eax, ecx and edx are free in a cdecl function.
//     test eax,eax / jz number / push dword [esp] / call worker / add esp,4 / fldz / ret / number: jmp 00409EA8
inline size_t BuildNanTrapGuard(uint8_t* out, uint32_t va, uint32_t workerVa, uint32_t numberVa) {
    uint8_t* p = out;
    *p++ = 0x85; *p++ = 0xC0;               // test eax, eax
    *p++ = 0x74; *p++ = 0x0E;               // jz number (+14: over the 14 bytes below)
    *p++ = 0xFF; *p++ = 0x34; *p++ = 0x24;  // push dword [esp]
    *p++ = 0xE8;
    StubPut32(p, workerVa - (va + (uint32_t)(p - out) + 4));  // call worker
    *p++ = 0x83; *p++ = 0xC4; *p++ = 0x04;  // add esp, 4
    *p++ = 0xD9; *p++ = 0xEE;               // fldz
    *p++ = 0xC3;                            // ret
    StubJmp(p, out, va, numberVa);          // number: back into the function
    return (size_t)(p - out);
}

// Replaces the 6 bytes at 006B34E0 (kSiteGameFileBranch). worker(addonPath, requestedId, finalId, &size) runs for every load;
// with a CAddon path it returns 0 and the game reads that path as before; otherwise it may return the data of a file from a mod
// (or 0: the archive or language branch as before).
inline size_t BuildGameFileHook(uint8_t* out, uint32_t va, uint32_t workerVa, uint32_t addonVa, uint32_t archiveVa, uint32_t haveVa) {
    uint8_t* p = out;
    *p++ = 0x60;                                         // pushad
    *p++ = 0x8D; *p++ = 0x45; *p++ = 0xEC;               // lea eax, [ebp-14h]
    *p++ = 0x50;                                         // push eax
    *p++ = 0xFF; *p++ = 0x75; *p++ = 0xC8;               // push dword [ebp-38h]
    *p++ = 0xFF; *p++ = 0x75; *p++ = 0x08;               // push dword [ebp+8]
    *p++ = 0xFF; *p++ = 0x75; *p++ = 0xC0;               // push dword [ebp-40h]
    *p++ = 0xE8;
    StubPut32(p, workerVa - (va + (uint32_t)(p - out) + 4));  // call worker
    *p++ = 0x83; *p++ = 0xC4; *p++ = 0x10;               // add esp, 10h
    *p++ = 0x89; *p++ = 0x44; *p++ = 0x24; *p++ = 0x1C;  // mov [esp+1Ch], eax (eax of the pushad frame)
    *p++ = 0x61;                                         // popad
    *p++ = 0x83; *p++ = 0x7D; *p++ = 0xC0; *p++ = 0x00;  // cmp dword [ebp-40h], 0
    *p++ = 0x0F; *p++ = 0x85;                            // jne addon (rel32)
    StubPut32(p, addonVa - (va + (uint32_t)(p - out) + 4));
    *p++ = 0x85; *p++ = 0xC0;                            // test eax, eax
    *p++ = 0x0F; *p++ = 0x84;                            // jz archive (rel32)
    StubPut32(p, archiveVa - (va + (uint32_t)(p - out) + 4));
    *p++ = 0x89; *p++ = 0x45; *p++ = 0xE4;               // mov [ebp-1Ch], eax
    StubJmp(p, out, va, haveVa);                         // jmp have
    return (size_t)(p - out);
}

// Called in place of `fmul dword [movement]` (6 bytes, kSiteMouseX/Y): fmul dword [movement] / fmul dword [scale] / ret.
inline size_t BuildMouseScaleStub(uint8_t* out, uint32_t movementVa, uint32_t scaleVa) {
    uint8_t* p = out;
    *p++ = 0xD8; *p++ = 0x0D; StubPut32(p, movementVa);  // fmul dword [movement]
    *p++ = 0xD8; *p++ = 0x0D; StubPut32(p, scaleVa);     // fmul dword [scale]
    *p++ = 0xC3;                                         // ret
    return (size_t)(p - out);
}

// Called in place of `call 004D8980` (the route node of a resident: cdecl-like, out at [esp+4], index at [esp+8], ret 8,
// result in AL) at 004CCD50 and 004D1363. ESI is the resident there. When the lookup fails (AL = 0: the table is not active,
// or the mod's guard found the index out of range) `out` holds zeros or stack garbage, and the caller copies it into the
// resident's position: a jump to (0, 0, 0). The stub copies the resident's own position into `out` instead, so the
// resident stays where it stood. With `rider` (004CCD50) the caller writes to the ridden object when [esi+627h] = 1 and
// [esi+684h] is set; then that object's position is taken. EAX is clobbered (the callers do not read it), AL stays 0.
inline size_t BuildRouteKeepStub(uint8_t* out, uint32_t va, uint32_t fnVa, bool rider) {
    uint8_t* p = out;
    *p++ = 0xFF; *p++ = 0x74; *p++ = 0x24; *p++ = 0x08;   // push dword [esp+8]  (index)
    *p++ = 0xFF; *p++ = 0x74; *p++ = 0x24; *p++ = 0x08;   // push dword [esp+8]  (out)
    *p++ = 0xE8; StubPut32(p, fnVa - (va + (uint32_t)(p - out) + 4));  // call the route lookup (pops 8)
    *p++ = 0x84; *p++ = 0xC0;                             // test al, al
    uint8_t* jnz = p; *p++ = 0x75; *p++ = 0;              // jnz done
    *p++ = 0x8B; *p++ = 0x4C; *p++ = 0x24; *p++ = 0x04;   // mov ecx, [esp+4]  (out)
    *p++ = 0x85; *p++ = 0xC9;                             // test ecx, ecx
    uint8_t* jz = p; *p++ = 0x74; *p++ = 0;               // jz done
    *p++ = 0x8D; *p++ = 0x56; *p++ = 0x58;                // lea edx, [esi+58h]
    uint8_t* jne = nullptr; uint8_t* jz2 = nullptr;
    if (rider) {
        *p++ = 0x80; *p++ = 0xBE; StubPut32(p, 0x627); *p++ = 0x01;  // cmp byte [esi+627h], 1
        jne = p; *p++ = 0x75; *p++ = 0;                              // jne copy
        *p++ = 0x8B; *p++ = 0x86; StubPut32(p, 0x684);               // mov eax, [esi+684h]
        *p++ = 0x85; *p++ = 0xC0;                                    // test eax, eax
        jz2 = p; *p++ = 0x74; *p++ = 0;                              // jz copy
        *p++ = 0x8D; *p++ = 0x50; *p++ = 0x58;                       // lea edx, [eax+58h]
    }
    uint8_t* copy = p;
    if (jne) jne[1] = (uint8_t)(copy - (jne + 2));
    if (jz2) jz2[1] = (uint8_t)(copy - (jz2 + 2));
    for (uint8_t k = 0; k < 16; k += 4) {
        *p++ = 0x8B; if (k) { *p++ = 0x42; *p++ = k; } else { *p++ = 0x02; }   // mov eax, [edx+k]
        *p++ = 0x89; if (k) { *p++ = 0x41; *p++ = k; } else { *p++ = 0x01; }   // mov [ecx+k], eax
    }
    *p++ = 0x31; *p++ = 0xC0;                             // xor eax, eax  (AL = 0 as the lookup said)
    uint8_t* done = p;
    jnz[1] = (uint8_t)(done - (jnz + 2));
    jz[1] = (uint8_t)(done - (jz + 2));
    *p++ = 0xC2; *p++ = 0x08; *p++ = 0x00;                // ret 8
    return (size_t)(p - out);
}

}  // namespace ufx
