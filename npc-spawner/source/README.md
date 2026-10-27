# NPC Spawner with Progression 1.0.1 (base and Seeker of Fire Edition) - source code

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
- `NPCSpawner-1.0.1.zip` / `dll/NPCSpawner.dll`: `1c8445fd816b2a1118c23e7db1ff206204c76c284fd6b5aa91afc5373401114c`
- `NPCSpawner-1.0.1.zip` / `loader/Game/dinput8.dll`: `71f045030860a951600a3fc498ae9aaf8c2e36ad6e25fea042d16a188c276f8c`
- `NPCSpawner-Seeker-1.0.1.zip` / `dll/NPCSpawner.dll`: `dc4855c14ce0d9cdf590f1d5e66b590b17b042eab9558b82756847eabc9269ef`
- `NPCSpawner-Seeker-1.0.1.zip` / `loader/Game/dinput8.dll`: `4a1cf691ab4ee6294b3b49dd02471e6527c708fa9930d3338c6d6f7dcb4cb46f`

## Licenses
Dear ImGui: MIT (`third_party/imgui/LICENSE.txt`). MinHook: BSD-2-Clause (not included).
