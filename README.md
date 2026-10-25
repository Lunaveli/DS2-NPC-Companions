# NPC Companions — Dark Souls II: Scholar of the First Sin

Build your own NPC party and explore Drangleic with up to **three companions**. Choose from **68 profiles** through an in-game menu, including familiar summons, story characters, combat merchants and a following Emerald Herald.

**[Download 0.26](https://github.com/Lunaveli/DS2-NPC-Companions/releases/tag/v0.26)** · **[Installation](docs/INSTALLATION.md)** · **[Full features](docs/FEATURES.md)** · **[NPC roster](docs/ROSTER.md)** · **[Troubleshooting](docs/TROUBLESHOOTING.md)**

Download **`NPCCompanions-0.26.zip`** from the release's **Assets** section. GitHub's automatic “Source code” downloads contain this documentation, not the playable mod.

## What it does

- **Three configurable companion slots.** Select, summon, dismiss and recall companions with F7. F8 brings all active companions to you.
- **Native combat personalities.** Companions keep their own combat styles, weapons and supported spells/healing. The mod adds following, enemy targeting and recovery around those routines.
- **68 profiles.** Familiar white summons, additional combat profiles, story NPC copies and 16 combat-only merchants. See the [complete roster](docs/ROSTER.md).
- **Following and recovery.** Companions follow normally; automatic catch-up and manual recall handle separation and stuck actors. Streaming recovery can recreate an unloaded companion and carry over recorded health where possible.
- **Emerald Herald companion.** She follows and offers native Talk, level-up and flask services outside Majula. She is peaceful and uses her own animation when responding to gestures. Companion Herald is blocked in Majula so you can use the original Herald there.
- **Party display.** Companion HP cards and overhead names/health bars. Visible names no longer include “(Invader).”
- **Optional gesture responses.** Supported human companions mirror gestures when idle. Combat, casting, Talk and leveling take priority.
- **Optional enemy/boss HP scaling.** Enable a **1.0×–99.0×** slider without increasing damage. Boss health bars follow the scaled maximum.
- **Optional souls multiplier.** **1.0×–99.0×** for increases to carried souls. Soul Memory uses the ordinary gain; costs and losses are not multiplied.
- **Optional infinite equipment durability** for the player.
- **Optional ordinary area fog removal**, excluding boss doors. Opened fog walls remain open in that mod save even after disabling the option.
- **Session logging and area/problem markers** to help diagnose failures during a playthrough.

## Quick installation

Requires the **Windows x64 Steam edition of Scholar of the First Sin** and the supported game executable. This is an **offline, single-player mod**; original Dark Souls II and console editions are unsupported.

1. Fully close the game and back up `%APPDATA%\DarkSoulsII`.
2. Download `NPCCompanions-0.26.zip` from the [release](https://github.com/Lunaveli/DS2-NPC-Companions/releases/tag/v0.26).
3. **Vortex:** use **Install From File**, select the ZIP, enable it and deploy. Enable only one NPC Companions version.
4. **Manual:** extract the ZIP into the game's **`Game` folder**, alongside `DarkSoulsII.exe`.
5. Launch Scholar normally through Steam. Press **F7**, choose an NPC in a slot and click **Summon**.

The installed layout must be:

```text
Dark Souls II Scholar of the First Sin/
└─ Game/
   ├─ DarkSoulsII.exe
   ├─ dinput8.dll
   └─ NPCCompanions/
      ├─ NPCCompanions.dll
      ├─ settings.ini
      └─ assets/
```

No separate launcher or Cheat Engine table is required. If another mod already provides `dinput8.dll`, resolve the loader conflict before replacing anything. Read the [installation guide](docs/INSTALLATION.md) for upgrades, saves and removal.

## Saves and online play

The mod forces offline behavior while active and uses a separate **`.snc` save**. On first activation it attempts to copy each existing `.sl2` save to `.snc` if that companion save does not already exist, and attempts a backup under `NPCCompanions/save-backups/`. Existing `.snc` files are retained.

To begin a fresh character, choose **New Game while the mod is active**. Modded progress goes into the separate companion save. Disable/undeploy the mod and relaunch to return to normal `.sl2` saves. **Keep your own backup and do not rename a modded save into a normal save.**

## Compatibility and current status

**0.26 is an experimental playtest release.** Core companion behavior and Herald services have been exercised during development. The new merchant profiles passed compiled-AI/data checks, but their complete in-game behavior and a full-game playthrough are not yet verified. Following, casting, fog doors, travel and death/loading behavior can still need fixes.

This build targets vanilla Scholar data. **Seeker of Fire compatibility is pending; no Seeker of Fire patch is included.** Combining it with Seeker of Fire, Seamless Co-op, other regulation/map/AI overhauls or another loader has not been validated. Original NPCs use separate records from the companion copies, and original summon signs retain their normal rules. Companion copies do not promise vanilla summon-quest credit.

The loader checks the supported executable and refuses mismatched signatures. Detailed requirements and conflict advice are in [Installation](docs/INSTALLATION.md).

## Report a problem

Use **Mark a problem** in F7, then open a [GitHub issue](https://github.com/Lunaveli/DS2-NPC-Companions/issues). Include your mod version, NPC/slot, area, what happened, steps to reproduce and the relevant log excerpt. See [Troubleshooting](docs/TROUBLESHOOTING.md). Do not upload your saves or crash dumps as routine bug reports.

[Changelog](CHANGELOG.md) · [Credits and included licenses](CREDITS.md)
