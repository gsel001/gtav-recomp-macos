#pragma once
#include <cstdint>
#define REXCVAR_DEFINE_INT32(name, value, category, description) int32_t FLAGS_##name = value
#define REXCVAR_DEFINE_BOOL(name, value, category, description) bool FLAGS_##name = value
#define REXCVAR_GET(name) (FLAGS_##name)
