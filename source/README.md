# NPC Companions 0.44 / Seeker of Fire patch 0.6.11 - source code

Source for the compiled files in the Nexus Mods downloads (Dark Souls II: Scholar of the First Sin, Windows x64,
Steam, executable 1.0.3.0). `main/` builds the base-game files, `seeker/` the Seeker of Fire files.

- `dinput8.dll` (`bootstrap.cpp`, `proxy.asm`, `proxy.def`): forwards the DirectInput exports to System32\dinput8.dll,
  loads `NPCCompanions.dll` from the mod's data folder and every DLL in `Game\dllMods` (Lazy Loader convention).
- `NPCCompanions.dll` (`loader.cpp` + headers + Dear ImGui): also works when loaded by Lazy Loader from `Game\dllMods`. It checks the
  executable hash and code signatures, forces offline mode and a separate save file, redirects asset loads to the mod's
  folder and hooks game functions with MinHook to spawn and manage NPC companions and draw the F7 menu.
- No network access, no other processes; it writes only its settings, logs and the separate save copy.

## Building
1. Visual Studio 2022+ with Desktop development with C++.
2. MinHook (https://github.com/TsudaKageyu/minhook, BSD-2-Clause) built as an x64 static library:
   `third_party/minhook/include/MinHook.h`, `third_party/minhook/lib/libMinHook.x64.lib` (or set `MINHOOK`).
3. `build.cmd main` or `build.cmd seeker` -> `out/<variant>/`. MSVC embeds timestamps, so rebuilds are not byte-identical.

## Release checksums (SHA-256)
- `NPCCompanions-0.44-Vortex.zip` / `dll/NPCCompanions.dll`: `4dfd0d7af0570cc4214e7a68d664bdb6f5bdf553a730a778110c2165b42386c2`
- `NPCCompanions-0.44-Vortex.zip` / `loader/Game/dinput8.dll`: `8b4b85b8d7176433b81fb8c4c03b876650c2c8b27ed2fd4d00cfe564d65faf06`
- `NPCCompanions-Seeker-Vortex-0.6.11.zip` / `dll/NPCCompanions.dll`: `2065885ce9c739c9a828bac60383a77d1a0d1e7397bf8f3627f2ca5657f96e17`
- `NPCCompanions-Seeker-Vortex-0.6.11.zip` / `loader/Game/dinput8.dll`: `2a3436dffb4ed9d0f5b284046d090f29ffdfe7e2d75de22c5ca30efef27d43ac`

## Licenses
Dear ImGui: MIT (`third_party/imgui/LICENSE.txt`). MinHook: BSD-2-Clause (not included).
