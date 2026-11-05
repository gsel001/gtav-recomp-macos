#pragma once
#include <cstdint>
struct PPCContext { uint32_t calls = 0; };
#define REX_EXTERN(name) extern "C" void name(PPCContext& ctx, uint8_t* base)
#define REX_HOOK_RAW(name) extern "C" void name(PPCContext& ctx, uint8_t* base)
