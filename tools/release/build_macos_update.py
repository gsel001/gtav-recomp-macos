"""Build the Mac frontend and launcher against an existing v0.3.0 runtime.
Requires the matching source archive with SDK and Plume patches already applied,
CMake, Ninja, Apple clang, and llvm-nm. No generated game code is used.
"""
import argparse
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--dependencies', type=Path, required=True)
p.add_argument('--app', type=Path, required=True)
p.add_argument('--llvm-bin', type=Path, required=True)
p.add_argument('--build', type=Path, required=True)
a = p.parse_args()
repo = Path(__file__).resolve().parents[2]
deps, build = a.dependencies.resolve(), a.build.resolve()
runtime = a.app.resolve() / 'Contents/Resources/gtav'
headers = build / 'native_shaders'
headers.mkdir(parents=True, exist_ok=True)
cmake = (repo / 'gtav/CMakeLists.txt').read_text()
block = cmake[cmake.index('    OUTPUT ${GTAV_NATIVE_SHADER_DIR}/native_blit_shaders.h'):cmake.index('    COMMAND ${CMAKE_COMMAND} -E echo "#pragma once"')]
variables = {'GTAV_NATIVE_SHADER_DIR': str(headers)}
for key, name in [('GTAV_BLIT_HLSL', 'native_blit'), ('GTAV_RECTLIST_HLSL', 'native_rectlist'), ('GTAV_MEMORY_HLSL', 'native_memory'), ('GTAV_MENU_HLSL', 'native_menu')]:
    variables[key] = str(repo / ('gtav/src/native/shaders/' + name + '.hlsl'))
env = os.environ.copy()
env['DYLD_LIBRARY_PATH'] = str(runtime / 'tools/shaders/lib')
for match in re.finditer(r'COMMAND \$\{GTAV_DXC\}\s+(.*?)(?=\n    COMMAND|\Z)', block, re.S):
    command = match.group(1)
    for key, value in variables.items():
        command = command.replace('${' + key + '}', value)
    subprocess.run([str(runtime / 'tools/shaders/dxc'), *shlex.split(command)], env=env, check=True)
names = re.findall(r'#include \\\"(native_[a-z_]+\.h)\\\"', cmake)
assert len(names) == 12
(headers / 'native_blit_shaders.h').write_text('#pragma once\n' + ''.join('#include "' + n + '"\n' for n in names))
# The v0.3.0 corresponding source includes its authored fetch-resolve utility shader header.
# Its GLSL sources are alongside it. These are renderer utilities, not translated game shaders.
version = (deps / 'rexglue-sdk/include/rex/version.h.in').read_text()
values = {'REXGLUE_DERIVED_MAJOR': '0', 'REXGLUE_DERIVED_MINOR': '10', 'REXGLUE_DERIVED_PATCH': '0', 'REXGLUE_DERIVED_TWEAK': '0', 'REXGLUE_FULL_VERSION': '0.10.0-macos-downstream', 'REXGLUE_NUMERIC_VERSION': '0.10.0', 'REXGLUE_FLOOR_VERSION': '0.10.0', 'REXGLUE_VERSION_CHANNEL': 'local', 'REX_PLATFORM': 'mac-arm64', 'REXGLUE_BUILD_TIMESTAMP': '20261105'}
(headers / 'rex').mkdir(exist_ok=True)
(headers / 'rex/version.h').write_text(re.sub(r'@([A-Z_]+)@', lambda m: values[m[1]], version))
def run(*args):
    subprocess.run(args, check=True)
run('cmake', '-S', str(repo / 'tools/release/macos-update'), '-B', str(build / 'frontend'), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DGTAV_REPO=' + str(repo), '-DGTAV_DEPS=' + str(deps), '-DGTAV_NATIVE_HEADERS=' + str(headers), '-DGTAV_RUNTIME_DIR=' + str(runtime), '-DGTAV_LLVM_BIN=' + str(a.llvm_bin.resolve()))
run('cmake', '--build', str(build / 'frontend'), '-j', '2')
run('cmake', '-S', str(repo / 'loader'), '-B', str(build / 'launcher'), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0', '-DCMAKE_CXX_FLAGS=-ffile-prefix-map=' + str(repo) + '=.', '-DCMAKE_OBJCXX_FLAGS=-ffile-prefix-map=' + str(repo) + '=.')
run('cmake', '--build', str(build / 'launcher'), '-j', '2')
print('Built:', build / 'frontend/gtav_fast', build / 'launcher/GTAV.app/Contents/MacOS/GTAV')
