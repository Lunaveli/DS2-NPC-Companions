# Changelog

## 0.26 — Merchant combat update (playtest)

- Added 16 combat-only merchant companions, bringing the roster to 68 profiles.
- Preserved each merchant's reachable native combat groups and native gear/spell identities; retained Licia's native healing checks.
- Added summon-style following and targeting around those combat profiles without importing shop/Talk roots.
- Removed the visible “(Invader)” suffix from menu, loading, party and overhead labels; retained compatibility with saved selections and duplicate-name loadouts.
- Preserved the working 0.25.2 Herald AI, services, gesture response and Majula restriction.
- Prepared public installation, feature, roster and troubleshooting documentation. Public packaging changes documentation/defaults only; tested DLL/asset bytes are unchanged.

## 0.25.2

- Added Herald's own native response to player gestures, with service priority and bounded cleanup.
- Blocked companion Herald spawns in Majula through the menu, recovery path and native custom-spawn flag reads.

## 0.25.1

- Repaired Herald's level-up animation/request handoff outside Majula.
- Added per-slot request flags, bounded completion waiting and cleanup to release the player after leveling/cancellation.

## 0.25

- Improved open-world enemy engagement for the summon targeting profiles.
- Routed companion Herald voice requests to the game's native Majula voice identity.

## Earlier 0.x development

Added runtime profile loading, story NPC copies and Herald services; HP/souls sliders and durability controls; gesture mirroring; session journals; improved recall/streaming recovery and death/loading lifecycle handling. Earlier prototypes are not supported installation packages.
