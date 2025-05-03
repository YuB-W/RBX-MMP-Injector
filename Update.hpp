#pragma once
#include "Windows.h"
#include <cstdint>


//  Roblox – Hyperion build 628 (April-30-2025) 
//updated offsets for version-8328c0dc77d84a44



namespace ofssetss_new
{
    // Bypass Roblox security checks
    constexpr uint16_t  SCF_INSERTED_JMP = 0x04EB;
    constexpr uint32_t SCF_END_MARKER = 0xF4CC02EB;
    constexpr uintptr_t SCF_MARKER_STK = 0xDEADBEEFDEADC0DE;
    constexpr uint64_t kPageMask = 0xfffffffffffff000; // need to update maybe


    // whitelist stuff
    static constexpr auto set_insert = 0xb57060;  // set_insert function
    static constexpr auto whitelist_page = 0x2a86a0;  // whitelist function (same as set_insert now)
    static constexpr auto Bitmap = 0x2A8558;  // whitelist function (same as set_insert now)

}
