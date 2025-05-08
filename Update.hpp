#pragma once
#include "Windows.h"
#include <cstdint>


//updated offsets for version-ff05edc617954c5b

namespace ofssetss_new
{
    // Bypass Roblox security checks
    constexpr uint16_t  SCF_INSERTED_JMP = 0x04EB;
    constexpr uint32_t SCF_END_MARKER = 0xF4CC02EB;
    constexpr uintptr_t SCF_MARKER_STK = 0xDEADBEEFDEADC0DE;
    constexpr uint64_t kPageMask = 0xfffffffffffff000; // need to update maybe


    // whitelist stuff
    static constexpr auto set_insert = 0xe5c390;  // set_insert function 0xba15b0
    static constexpr auto whitelist_page = 0x29d1d0;  // whitelist function (same as set_insert now) 0x2856b8
    static constexpr auto Bitmap = 0x283ac0;  // whitelist function (same as set_insert now) 0x299ac0

}
