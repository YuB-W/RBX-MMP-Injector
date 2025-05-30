#pragma once
#include "Windows.h"
#include <cstdint>


//updated offsets for version-ad3ee47cdc5e44f6

namespace ofssetss_new
{
    constexpr uint16_t  SCF_INSERTED_JMP = 0x04EB;
    constexpr uint32_t SCF_END_MARKER = 0xF4CC02EB;
    constexpr uintptr_t SCF_MARKER_STK = 0xDEADBEEFDEADC0DE;
    constexpr uint64_t kPageMask = 0xfffffffffffff000;


    // whitelist stuff // need to update every roblox update!!!!!!
    static const uintptr_t set_insert = 0xD77510;
    static const uintptr_t Bitmap = 0x2855A8;
    static const uintptr_t whitelist_page = 0x29cbd6;
    static const uintptr_t whitelist_page2 = 0x2A3820;

}
