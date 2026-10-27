# NPC Companions (Progression) 1.0.2 (base and Seeker of Fire Edition; internal name NPC Spawner) - source code

Source for the compiled files in the Nexus Mods downloads (Dark Souls II: Scholar of the First Sin, Windows x64,
Steam, executable 1.0.3.0). `main/` builds the base-game files, `seeker/` the Seeker of Fire files.

- `dinput8.dll` (`bootstrap.cpp`, `proxy.asm`, `proxy.def`): forwards the DirectInput exports to System32\dinput8.dll,
  loads `NPCSpawner.dll` from the mod's data folder and every DLL in `Game\dllMods` (Lazy Loader convention).
- `NPCSpawner.dll` (`loader.cpp` + headers + Dear ImGui): also works when loaded by Lazy Loader from `Game\dllMods`. It checks the
  executable hash and code signatures, forces offline mode and a separate save file, redirects asset loads to the mod's
  folder and hooks game functions with MinHook to spawn and manage NPC companions and draw the F7 menu.
- No network access, no other processes; it writes only its settings, logs and the separate save copy.

## Building
1. Visual Studio 2022+ with Desktop development with C++.
2. MinHook (https://github.com/TsudaKageyu/minhook, BSD-2-Clause) built as an x64 static library:
   `third_party/minhook/include/MinHook.h`, `third_party/minhook/lib/libMinHook.x64.lib` (or set `MINHOOK`).
3. `build.cmd main` or `build.cmd seeker` -> `out/<variant>/`. MSVC embeds timestamps, so rebuilds are not byte-identical.

## Release checksums (SHA-256)
- `NPCCompanionsProgression-1.0.2.zip` / `dll/NPCSpawner.dll`: `54d8f2118576b343c3670ad36a8790e9a6d3217a7dd4d95618850ee5a0ee2aa9`
- `NPCCompanionsProgression-1.0.2.zip` / `loader/Game/dinput8.dll`: `40601ba6e5ff6737a9346719f0552af888da7eaba80d013a914177b59100b6e0`
- `NPCCompanionsProgression-Seeker-1.0.2.zip` / `dll/NPCSpawner.dll`: `6e4ebaad8894f754eceb6661c86d5379429c993a3713e73ff5f0c8e2ffa04909`
- `NPCCompanionsProgression-Seeker-1.0.2.zip` / `loader/Game/dinput8.dll`: `18df382895ce6237e5584ec8b7b56ded2a394ca3130375d7ad90275858c0763f`

## Licenses
Dear ImGui: MIT (`third_party/imgui/LICENSE.txt`). MinHook: BSD-2-Clause (not included).
