# Troubleshooting and bug reports

## F7 does nothing / no menu

Check that `Game/dinput8.dll` and `Game/NPCCompanions/NPCCompanions.dll` exist next to the correct `DarkSoulsII.exe` installation. Avoid an extra nested archive folder. Fully exit and relaunch after installing an update. Check `Game/NPCCompanions/NPCCompanions.log` for initialization or executable-signature failures.

If another loader owns `dinput8.dll`, copying one over the other does not make both work. Use an isolated installation while compatibility is investigated.

## Unsupported executable / initialization failed

The release is pinned to the executable fingerprint in [Installation](INSTALLATION.md). A modified/different executable or conflicting native patch can be refused. Preserve the log and report the error. Do not disable the signature checks or overwrite the game's executable with an unverified file.

## A companion does not appear

Load a character, stand on safe open ground, open F7 and wait for **Loading model and AI** to finish. Confirm the slot is active and click **Summon** if dismissed. Try **Bring to me** or F8 when active. A normal area load or bonfire transition can help distinguish a streaming problem; needing that repeatedly is a bug worth reporting.

Herald being unavailable **in Majula is intentional**. Use the original NPC there. Other companions are not subject to that Herald-specific restriction.

## A companion follows but will not fight/cast

Herald is intentionally peaceful. Other NPCs use native combat routines, which have their own range, cooldown, equipment and health conditions. A caster does not necessarily cast every moment, and most merchants do not have healing. Report a consistently inactive combat profile with its name, area and a reproducible enemy encounter. New merchant behavior still needs gameplay confirmation.

## Stuck, too much teleporting, or missed a boss transition

Try Bring to me/F8 from safe ground. Record whether the problem happens around ladders, lifts, narrow doors, fog walls or a particular boss. Include the NPC name: large/nonstandard bodies can behave differently. Do not describe recovery recreation as proof that the old actor survived streaming.

## Herald conversation or level-up problem

Use the current complete package, not a new DLL with old AI/dialogue assets. Close/cancel Talk if the interaction remains open. Mark the issue and report whether you used native **Talk → Level Up** or the F7 shortcut, whether you confirmed/cancelled, and whether movement returns. A previous stuck-kneeling repair is included, but further failures should be reported.

## Death/loading freeze or crash

Do not replace files while the game runs. Note the last active companions and the exact trigger: player death, companion death, bonfire, travel or area boundary. Preserve the current session journal and main log before another launch. Windows crash information can help distinguish a process crash from a loading hang; a log stopping alone does not identify the cause.

## My normal character/progress is missing

The mod uses a separate `.snc` save while active. Your ordinary `.sl2` is separate. An existing `.snc` is retained, so later changes made in a normal session are not repeatedly copied into it. Disable/undeploy the mod and relaunch to use ordinary saves. Keep backups; do not rename a mod save into a normal one.

## Seeker of Fire and other overhauls

**0.26 does not include a Seeker of Fire patch.** The mod redirects regulation, map and AI data. Combining separate overhauls requires a deliberate data merge and testing; changing load order alone is not a verified fix. Mention every other mod and loader in a bug report.

## Report template

Use [GitHub Issues](https://github.com/Lunaveli/DS2-NPC-Companions/issues) and include:

- NPC Companions version and installation method.
- Companion names/slots, area and nearest bonfire/boss.
- What happened, what you expected and repeatable steps.
- Whether it happens on a new character, after reload, and with other companions.
- Other mods/loaders, especially data overhauls.
- The relevant journal/main-log excerpt after pressing **Mark a problem**.

Logs can contain local paths/usernames and gameplay details. Review/redact them before posting. Do not include credentials, save files, full crash dumps or unrelated personal files in routine reports. The optional `log-report.py` can summarize a chosen journal if Python is already available; Python is not required to play.
