# Audio, controls and interaction feedback

## Player controls

Settings exposes independent master, effects and ambience volumes; mute; mute while unfocused; first-person field of view; and mouse sensitivity (100% is the default). Existing language and graphics controls now persist too. Preferences are separate from save slots, in `settings.json` under the active save directory. Normal launches debounce edits for 0.4 seconds and flush pending changes on shutdown. Writes use a temporary file and retain the preceding `.bak`. Corrupt or unsupported settings produce a translated notification and default controls; automatic backup recovery is not implemented for preferences. World saves retain their existing recovery rules.

Demos, bounded frame captures, skin/compute checks and scripted playthroughs do not read or write the player's preferences. `--preferences-file /path/to/settings.json` allows an isolated capture to read a supplied configuration. Automated runs still do not write it. `--no-audio` disables device initialization. `MEGA_AUDIO_DEVICE=null` exercises output without playback; the default is the desktop audio device.

## Native sound

`mega_presentation` contains an original 48 kHz stereo synthesizer and mixer. At most 48 voices play, including at most 32 persistent nearby sources. Sources have stable identities, distance falloff, equal-power pan and smoothed gains; obstructed sources are muffled. The output is limited before conversion to signed 16-bit samples. World pause fades environmental sounds while UI feedback remains available. Resetting a world clears old voices and event history. Audio controls never modify the simulation; muting alone does not pause game time. Master mute and background muting silence only output.

Actual nearby walking/climbing drives footstep/ladder cadence. Only nearby operational machinery, active washing/water service, moving lifts/vehicles and fires emit their corresponding loops. Local observed pickup/delivery, melee and shot events add short effects. This is a bounded presentation of nearby gameplay, not an acoustic propagation simulation. No speech, recorded Foley, music, reverberation or device selector has been added.

Linux loads `libasound.so.2` at runtime, uses nonblocking interleaved playback on a dedicated thread, handles underruns and closes the device on stop. Missing/failed output leaves the game playable and exposes Retry in Settings. The ALSA runtime is optional; its headers are not a build dependency. Other platforms currently use silent output. Public ALSA API references: https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m.html and https://alsa-project.org/alsa-doc/alsa-lib/pcm_8h.html . The synthesis code and sounds are original project work; no downloaded sound licence is needed.

## Physical presentation

The E-key handler and prompt resolve one shared nearby interaction target. The prompt identifies owned loose belongings, vehicles, ladders, lift access, rented storage and services. Service hints include real prices (including cents), waiting counts or access/staffing state. Computer access displays its thirty-minute charge. Facility and item targeting checks clear sight/reach; real authorization and payment checks remain in the simulation. Menus may still be opened for services that cannot currently serve. Body transport, captivity and other simulation restrictions remain authoritative.

Multilingual prompt backgrounds use the same wrap calculation as the text. Enabled button activations generate one interface sound per frame; hidden, disabled and duplicate overlapping clicks do not.

Pickup feedback lasts under a second, using a ground crouch/reach or standing locker handover. A visible nearby object moves briefly toward the hand and shrinks into inventory. It does not create a second physical object. AI collection routes end beside the item instead of on the item/body centre, and low reach remains blocked by walls.

Nearby remains use male/female character meshes with a death-time clothing snapshot and a still lying pose. Freshly observed deaths blend briefly into this pose; loading an old journal does not replay a collapse. Carried/stored bodies remain lying and moving lift bodies follow their platform. The CPU skinning path supports the same poses. Distant/capped remains use simpler geometry. These changes do not implement ragdolls, floor-contact IK, dismemberment or cloth simulation.

Journal entries now carry a monotonically increasing presentation serial, independent of the simulation event clock. Identical actions at paused game time remain distinguishable; trimmed journal entries cannot cause a replay. Saves preserve serials and body appearance. Older saves receive serials on load and derive available body clothing from their retained outfit.

## Validation and remaining work

Offline checks cover block-independent effects, silence, source falloff/panning, pause/mute/bus isolation, bounded voices, reset, preference normalization/round trips/corruption, native device stop/restart, multilingual measured wrapping and real button activations. Focused simulation checks cover journal/save migration, clothing snapshots, reachable pickup approaches and wall obstruction. The rendered inheritance workflow verifies actual pickup, visible rigged remains, walking sound sources and audio output with the null sink. Vulkan skinning tests include the new poses.

This establishes working audio and interaction feedback. Reference-quality interiors and characters, modular garments, imported animation clips, contact IK, recorded audio, comprehensive accessibility/input QA and a complete production release remain unfinished.
