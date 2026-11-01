# Investigations, custody and recovered property

The native game records an act separately from a police report. New assaults, thefts, queue cutting, excessive force and homicides do not automatically reveal a live offender position to every warden. The trusted `report` helper is retained for old saves, internal formal reports and tests; gameplay uses `recordCrime`.

## Reports and searches

An awake, living nearby witness must have line of sight. At most twelve observations are stored per act, prioritizing observing wardens and the victim. A victim can report missing property without identifying its thief. A recorded identifying statement establishes a suspect; inspecting an unknown scene alone does not. Repeated inspection does not raise confidence repeatedly. A reported assault can provide the causal identity for a later lethal fall.

A phone/computer, an actual nearby visible warden, or an open jail counter is needed to file a complaint. Wardens can inspect scenes and take recorded eyewitness testimony only after reaching them. Public cases use last-seen positions, refreshed when an officer actually sees the suspect. Lost searches wait before retrying. Wardens respond to reported cases; queue cutting produces a warning rather than an arrest warrant.

My life → Law & recovered property lists known cases in pages. A resident sees their own victim/witness cases and reported allegations against them. Wardens, Manager and Cheat can view reported public cases. Unidentified perpetrators are not named by the case screen, job dispatch or administrative tickets. Crime journal notices omit actor names outside Cheat. The diary itself is still a building-wide event log, not a fully private communications simulation.

## Physical recovery

Seizure needs reach, line of sight, the correct case/property link and a secured holder with a warrant or custody record. Ordinary unrelated belongings cannot be confiscated using an arbitrary case number. Stolen items retain their original account owner; weapons and tools seized during arrest receive separate receipts. The officer carries the actual objects in inventory and must reach a jail desk to deposit them. Owners must walk to that desk to collect their own item, exactly once. Held weapons are not returned to a currently wanted owner.

A killed or departed carrier leaves recoverable physical evidence, retaining the same receipt. Collection does not create a second copy. Claims and seizure remain subject to item reservations, use and ownership. Evidence, testimony and progress persist across slots. Legacy reports retain their previously known suspect rather than silently becoming unidentified.

## Custody and escape

Escorts follow a continuous route with a time gap on ladders, instead of being offset sideways into open air. Both people must reach the jail; intake accepts the prisoner's actual nearby position, without snapping them onto the guard's coordinate or requiring them to penetrate a body blocking a final waypoint. Losing the guard interrupts the escort in place and reopens its charges.

Assault, theft, excessive force, escape and homicide have different sentence lengths. Jail care consumes real delivered stock, accumulates waste, and allows nighttime energy recovery. Empty jails cannot create rations. Non-custodial surrendered residents recover after a short period instead of remaining frozen forever.

An escape takes time and energy. A nearby attending on-shift warden can interrupt it and extend the sentence. Unattended escape retains the actual position and produces a custody alarm and wanted case. Some aggressive autonomous prisoners attempt escapes. This is a timed escape system, not a lock-picking or destructible-cell minigame.

## Limits

Jails still use a capacity-limited intake footprint, not individually constructed lockable cells. The system has deterministic real observations and custody receipts, not fingerprints, cameras, false testimony, a court/trial system, or a complete forensic evidence model. Officers do not yet perform broad search grids, autonomously discover every unwitnessed body, or travel to every distant unfiled witness. Physical restraints, deliberate prison assaults/rescues and full witness-protection behavior remain unfinished. Severe crimes currently change sentence length rather than implementing a complete justice policy editor.
