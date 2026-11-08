# Encounter memory — M01.1

Residents keep directional opinions as well as the shared trust imported from earlier saves. A person can regard someone as a rival while that person still considers them an acquaintance. Repeated shared interests can create friendship; insults, observed theft, violence and queue cutting create grievances. A valid meal handover, completed local paid trade or actual prisoner rescue can improve regard. These changes do not replace criminal evidence or authorize enforcement.

Each pair retains its sixteen most recent encounters: actual actor, time, event, applied opinion changes and which participants know about it. Trimming the display history does not reset the accumulated opinion. At the existing 100,000-contact soft cap, new conversations can replace old weak contacts; friendships, rivalries and household ties are protected. If every slot is protected, no new contact is invented. This is a bounded social model, not unlimited event retention; world-scale relationship storage is part of M29.

An unobserved theft is remembered by the thief only. It does not create a victim's grievance against an unknown named offender or expose that identity in the victim's history. A remote/unrecognized attack likewise does not reveal its source through social memory. Merely witnessing another pair's encounter does not create an interpersonal memory with its offender; observation/report communication follows the evidence system and [M01.2 communication](communication.md).

The My life conversation controls show your opinion, friendship/rivalry status and the three most recent known encounters with the nearby person, in English, French and Chinese. A short apology can begin repairing a grievance but cannot immediately erase an assault. Both participants' actual trust is respected when offering/accepting meal aid. Fallback estate candidates use the deceased's directional opinion rather than stale imported shared trust.

Directional regard and memories save/load with defaults for older native worlds. Historical shared trust remains unchanged as the migration baseline; older saves gain neither invented encounters nor a rewritten history. Death/departure retains past identities and memories. Validation rejects invalid actors, future/out-of-order events, oversized histories, hidden opinion changes and nonfinite/out-of-range regard.

`social_encounter_memory` covers real conversation, violence, hidden/known theft, paid trades, meal aid, old-field migration, interrupted/failed actions, persistence and inheritance. `vulkan_social_history` renders actual greeting/assault/apology results in an isolated paused world. For a localized visual check:

```bash
<build>/mega_dormitory --demo --floors 20 --population 40 --panel social --frames 3 \
  --menu-scroll 920 --lang fr --width 1000 --height 720 --capture /tmp/social.ppm
```

M01 remains active. Structured information transfer is delivered in **M01.2** and the connected personality/history choices are documented in [M01.3 decisions](decisions.md). The complete relationship/history interface and full milestone acceptance remain **M01.4**. These checkpoints do not declare the later work or the game complete.
