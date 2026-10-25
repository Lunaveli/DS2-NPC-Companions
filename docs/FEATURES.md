# Features and behavior

## Companion party

There are **three independent slots** and **68 selectable profiles**. Use F7 to choose an NPC, summon/dismiss it or bring that slot to you. F8 brings all active companions to you. Selections and active states are stored in `NPCCompanions/settings.ini`.

The roster contains familiar summon characters, additional combat profiles, isolated story NPC copies, 16 combat merchants and Emerald Herald. Original quest NPC records are separate from these copies. Original summon signs retain their own requirements and behavior; this menu is a separate way to summon companions. Quest-specific summon credit is not guaranteed for a companion copy.

Selecting a different NPC queues the game's native character/AI loading. A loading message can appear before the switch is ready. Once this version is installed, changing roster selections does not require restarting the game.

## Native combat, spells and healing

Companions use their supported native combat style. The party's targeting/following controller encourages nearby enemy engagement and handles following between fights. It does not turn every character into the same melee fighter.

The 16 new merchants keep their complete reachable native combat state machines, equipment and spell/behavior identities. This includes their existing attacks, defensive choices and spell/healing checks where present. Licia's healing branch and its health/cooldown conditions are retained. Spell selection and healing still depend on the NPC's own AI and combat situation; this is not a forced cast or a universal party-heal feature.

The merchant copies have no shop or Talk menu. Their original NPCs remain available through the game. Emerald Herald is the exception described below and remains peaceful.

## Following, recall and area changes

Living companions normally follow under their own movement controller. The mod uses live position requests for recall/catch-up and tries to avoid repeated teleporting. Fixed map anchors provide initial spawns; a new companion can then be brought to the player.

Automatic recovery handles substantial separation, stationary stragglers and vertical transitions. If streaming removes a companion, the mod can fall back to recreation and restore recorded health where possible. This does not mean the same engine actor survives every loading boundary. A companion can die; the recovery system is not invincibility.

Travel, boss doors, ladders, narrow spaces and elevators remain important test cases. Large or unusual models can have navigation limitations. Use **Bring to me** or **F8** if necessary.

## Emerald Herald

- Following, peaceful companion outside Majula.
- Native Talk interaction, level-up and flask services, using the game's ordinary progression and costs.
- F7 level-up shortcut when she is nearby.
- Native soul-receiving animation/level-up handoff with bounded recovery for the conversation.
- Responds to player gestures with her **own** native animation; her model lacks the player gesture clips.
- Talk and level-up take priority over gestures and following.
- Companion Herald cannot spawn in **Majula**. The menu explains the restriction; automatic spawning also respects it. Use the original Herald there. An already active companion slot stays selected and can resume outside Majula.

## Gestures and display

Enable **Companions respond to your gestures when idle** in F7. Supported human companions can mirror your emotes. Herald uses her own response. Busy, damaged, fighting, casting or interacting companions can skip a request.

The overlay displays party HP plus overhead names/health bars. The visible “(Invader)” suffix is removed, while old selection keys remain compatible. Two profiles can share a character name but use different loadouts; their area/origin in the dropdown distinguishes them.

## Optional world/player settings

| Option | Behavior |
| --- | --- |
| Scale enemy and boss HP | Checkbox plus 1.0×–99.0× slider in 0.1 increments. Changes health, not damage. Boss bars use the scaled maximum. Disabling restores native max HP while retaining the current health fraction. |
| Multiply souls gained | Checkbox plus 1.0×–99.0× slider. Multiplies positive changes to carried souls. Soul Memory and other native counters use their ordinary gain; costs/losses are not multiplied. The game's carried-souls cap remains. |
| Infinite equipment durability | Prevents negative durability changes for the player's equipped gear while enabled. Native repairs remain available. |
| Open area fog walls | Opens configured ordinary area fog walls, excluding boss doors. Opened walls remain open in the `.snc` save after the setting is disabled. |

Ctrl+click a multiplier slider to type a value. Souls multiplication, infinite durability, HP scaling and fog removal start disabled in the public package. Gesture responses start enabled.

## Logging

**Mark area complete** and **Mark a problem** in F7 add checkpoints to the session journal. Files are under `Game/NPCCompanions/logs/`; the main diagnostic log is `Game/NPCCompanions/NPCCompanions.log`.

Records include version, map, selected profiles, actor presence/health, resource-loading status, recall/recovery, death/loading transitions and selected feature events. Logs do not automatically upload anywhere. An abrupt log ending alone does not prove a crash cause.

## Validation limits

0.26 passed compilation, 25 executable signature checks, preparation of 14 native hooks, isolated lifecycle/feature tests, native resource accounting tests and compiled merchant AI/data preservation checks. Those checks do not replace gameplay testing. A complete start-to-finish playthrough, all 68 profiles and every mod combination have not been verified.

This is an offline single-player release for vanilla Scholar data. **Seeker of Fire compatibility is pending and will be a separate patch.** No multiplayer or anti-cheat compatibility claim is made.
