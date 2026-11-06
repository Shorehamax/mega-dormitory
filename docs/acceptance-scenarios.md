# Release acceptance scenarios — M00

These are required checks, not passing results. Each scenario has an owner and actual failure/recovery expectations. Run isolated worlds and keep the exact seed, build, mode, language, hardware and evidence. Existing automated suites are starting evidence, not a replacement for the full scenario.

For final acceptance expand the six whole-game routes in the [completion plan](completion-plan.md#required-whole-game-release-scenarios) using these checks. Use distinct test save roots; do not use player slots.

<a id="ac01"></a>

## AC01 — Dormitory-focused dystopia, reference architecture, lobby and crowded galleries

Requirement: **R01** · Owners: **M00, M22, M26** · Baseline: **partial**

**Procedure:** Inspect a default lobby, residential gallery and commercial floor at eye height, day and night; walk every connecting route.

**Pass condition:** Crowding and architecture match the recorded references while all occupied areas remain supported and reachable.

**Inspect:** [src/world.cpp](../src/world.cpp), [include/mega/layout.hpp](../include/mega/layout.hpp), [src/application.cpp](../src/application.cpp)

<a id="ac02"></a>

## AC02 — Configurable 2,000-floor / 100,000-resident world; 512 m standard and 640 m entrance footprints

Requirement: **R02** · Owners: **M00, M22, M28, M29** · Baseline: **partial**

**Procedure:** Generate defaults and alternate dimensions; save/reload; expand an occupied floor with a funded crew.

**Pass condition:** 2,000 floors/100,000 identities and 512/640 m defaults persist; expansion consumes actual materials/work without moving existing titles.

**Inspect:** [src/world.cpp](../src/world.cpp), [src/housing_work.cpp](../src/housing_work.cpp), [src/construction.cpp](../src/construction.cpp)

<a id="ac03"></a>

## AC03 — Uneven residential, service, office and commercial floors; scarce interactive facilities

Requirement: **R03** · Owners: **M00, M04, M15, M22** · Baseline: **partial**

**Procedure:** Visit housing-only, service and commercial floors; attempt each service with valid and missing staff, supplies and access.

**Pass condition:** Uneven scarcity is visible; every advertised usable facility performs its actual action or explains its real unavailable prerequisite.

**Inspect:** [src/world.cpp](../src/world.cpp), [src/operations.cpp](../src/operations.cpp), [src/education.cpp](../src/education.cpp)

<a id="ac04"></a>

## AC04 — Continuous locomotion, queues, ladders, safe support, sleeping on beds and fitness

Requirement: **R04** · Owners: **M07, M08, M10, M18, M24, M27, M29** · Baseline: **partial**

**Procedure:** Walk/run through a crowd; queue; climb and dismount; sleep in both bunks; jump/fall; reload during movement.

**Pass condition:** No unsupported standing, body penetration, teleportation or camera shaking; injuries, fitness and poses agree with physical state.

**Inspect:** [src/movement.cpp](../src/movement.cpp), [src/queues.cpp](../src/queues.cpp), [src/airborne.cpp](../src/airborne.cpp), [src/routines.cpp](../src/routines.cpp)

<a id="ac05"></a>

## AC05 — Survival, Manager, Cheat, possession/inspection, AI owner and ownership takeover

Requirement: **R05** · Owners: **M00, M25, M28, M31** · Baseline: **partial**

**Procedure:** Start each mode, switch permitted viewpoints, inspect/possess, edit a supported rule, then reload.

**Pass condition:** Survival presents essentials; Manager has owner authority; Cheat supports the complete bounded editor without corrupting accounts.

**Inspect:** [src/application.cpp](../src/application.cpp), [src/governance.cpp](../src/governance.cpp), [src/social.cpp](../src/social.cpp)

<a id="ac06"></a>

## AC06 — Move-in/out, death/empty-world game over, retirement and succession

Requirement: **R06** · Owners: **M17, M25, M28, M31** · Baseline: **partial**

**Procedure:** Move in; depart in Survival/Manager; empty a world; refuse a sale; trigger retirement and a contested takeover.

**Pass condition:** Correct runs end with explained causes; refusal is real and successor/contested ownership preserves existing obligations.

**Inspect:** [src/society.cpp](../src/society.cpp), [src/governance.cpp](../src/governance.cpp), [src/routines.cpp](../src/routines.cpp)

<a id="ac07"></a>

## AC07 — Named slots, backups, recovery, migrations and browser feature parity

Requirement: **R07** · Owners: **M00, M03, M25, M28, M32** · Baseline: **partial**

**Procedure:** Load native and browser fixtures, finish prepaid work, save a separate named slot, corrupt its primary and recover the backup.

**Pass condition:** Identities, property, escrow, active tasks and required browser behaviour continue once without losing the source.

**Inspect:** [src/persistence.cpp](../src/persistence.cpp), [src/browser_import.cpp](../src/browser_import.cpp), [include/mega/runtime_paths.hpp](../include/mega/runtime_paths.hpp)

<a id="ac08"></a>

## AC08 — Stable identities, age/personality/preferences and English/French/Chinese display names

Requirement: **R08** · Owners: **M01, M03, M15, M23** · Baseline: **partial**

**Procedure:** Follow the same resident through language switches, relationships, age milestones, clothing changes and reload.

**Pass condition:** All references retain identity and appropriate age/personality choices; no duplicate or substituted resident is created.

**Inspect:** [src/world.cpp](../src/world.cpp), [src/social.cpp](../src/social.cpp), [src/characters.cpp](../src/characters.cpp)

<a id="ac09"></a>

## AC09 — Complete Chinese, English and French UI, text input and readable layouts

Requirement: **R09** · Owners: **M25, M28, M31, M32** · Baseline: **partial**

**Procedure:** In each language enter/search a name using relevant keyboard/IME; exercise every screen at small and large supported sizes.

**Pass condition:** Names and controls remain readable, editable and reachable without clipping, hidden buttons or broken focus.

**Inspect:** [src/application.cpp](../src/application.cpp), [src/ui.cpp](../src/ui.cpp), [assets/locales/en.json](../assets/locales/en.json), [assets/locales/fr.json](../assets/locales/fr.json), [assets/locales/zh.json](../assets/locales/zh.json)

<a id="ac10"></a>

## AC10 — Independent schedules, night shifts, sleep, meals, work, school and leisure

Requirement: **R10** · Owners: **M01, M02, M03, M12, M15, M20, M30** · Baseline: **partial**

**Procedure:** Observe day workers, night workers, students and carers for several days; remove their normal service or workplace.

**Pass condition:** Actors visibly attend real activities, sleep on beds and replan needs/commutes without permanent task-failure loops.

**Inspect:** [src/routines.cpp](../src/routines.cpp), [src/autonomy.cpp](../src/autonomy.cpp), [src/education.cpp](../src/education.cpp)

<a id="ac11"></a>

## AC11 — Communication, relationships, households, families and dependent care

Requirement: **R11** · Owners: **M01, M02, M03, M15, M17, M19** · Baseline: **partial**

**Procedure:** Build friendship, cause a grievance, communicate a witnessed fact, form a consenting household and interrupt a care errand.

**Pass condition:** History changes later choices; only acquired information is known and real family/care commitments survive interruption/reload.

**Inspect:** [src/social.cpp](../src/social.cpp), [src/households.cpp](../src/households.cpp), [src/economy.cpp](../src/economy.cpp), [src/browser_import.cpp](../src/browser_import.cpp)

<a id="ac12"></a>

## AC12 — Personal items, shared/private storage, provisioning, wear, washing and clothes changes

Requirement: **R12** · Owners: **M02, M05, M08, M22, M23, M24** · Baseline: **partial**

**Procedure:** Purchase and deliver supplies; store/share/retrieve them; cook, wash and change clothes; attempt unauthorized access.

**Pass condition:** Each item has one title/location; reservations, dirt/wear and ownership remain valid after theft, cancellation and reload.

**Inspect:** [src/personal_items.cpp](../src/personal_items.cpp), [src/storage.cpp](../src/storage.cpp), [src/washing.cpp](../src/washing.cpp), [src/economy.cpp](../src/economy.cpp)

<a id="ac13"></a>

## AC13 — Own/buy/rent/sell homes and possessions; theft and robbery consequences

Requirement: **R13** · Owners: **M01, M02, M06, M11, M17, M18, M23, M31** · Baseline: **partial**

**Procedure:** Rent/buy/list/sell an occupied home and a personal item; attempt theft; recover property through real custody.

**Pass condition:** Leases, meeting distance, consent, payment and provenance prevent free transfers or remote handovers.

**Inspect:** [src/economy.cpp](../src/economy.cpp), [src/social.cpp](../src/social.cpp), [src/society.cpp](../src/society.cpp), [src/security.cpp](../src/security.cpp)

<a id="ac14"></a>

## AC14 — Food, water, toilets, showers, sinks, laundry, waste and repair facilities

Requirement: **R14** · Owners: **M20, M21, M22** · Baseline: **partial**

**Procedure:** Visit all daily-life services; exhaust staff, stock and waste capacity; close/reopen and repair a service.

**Pass condition:** Unstaffed or unsupplied services stop appropriately; unattended permitted services continue; no stock or sanitation disappears for free.

**Inspect:** [src/operations.cpp](../src/operations.cpp), [src/washing.cpp](../src/washing.cpp), [src/repairs.cpp](../src/repairs.cpp), [src/routines.cpp](../src/routines.cpp)

<a id="ac15"></a>

## AC15 — Sparse pools, arcades, playgrounds, cinemas, gardens, libraries, gyms and preference-driven recreation

Requirement: **R15** · Owners: **M02, M22** · Baseline: **partial**

**Procedure:** Give contrasting preferences access to a cinema, food venue, gym/pool and shop with differing queues/prices.

**Pass condition:** Residents choose explainably different affordable activities; recreation uses actual capacity and affects happiness.

**Inspect:** [src/world.cpp](../src/world.cpp), [src/routines.cpp](../src/routines.cpp), [src/operations.cpp](../src/operations.cpp)

<a id="ac16"></a>

## AC16 — Long real queues, capacity, prices, supplies, opening hours, workers and interruptions

Requirement: **R16** · Owners: **M02, M10, M12, M15, M25, M30** · Baseline: **partial**

**Procedure:** Join/cut a scarce service line; remove a worker; resume a prepaid visit; block and later clear the route.

**Pass condition:** Physical queue order and paid progress persist, cutting can cause consequences, and benign changes do not duplicate charges or strand tasks.

**Inspect:** [src/queues.cpp](../src/queues.cpp), [src/operations.cpp](../src/operations.cpp), [src/routines.cpp](../src/routines.cpp)

<a id="ac17"></a>

## AC17 — Conserved accounts, migration capital, happiness and causal economic failure

Requirement: **R17** · Owners: **M02, M05, M06, M11, M16, M17, M20, M30** · Baseline: **partial**

**Procedure:** Track every account while residents arrive/leave, buy/work/build, die, receive refunds and suffer shortages.

**Pass condition:** Internal changes net to zero except explicit external entries; happiness/migration and game over follow actual economic conditions.

**Inspect:** [src/economy.cpp](../src/economy.cpp), [src/world.cpp](../src/world.cpp), [src/autonomy.cpp](../src/autonomy.cpp), [src/companies.cpp](../src/companies.cpp)

<a id="ac18"></a>

## AC18 — Property premiums based on actual service/job/transport access, crowding, risk and condition

Requirement: **R18** · Owners: **M17, M30** · Baseline: **partial**

**Procedure:** Compare equivalent homes before/after service closure, transport access changes, crowding and damage; keep an active lease.

**Pass condition:** Price changes reflect measured accessibility/conditions and never rewrite an existing lease by accident.

**Inspect:** [src/economy.cpp](../src/economy.cpp), [src/housing_work.cpp](../src/housing_work.cpp), [src/estates.cpp](../src/estates.cpp)

<a id="ac19"></a>

## AC19 — Live layout editing, beds/facilities/floors, expansion, planners, foremen and paid construction

Requirement: **R19** · Owners: **M04, M05, M06, M07, M16, M21, M24, M31** · Baseline: **partial**

**Procedure:** Plan a mixed interior, partial slab edit and extension; survey, quote, fund, deliver, build, cancel and reopen.

**Pass condition:** Preview and actual geometry/nav/support agree; site access, paid work and real materials gate progress and payer title.

**Inspect:** [src/structures.cpp](../src/structures.cpp), [src/construction.cpp](../src/construction.cpp), [src/crew_contracts.cpp](../src/crew_contracts.cpp), [src/housing_work.cpp](../src/housing_work.cpp)

<a id="ac20"></a>

## AC20 — Ownership/leases, compensation, negotiation, physical eviction, resistance and casualties

Requirement: **R20** · Owners: **M04, M05, M06, M17** · Baseline: **partial**

**Procedure:** Attempt ordinary and forced projects on rented, owned, shared-storage and commercial spaces with insufficient funds/refusal.

**Pass condition:** Rights and funded compensation are preserved; forced action requires actual actors and can cause resistance/crime/casualties.

**Inspect:** [src/construction.cpp](../src/construction.cpp), [src/housing_work.cpp](../src/housing_work.cpp), [src/structures.cpp](../src/structures.cpp)

<a id="ac21"></a>

## AC21 — Companies, workspace, hiring, team direction, jobs, wages, investment and real profit

Requirement: **R21** · Owners: **M02, M05, M11, M12, M13, M14, M16, M25, M30, M31** · Baseline: **partial**

**Procedure:** Found/rent/staff a company, assign work, meet a client contract, pay wages and grow or default.

**Pass condition:** Profit comes from completed funded work, workspace and authority stay valid, and losses/claims persist across ownership changes.

**Inspect:** [src/companies.cpp](../src/companies.cpp), [src/office.cpp](../src/office.cpp), [src/equity.cpp](../src/equity.cpp), [src/autonomy.cpp](../src/autonomy.cpp)

<a id="ac22"></a>

## AC22 — Meaningful programming, administration and video-editing work

Requirement: **R22** · Owners: **M13, M14** · Baseline: **partial**

**Procedure:** Play each discipline against a concrete brief; submit poor/good revisions, delegate and miss a deadline.

**Pass condition:** Controls and scoring are discipline-specific; actual quality, time, staffing and client funds determine settlement.

**Inspect:** [src/office.cpp](../src/office.cpp), [src/administration.cpp](../src/administration.cpp)

<a id="ac23"></a>

## AC23 — Actual delivery, construction, sanitation, repair, retail, teaching, medicine, piloting and security work

Requirement: **R23** · Owners: **M05, M07, M09, M10, M11, M12, M13, M14, M16, M19, M20, M21, M24, M27** · Baseline: **partial**

**Procedure:** Play each physical job from paid offer to pickup/tools, travel, on-site action, delivery/result and wages; interrupt it mid-route.

**Pass condition:** No timer awards remote work; people, equipment, client goods and funds reach their actual required places exactly once.

**Inspect:** [src/economy.cpp](../src/economy.cpp), [src/operations.cpp](../src/operations.cpp), [src/medicine.cpp](../src/medicine.cpp), [src/security.cpp](../src/security.cpp), [src/crew_contracts.cpp](../src/crew_contracts.cpp)

<a id="ac24"></a>

## AC24 — Staffed education, school schedules, learning and career qualifications

Requirement: **R24** · Owners: **M03, M12, M15** · Baseline: **partial**

**Procedure:** Attend/teach school with capacity/material/staff limits, finish exercises, miss lessons and apply a qualification to work.

**Pass condition:** Knowledge requires real attendance/results and affects legitimate opportunities; absent teaching cannot grant a diploma.

**Inspect:** [src/education.cpp](../src/education.cpp), [src/routines.cpp](../src/routines.cpp)

<a id="ac25"></a>

## AC25 — Devices, paid public/work computers and polished simulated DORMNET commerce/messages/work/news

Requirement: **R25** · Owners: **M13, M14, M25, M31** · Baseline: **partial**

**Procedure:** Use owned phone/laptop, employee computer and paid public terminal; lose access during work/order/message actions.

**Pass condition:** Access and charges are real; offline/remote access does not fabricate information, stock or delivery and screens stay usable.

**Inspect:** [src/economy.cpp](../src/economy.cpp), [src/application.cpp](../src/application.cpp)

<a id="ac26"></a>

## AC26 — Melee/firearms, injuries/fatigue, surrender, lethal choices and causal falling/shoving

Requirement: **R26** · Owners: **M06, M07, M08, M09, M18, M19, M21, M24, M27, M30** · Baseline: **partial**

**Procedure:** Fight, defend, surrender, execute, shove and fall with cover/bystanders, then seek care and reload.

**Pass condition:** Hits respect reach/support, helpless killing is severe, injuries affect action and culpability persists.

**Inspect:** [src/society.cpp](../src/society.cpp), [src/combat.cpp](../src/combat.cpp), [src/airborne.cpp](../src/airborne.cpp)

<a id="ac27"></a>

## AC27 — Witness-based responsibility, self-defence, investigations, arrests, cells, escapes and police misconduct

Requirement: **R27** · Owners: **M01, M06, M11, M18, M19, M30** · Baseline: **partial**

**Procedure:** Report witnessed/unwitnessed offences, investigate, arrest/escort, book into a real cell, escape/rescue and recover property.

**Pass condition:** Unknown facts stay unknown; every arrest/custody/escape uses actual actors, cell space and known evidence.

**Inspect:** [src/security.cpp](../src/security.cpp), [src/death_reporting.cpp](../src/death_reporting.cpp), [src/prison_rescues.cpp](../src/prison_rescues.cpp)

<a id="ac28"></a>

## AC28 — Internal infirmaries, staffed care, ambulance hangars/crews, paid tiers and armed response

Requirement: **R28** · Owners: **M03, M10, M18, M20, M24, M27, M30** · Baseline: **partial**

**Procedure:** Dispatch each tier under active violence, missing crews/fuel/space and traffic; treat after physical handover.

**Pass condition:** Tier priority improves real available response, never instant rescue; equipment/staff/payment and patient location remain causal.

**Inspect:** [src/medicine.cpp](../src/medicine.cpp), [src/transport.cpp](../src/transport.cpp)

<a id="ac29"></a>

## AC29 — Real corpse pickup, capacity-limited morgues and cremation with costs/sanitation

Requirement: **R29** · Owners: **M03, M07, M19, M20, M21, M24, M27** · Baseline: **partial**

**Procedure:** Find a body, notify workers, transport to a full/available morgue, cremate with supplies and settle the estate.

**Pass condition:** One body and one title/debt history persist; no remote disposal, duplicated inheritance or unpaid cremation.

**Inspect:** [src/medicine.cpp](../src/medicine.cpp), [src/death_reporting.cpp](../src/death_reporting.cpp), [src/estates.cpp](../src/estates.cpp)

<a id="ac30"></a>

## AC30 — Rare disease/accidents, fire, smoke, property damage and disaster response

Requirement: **R30** · Owners: **M05, M07, M20, M21, M30** · Baseline: **partial**

**Procedure:** Cause a rare bounded outbreak/fire, operate barriers, evacuate, lose goods, treat casualties, clean and fund repairs.

**Pass condition:** Traceable hazards respect geometry; losses, response and recovery consume real resources and never replace assets for free.

**Inspect:** [src/emergencies.cpp](../src/emergencies.cpp), [src/fire_spread.cpp](../src/fire_spread.cpp), [src/fire_transport.cpp](../src/fire_transport.cpp)

<a id="ac31"></a>

## AC31 — Expensive vehicles, stores, scarce paid parking, permission, fuel and maintenance

Requirement: **R31** · Owners: **M08, M09, M10, M11, M23** · Baseline: **partial**

**Procedure:** Buy with/without parking, drive loaded, collide/run out of fuel, request repair/tow and attempt an unauthorized drive.

**Pass condition:** Purchase grants no free space; handling/load/permission matter and paid physical work recovers the actual vehicle.

**Inspect:** [src/vehicles.cpp](../src/vehicles.cpp), [src/vehicle_services.cpp](../src/vehicle_services.cpp), [src/economy.cpp](../src/economy.cpp)

<a id="ac32"></a>

## AC32 — Physical ramps, selected-floor paid/subscribed lifts, transport queues and towing

Requirement: **R32** · Owners: **M04, M08, M09, M10, M11, M29** · Baseline: **partial**

**Procedure:** Queue at a sparse paid lift, board/ride/exit, take a loaded vehicle over a ramp/platform and interrupt/cancel transport.

**Pass condition:** Fares/subscriptions/whitelists and capacity apply; no teleportation, clipped support or duplicated occupancy.

**Inspect:** [src/transport.cpp](../src/transport.cpp), [src/roads.cpp](../src/roads.cpp), [src/vehicle_services.cpp](../src/vehicle_services.cpp)

<a id="ac33"></a>

## AC33 — Custom C++ engine, Vulkan, suitable GPU parallel work and CPU/software fallback

Requirement: **R33** · Owners: **M26, M28, M29, M32** · Baseline: **partial**

**Procedure:** Run supported hardware and CPU presets on the same dense world; compare rules and profile parallel workloads.

**Pass condition:** Both paths preserve validated simulation/accounting; documented performance and responsiveness budgets are measured.

**Inspect:** [src/renderer.cpp](../src/renderer.cpp), [src/gpu_skinning.cpp](../src/gpu_skinning.cpp), [src/world.cpp](../src/world.cpp), [CMakeLists.txt](../CMakeLists.txt)

<a id="ac34"></a>

## AC34 — Licensed assets, multiple formats, modular people/clothes/beds/closets/vehicles

Requirement: **R34** · Owners: **M09, M22, M23, M26, M29, M32** · Baseline: **partial**

**Procedure:** Load each supported format and malformed input; fit/modify owned people, beds, closets and vehicles through paid work.

**Pass condition:** Imports fail clearly; licensed components persist, alter visible geometry and obey ownership rather than free palette-only upgrades.

**Inspect:** [src/assets.cpp](../src/assets.cpp), [src/characters.cpp](../src/characters.cpp), [src/customization.cpp](../src/customization.cpp), [assets/models/manifest.json](../assets/models/manifest.json)

<a id="ac35"></a>

## AC35 — Good lighting/materials/shadows, stable rendering and polished interaction animation

Requirement: **R35** · Owners: **M22, M23, M24, M26, M27, M29** · Baseline: **partial**

**Procedure:** Inspect all reference scenes in motion while walking, climbing, sleeping, using tools, fighting and carrying.

**Pass condition:** Good lighting/material scale and correct contacts remain stable under camera rotation and interruption.

**Inspect:** [src/renderer.cpp](../src/renderer.cpp), [src/characters.cpp](../src/characters.cpp), [src/application.cpp](../src/application.cpp)

<a id="ac36"></a>

## AC36 — Production UI, sound, input/accessibility and platform-ready packaging

Requirement: **R36** · Owners: **M25, M26, M27, M28, M31, M32** · Baseline: **partial**

**Procedure:** Use complete loops with keyboard/mouse at supported sizes/languages, mute/unplug audio, relocate install and recover saves.

**Pass condition:** Critical controls remain discoverable/reachable, cues work without audio, and supported clean player installs need no compiler.

**Inspect:** [src/ui.cpp](../src/ui.cpp), [src/application.cpp](../src/application.cpp), [src/audio.cpp](../src/audio.cpp), [src/preferences.cpp](../src/preferences.cpp), [tools/package_candidate.py](../tools/package_candidate.py)

<a id="ac37"></a>

## AC37 — Shares, acquisitions, deeper company finance, insolvency and personal/corporate inheritance

Requirement: **R37** · Owners: **M16, M17** · Baseline: **partial**

**Procedure:** Trade/finance/merge businesses, default with creditors, die with disputed assets and receive a late refund.

**Pass condition:** Authority/claims/assets transfer once with priority, consent and actual funds; debts and leases never disappear.

**Inspect:** [src/equity.cpp](../src/equity.cpp), [src/companies.cpp](../src/companies.cpp), [src/estates.cpp](../src/estates.cpp)

<a id="ac38"></a>

## AC38 — Finished playable loops, long-run scarcity balance, supported hardware and shipping quality

Requirement: **R38** · Owners: **M00, M28, M29, M30, M31, M32** · Baseline: **partial**

**Procedure:** Run the M30 seed matrix, M31 whole-game scenarios and M32 clean-install release procedure against the exact candidate.

**Pass condition:** All accepted gates pass with retained evidence, no P0/P1 defect remains and the exact shipped packages are verified.

**Inspect:** [docs/status.md](status.md), [docs/releasing.md](releasing.md), [tools/package_candidate.py](../tools/package_candidate.py)

## Baseline smoke-check recipes

```bash
ctest --test-dir <build> -R "^(browser_migration_and_personal_items|resident_autonomy|relocatable_runtime_paths)$" --output-on-failure
<build>/mega_dormitory --headless --mode cheat --autoplay --floors 2000 --population 100000 --seconds 0
<build>/mega_dormitory --headless --mode cheat --autoplay --floors 40 --population 1000 --seconds 3600 --save /tmp/mega-audit.mdsave
<build>/mega_dormitory --playtest-daily-life --lang en --width 1000 --height 720 --capture /tmp/mega-audit.ppm
```

The rendered recipe is scripted in-game play with real geometry/actions. A display and Vulkan driver are required. Hardware/software and language variants remain necessary for relevant later changes. The full M30/M31/M32 release gates exceed these baseline smoke checks.
