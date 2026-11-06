// gtav - background compiler for shaders without precompiled SPIR-V (see shader_compiler.cpp).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace gtav::native {

// Called for every shader the game creates; queues the ones without SPIR-V.
void OnShaderCreated(const uint8_t* container, size_t size, uint64_t hash, bool pixel);

// Move a shader needed by a draw ahead of shaders only created during loading.
// Safe before intake has written its container; repeated requests are cheap.
void PrioritizeShader(uint64_t hash, bool pixel);

// Bumped whenever a new SPIR-V file landed: cached misses should be looked up again.
uint64_t CompileGeneration();

// Called only alongside an existing five-second frame summary; opt-in, no I/O collection.
void ReportShaderCompilation();

// The background compiler found or wrote <name>.spv (no disk access).
bool ShaderOnDisk(const std::string& name);

}  // namespace gtav::native
