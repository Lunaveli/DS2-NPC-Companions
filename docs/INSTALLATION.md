# Installation and removal

## Requirements

- Dark Souls II: **Scholar of the First Sin**, Steam, Windows x64.
- Steam installed and signed in; launch the game normally after installation.
- A writable game/mod folder and enough space for a separate save and backup.
- The supported Scholar executable. Its SHA-256 is:

```text
0045931b8914504531b7864a9488d396dc50cbaf524964016e1d69c3d1173131
```

The loader also checks executable signatures and refuses unsupported/conflicting builds. Do not patch out that check. Original Dark Souls II, console editions and altered executables are unsupported.

## Before installing

1. Fully exit Dark Souls II. Do not replace mod DLLs while it is running.
2. Copy `%APPDATA%\DarkSoulsII` somewhere safe. Keep both ordinary `.sl2` and existing companion `.snc` saves in that backup.
3. Find your installation through Steam: right-click Scholar → **Manage → Browse local files**. Open its **Game** subfolder. `DarkSoulsII.exe` must be directly inside it.
4. Check for another `dinput8.dll` or a regulation/map/AI overhaul. The current package is for vanilla Scholar data. **Seeker of Fire compatibility is pending**, and this release does not include that patch.

## Download the playable mod

Open the [0.26 release](https://github.com/Lunaveli/DS2-NPC-Companions/releases/tag/v0.26) and expand **Assets**. Download **NPCCompanions-0.26.zip**.

Do not use the green **Code → Download ZIP** button or GitHub's automatic **Source code** archives to install the mod. Those contain the documentation repository.

## Vortex installation

1. Select Dark Souls II: Scholar of the First Sin as your managed game.
2. Use **Install From File** and choose `NPCCompanions-0.26.zip`.
3. Enable the mod and deploy it. Enable only one NPC Companions version.
4. Verify the deployed paths shown below. If your deployment placed the files one directory above the `Game` folder, correct the deployment location or use manual installation.
5. Launch Scholar normally through Steam. Press **F7** after loading a character.

## Manual installation

Extract the contents of the ZIP into the **Game** folder containing `DarkSoulsII.exe`. Copy the archive's contents, not an extra enclosing ZIP-named folder.

```text
Game/
├─ DarkSoulsII.exe                  (already installed game)
├─ dinput8.dll                      (mod bootstrap)
└─ NPCCompanions/
   ├─ NPCCompanions.dll
   ├─ settings.ini
   ├─ assets/
   ├─ licenses/
   └─ documentation files
```

The mod's `enc_regulation.bnd.dcx` belongs under **`Game/NPCCompanions/assets/`**. Do not replace the game's own root regulation file with it. The loader redirects to the mod assets at runtime.

No custom launcher, Cheat Engine or separate Python installation is needed to play. The optional Python log-report helper is for diagnostics only.

## First session

1. Launch the game through Steam. The deployed mod activates automatically and applies its offline/save handling.
2. Continue an existing companion save, or select **New Game** for a fresh character.
3. Stand in a safe, open place and press **F7**.
4. Choose a profile in Slot 1, wait for any **Loading model and AI** message to finish, then click **Summon**. Repeat for Slots 2 and 3 if desired.
5. Close the menu with F7. **F8** recalls active companions.

Companions first use the game's map spawn anchors and are brought to you. Switching to another profile after installation uses the native resource loader and does not require restarting the game. Every newly installed DLL/asset update does require a full exit and relaunch.

## Save behavior

While active, the mod uses **`.snc`** saves under `%APPDATA%\DarkSoulsII`, separate from ordinary `.sl2` saves.

- If a matching `.snc` does not exist, the mod attempts a first copy from `.sl2` and a backup in `Game/NPCCompanions/save-backups/`.
- An existing `.snc` is not overwritten by that first-copy routine.
- If no normal save exists, create a character while the mod is active.
- A fresh mod character and normal-game character belong to different save files.
- Installation does not replace your ordinary save. Keep a manual backup because a separate extension does not replace a backup strategy.
- Do not rename `.snc` to `.sl2` to take modified progress online.

## Updating

1. Exit the game completely.
2. Back up `NPCCompanions/settings.ini`, `NPCCompanions/save-backups/` and your `.snc` saves. Keep useful logs before replacing files.
3. In Vortex, replace the prior version, enable only the new one and deploy. For manual installation, replace the old mod files with the complete new package.
4. Restore your existing `settings.ini` if the installation replaced it. Existing selections remain compatible with the cleaned-up display names.
5. Relaunch through Steam. A complete update includes the DLL and assets; updating only the DLL can leave mismatched NPC data.

## Removing or disabling

Close the game first. **Vortex users:** disable the mod and deploy/undeploy so its `dinput8.dll` is no longer active. **Manual users:** move this mod's `Game/dinput8.dll` out of the Game folder or rename it to a non-DLL extension. Identify it as this mod's file before touching a DLL installed by something else.

You can leave `NPCCompanions/` in place while disabled. For complete removal, first copy out the settings, logs and `save-backups` you want to keep, then remove only this mod's files. Retain your `%APPDATA%\DarkSoulsII` saves. Relaunch normally to use `.sl2` saves.

## Other mods

One proxy DLL cannot simply replace another loader and retain both mods. This release does not provide a loader chain or automatic data merge. Seeker of Fire, Seamless Co-op and other regulation/map/AI overhauls need separate compatibility work. **A Seeker of Fire patch is planned separately; it is not part of 0.26.**
