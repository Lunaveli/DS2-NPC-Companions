# NPC Companions - source code (0.33 / Seeker of Fire patch 0.6)

Source for the two compiled files in the Nexus Mods downloads of **NPC Companions** for
Dark Souls II: Scholar of the First Sin (Windows x64, Steam, executable 1.0.3.0).

| Shipped file | Built from |
| --- | --- |
| `Game/dinput8.dll` | `<variant>/bootstrap.cpp`, `proxy.asm`, `proxy.def` |
| `Game/NPCCompanions/NPCCompanions.dll` | `main/loader.cpp` + headers + Dear ImGui |
| `Game/NPCCompanionsSeeker/NPCCompanions.dll` | `seeker/loader.cpp` + headers + Dear ImGui |

## What the code does

- `dinput8.dll` is a proxy: it forwards the four DirectInput exports to the real
  `System32\dinput8.dll` and loads `NPCCompanions\NPCCompanions.dll` (or `NPCCompanionsSeeker`) from the game folder.
- `NPCCompanions.dll` checks the executable's SHA-256 and code signatures and refuses to start on any other version.
  It forces offline mode and changes the save extension to `.snc` (`.sfc` for Seeker) so normal saves are never written,
  redirects game asset loads to the mod's `assets` folder, and hooks game functions with MinHook to drive companion
  spawners (event flags), read character state, scale HP and draw the Dear ImGui menu over the DirectX 11 swap chain.
- It writes only inside the game process and its own folder (settings.ini, logs). No network access, no other processes.

## Building

1. Install Visual Studio 2022 or newer with **Desktop development with C++** (provides cl and ml64).
2. Build MinHook (https://github.com/TsudaKageyu/minhook, BSD-2-Clause) as an x64 static library and place it as
   `third_party/minhook/include/MinHook.h` and `third_party/minhook/lib/libMinHook.x64.lib`
   (or set the `MINHOOK` environment variable to a folder with that layout).
3. From a command prompt in this folder: `build.cmd main` or `build.cmd seeker`.
   Output: `out/<variant>/NPCCompanions.dll` and `out/<variant>/dinput8.dll`.

MSVC embeds timestamps, so rebuilt binaries are functionally identical but not byte-identical to the release.

## Release checksums (SHA-256)

- `Game/NPCCompanions/NPCCompanions.dll`: `5956a3df71a831e22e16956f489769fad161606e1322e21a9af642631128c47a`
- `Game/dinput8.dll`: `b0d11a93b6b487e361b6e0ccb500c98a44be0e1c51e83dfd7d01bcca69c54d50`
- `Game/NPCCompanionsSeeker/NPCCompanions.dll`: `e1190ab2d6a6451a7a3a03673c6941dd5c7452d0c919139505ecaadcabca6f10`
- `Game/dinput8.dll`: `503e55d433f3a8bbf3bbfbf14ef92e72ea09565a62a4bb9fa396d97256520c21`

## Licenses

- Dear ImGui: MIT, `third_party/imgui/LICENSE.txt`.
- MinHook: BSD-2-Clause (not included; downloaded separately).
- The mod's own source is published for review and transparency; reuse requires the author's permission.
