# Investigations, custody and recovered property

The native game records an act separately from a police report. New assaults, thefts, queue cutting, excessive force and homicides do not automatically reveal a live offender position to every warden. The trusted `report` helper is retained for old saves, internal formal reports and tests; gameplay uses `recordCrime`.

## Reports and searches

An awake, living nearby witness must have line of sight. At most twelve direct observations are stored per act, prioritizing observing wardens and the victim. Subsequent body discoveries share a total limit of 24 observations; an unfiled duplicate body-only account can be replaced so a later reporting officer is not blocked by a full list. Direct eyewitness accounts are retained. A victim can report missing property without identifying its thief. A recorded identifying statement establishes a suspect; inspecting an unknown scene alone does not. Repeated inspection does not raise confidence repeatedly. A reported assault can provide the causal identity for a later lethal fall.

A phone/computer, an actual nearby visible warden, or an open jail counter is needed to file a complaint. Wardens can inspect scenes and take recorded eyewitness testimony only after reaching them. Public cases use last-seen positions, refreshed when an officer actually sees the suspect. Lost searches wait before retrying. Wardens respond to reported cases; queue cutting produces a warning rather than an arrest warrant.

Visible, accessible remains within four metres can be examined from My life → Law & recovered property. This records the actual found location, without identifying a killer. Wardens on duty, medics and cleaners can make nearby discoveries; undertakers also attempt a report at physical collection. A manually controlled player chooses whether to report. Walls, private morgue access, bodies in ambulances/lifts and unreachable or cremated remains prevent discovery. Bodies must also be within vertical reach. A remembered account needs a working communication channel before it becomes a police report; sleeping residents cannot file one. A natural death alone does not create a homicide case.

Original crime locations and reported scenes are separate saved state. If a body was moved before discovery, police, dispatch and the public case screen use the discovered floor, rather than a hidden crime coordinate. Body-only testimony cannot establish an offender's identity or warrant.

My life → Law & recovered property lists known cases in pages. A resident sees their own victim/witness cases and reported allegations against them. Wardens, Manager and Cheat can view reported public cases. Unidentified perpetrators are not named by the case screen, job dispatch or administrative tickets. Crime journal notices omit actor names outside Cheat. The diary itself is still a building-wide event log, not a fully private communications simulation.

## Physical recovery

Seizure needs reach, line of sight, the correct case/property link and a secured holder with a warrant or custody record. Ordinary unrelated belongings cannot be confiscated using an arbitrary case number. Stolen items retain their original account owner; weapons and tools seized during arrest receive separate receipts. The officer carries the actual objects in inventory and must reach a jail desk to deposit them. Owners must walk to that desk to collect their own item, exactly once. Company controllers, leaders and legitimate institutional facility managers can collect authorized institutional property; its title remains with the company or dormitory. A manager cannot claim an employer's personal possessions merely because the same personal account funds their shop. Held weapons are not returned to a currently wanted owner.

A killed or departed carrier leaves recoverable physical evidence, retaining the same receipt. Collection does not create a second copy. Claims and seizure remain subject to item reservations, use and ownership. Evidence, testimony and progress persist across slots. Legacy reports retain their previously known suspect rather than silently becoming unidentified.

## Custody and escape

Escorts follow a continuous route with a time gap on ladders, instead of being offset sideways into open air. Both people must reach the jail; intake accepts the prisoner's actual nearby position, without snapping them onto the guard's coordinate or requiring them to penetrate a body blocking a final waypoint. Losing the guard interrupts the escort in place and reopens its charges.

Assault, theft, excessive force, escape and homicide have different sentence lengths. Jail care consumes real delivered stock, accumulates waste, and allows nighttime energy recovery. Empty jails cannot create rations. Non-custodial surrendered residents recover after a short period instead of remaining frozen forever.

An escape takes time and energy. A nearby attending on-shift warden can interrupt it and extend the sentence. Unattended escape retains the actual position and produces a custody alarm and wanted case. Some aggressive autonomous prisoners attempt escapes. This is a timed escape system, not a lock-picking or destructible-cell minigame.

A nearby free adult with usable personally owned tools can attempt to free a captive from the conversation controls. Five game minutes of continuous work within two metres, with line of sight, consume tool condition and energy. Movement, missing/broken tools, custody ending, combat, transport or urgent survival needs cancel the attempt. An alert on-shift warden within sight can stop it and report the observed offender; a hidden guard cannot see through a wall. Fighting/sleeping guards do not monitor these attempts.

Successful release keeps the actual prisoner position, removes the active custody restraint and produces the existing escape alarm/warrant. The rescuer's separate custody-interference offense needs real witness evidence; it is not automatically broadcast. Existing criminal charges are not erased. Rescue progress, tool identity and the custody restraint flag persist through saves. These are timed interventions in the existing intake/escort system, not a complete lock-picking or destructible-cell minigame.

## Limits

Jails still use a capacity-limited intake footprint, not individually constructed lockable cells. The system has deterministic real observations and custody receipts, not fingerprints, cameras, false testimony, a court/trial system, or a complete forensic evidence model. Discovery is local to real nearby actors; officers do not yet perform broad search grids or travel to every distant unfiled witness. Broad civilian search behavior and a complete forensic determination of cause of death remain unfinished. Standalone restraint equipment and its complete physical constraints, deliberate prison assaults, richer rescue coordination and full witness-protection behavior remain unfinished. Severe crimes currently change sentence length rather than implementing a complete justice policy editor.

## Capacity during booking

New arrests select the nearest open, undamaged and safe jail with an unreserved place; opening hours are respected. Residents already jailed or being escorted reserve capacity. A warden bringing a prisoner provides the officer presence, so destination selection does not require the jail desk to be staffed before the escort arrives. If all safe places are full, automatic officers retain the warrant and defer another booking attempt for five game minutes, without teleporting or claiming to detain the suspect. They may resume when real capacity opens. This is aggregate jail capacity, not individual lockable cells or a court system.
