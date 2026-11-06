#pragma once
#define REXCVAR_DEFINE_BOOL(name, value, category, description) bool FLAGS_##name = value
#define REXCVAR_DECLARE(type, name) type FLAGS_##name
#define REXCVAR_GET(name) (FLAGS_##name)
