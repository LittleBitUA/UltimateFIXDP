#include "base.h"
#include "sites.h"

namespace ufx {

static IMAGE_NT_HEADERS32* NtHeaders(HMODULE m) {
    u8* base = (u8*)m;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    IMAGE_NT_HEADERS32* nt = (IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    return nt;
}

bool IsSupportedBuild(HMODULE exe) {
    IMAGE_NT_HEADERS32* nt = NtHeaders(exe);
    if (!nt) return false;
    return nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
           nt->FileHeader.TimeDateStamp == kExeTimeDateStamp &&
           nt->OptionalHeader.SizeOfImage == kExeSizeOfImage &&
           (u32)(uintptr_t)exe == 0x400000;
}

bool HasLargeAddressAware(HMODULE exe) {
    IMAGE_NT_HEADERS32* nt = NtHeaders(exe);
    return nt && (nt->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) != 0;
}

static bool EqualNoCase(const char* a, const char* b) {
    for (;; ++a, ++b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return false;
        if (!x) return true;
    }
}

static bool EqualExact(const char* a, const char* b) {
    for (; *a && *a == *b; ++a, ++b) {}
    return *a == *b;
}

void** FindImportSlot(HMODULE module, const char* dll, const char* func) {
    IMAGE_NT_HEADERS32* nt = NtHeaders(module);
    if (!nt) return nullptr;
    u8* base = (u8*)module;
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return nullptr;
    for (IMAGE_IMPORT_DESCRIPTOR* d = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); d->Name; ++d) {
        if (!EqualNoCase((const char*)(base + d->Name), dll)) continue;
        DWORD lookup = d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk;
        IMAGE_THUNK_DATA32* names = (IMAGE_THUNK_DATA32*)(base + lookup);
        IMAGE_THUNK_DATA32* slots = (IMAGE_THUNK_DATA32*)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (names->u1.Ordinal & 0x80000000u) continue;  // by ordinal
            IMAGE_IMPORT_BY_NAME* ibn = (IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData);
            if (EqualExact((const char*)ibn->Name, func)) return (void**)&slots->u1.Function;
        }
    }
    return nullptr;
}

}  // namespace ufx
