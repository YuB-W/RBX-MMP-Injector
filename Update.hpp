#pragma once
#include "Windows.h"
#include <cstdint>

namespace ofssetss_new
{
    constexpr uint16_t  SCF_INSERTED_JMP = 0x04EB;
    constexpr uint32_t SCF_END_MARKER = 0xF4CC02EB;
    constexpr uintptr_t SCF_MARKER_STK = 0xDEADBEEFDEADC0DE;
    constexpr uint64_t kPageMask = 0xfffffffffffff000;


    // whitelist stuff // need to update every roblox update!!!!!!
   static constexpr auto set_insert = 0xB86550;  
   static constexpr auto whitelist_page = 0x286640;  
   static constexpr auto Bitmap = 0x282f68; 

}
