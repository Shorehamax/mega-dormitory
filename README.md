# Mega Dormitory — native development build

A C++20 dormitory society simulation with a custom Vulkan renderer, UI, navigation, movement and game rules. This is a playable development build, **not the complete requested production game**. The browser prototype is preserved separately. The accepted design is recorded in [requirements](docs/requirements.md). Follow the [ordered completion plan](docs/completion-plan.md) and its [progress tracker](docs/completion-progress.json) for remaining work; [implementation status](docs/status.md) records historical evidence.

## Run on Linux

Clone the repository and start the native game:

```bash
git clone git@github.com:Shorehamax/mega-dormitory.git
cd mega-dormitory
bash run.sh
```

The launcher builds into `$XDG_CACHE_HOME/mega-dormitory/native` (normally `~/.cache/mega-dormitory/native`). Source, art and save slots stay in this project. `MEGA_BUILD_DIR` overrides the cache and `MEGA_BUILD_JOBS` controls build concurrency. Keeping builds in the cache also supports source checkouts on filesystems with executable or metadata limitations.

Requires a C++20 compiler, CMake 3.24+, Ninja, Vulkan headers/loader, GLFW 3.3+, Zstandard and zlib development files, and `glslc`. Rendering, UI and simulation are project code; small utility libraries provide window/input, font/image/mesh decoding, serialization and compression.

```bash
bash run.sh --demo --floors 20 --population 600
bash run.sh --software                           # Mesa CPU Vulkan fallback
bash run.sh --demo --panel programming --lang fr
bash run.sh --headless --mode cheat --autoplay --floors 200 --population 10000 --seconds 86400
bash test.sh
```

The software option uses lavapipe and selects X11 when available on this Linux installation. Headless simulation needs no GPU. The renderer currently requires Vulkan even on the software path.

## Start and controls

- New game exposes Survival, Manager and Cheat; seed, population, floor count, scarcity, starting cash and day length; and independent standard-floor/entrance width and depth. Defaults are **2,000 floors, 100,000 people, 512 × 512 m ordinary floors and a 640 × 640 m entrance**. There are 256 physical beds per residential floor; excessive populations are rejected.
- The world starts paused with the localized **H controls guide**. Resume before walking or climbing. The guide can route you to reception or a ladder.
- **V** switches first person / observer. In free view, WASD pans relative to the camera, Q/E descends/ascends smoothly, Shift moves faster, right-drag orbits and the wheel zooms down to close eye-level inspection. Floor +/− retains horizontal position; Find resident centres on the selected person without taking control. Entire floor fits the footprint. Save/import and game-over modals block background camera input. The H guide has a scrollable body with fixed action buttons.
- **Crowd contact:** blocked movement keeps real body collision. Nearby pedestrians wait at an occupied destination and can safely pass occupied path corners; brief contact corrections are filtered within the first-person body's footprint. Mouse look, ladders and falls stay responsive.
- **My life → Law & recovered property** shows this resident's known cases, witness-report controls and property receipts. Wardens investigate reports and physically escort suspects. Nearby accessible remains can be examined and reported at their found location without revealing a killer. Seized belongings must reach a jail desk before their owner or authorized institutional representative can collect them there; company property retains company title. A prisoner can attempt a timed escape; an attending guard can stop it. A nearby rescuer can use owned tools to free a captive over time, with witness-based custody-interference consequences.
- **Tab or Esc** opens/closes the animated detail drawer and releases the mouse. Survival starts in first person with only essential health, energy, hunger and thirst meters. Life, facilities, work, housing, management, journal and settings retain their detailed controls inside the drawer. Settings exposes saved language, sound, mouse sensitivity, field of view and graphics preferences, alongside time controls and eight save slots.
- First person: click the scene for mouse look; WASD walks, Shift runs, Space jumps, Tab or Esc opens the menu. E interacts; the prompt identifies the same nearby target, showing service prices and waiting counts. Long prompts wrap in all three languages. At a gold-marked ladder, E grabs, W/S climbs; release/repress at each landing to continue and E dismounts. Gravity and impact damage continue in free view and through save/reload; falling corpses also settle or become unreachable.
- Base walking is 3.6 m/s; running is 7.2 m/s, up to 25% faster with fitness. Injuries slow movement and exhaustion/dehydration prevent sprinting. Actual movement, climbing, gym sessions and swimming gradually improve fitness and strength, consume energy/food/water, and persist in saves. Climbing travels at 3 m/s and stops at each landing.
- Facilities: Walk follows a route to the visible tail of the business queue. After service, residents resume their needs or work shift and clear the counter through ordinary walking. Service requires access, money, available supplies, capacity and any necessary on-site staff. Changing prices, access or opening hours during a trip trigger normal NPC replanning. Cutting a queue can provoke conflict. Sparse elevators have real cabins, served stops, queues, fares, subscriptions and whitelists.
- Combat: F strikes, G shoves, Q surrenders, and left-click fires a carried, loaded firearm while mouse look is active. Wardens can physically respond, apprehend and escort to safe jail space; full detention defers booking while charges remain active. Executing a surrendered resident is treated as murder.
- **DORMNET** requires a phone/laptop or access to a computer. It includes shops and delivered orders, messages, work contracts, vehicles/parking, cosmetic customization, medical subscriptions and resident item listings. Personal resale requires meeting the seller for the handover.
- **Management → Interior & structural works** plans connected partitions/doorways, paid construction crews and safe intermediate-floor removal/rebuilding. Prepare separate clearance offers, negotiate buyouts/compensation and replacement beds, and recover cancelled materials through actual workshop returns. Existing bed/facility building uses its established contracts. See [construction rules and current limits](docs/construction.md).
- **My life → Household & decisions** shows protected necessities, spendable funds, saved shopping plans and automatic/manual control. Automatic adults can provision real goods through paid computer access and actual courier deliveries, meet secondhand sellers, change to funded jobs, buy advertised businesses and share spare meals with trusted neighbors. Ask for food through the conversation menu. See [resident autonomy](docs/autonomy.md) for rules and current limits.
- **Daily plans** forecast real food, water, care, hygiene, clothing and recreation needs around day/night shifts and sleep. Inspect destinations, departure/queue estimates and waiting reasons in the household submenu. Leisure protects rent and essential money at entry, station admission and payment; clean spares prevent avoidable washing charges. See [daily planning](docs/daily-plans.md).
- **Learning & qualifications** offers staffed, paid classes with programming, video and logistics exercises. Saved knowledge shortens related office work; teaching stops when the teacher leaves.
- **Household & decisions** supports voluntary trusted groups, real food errands and autonomous company formation with funded vacant workspace leases. Members keep private wallets, possessions and housing.
- **Household support** coordinates food, medicine and clothing requests with one volunteer, one reserved real item and a private spending limit. NPCs use actual computers, shops, couriers, pantry collection and handovers. Agreed caregivers can assist young members; refunds and paid travel retain real accounting. See [household support](docs/household-support.md).
- **My life → Inheritance & belongings** names a personal heir and accepts or declines reviewed estates. Property, wallets, real belongings and outstanding obligations transfer without relocating tenants or goods. Unclaimed assets, wage debts, rent claims and late refunds persist. Loose inherited possessions must be physically collected. See [inheritance](docs/estates.md).
- **My life → Storage & household pantry** rents private lockers or household pantry space. Deposits keep their owner; sharing is explicit per item. Housemates physically collect donated supplies. Unlocked theft has normal witness and property consequences. Rent, arrears and saved contents remain real. See [storage](docs/storage.md).
- **Work** includes actual courier, sanitation, construction, maintenance and medical tasks, and distinct programming/administration/video commissions. Administration routes actual shortages, repair needs, incidents and injured subscribers to funded work or available security/medical crews. Its fees come from the dormitory treasury; owners still fund the service itself. **Work → Companies & teams** creates businesses with separate treasuries, payroll reserves, owned or leased offices, recruited employees, team leaders and assigned briefs. Leases retain the landlord’s title, accrue rent, hold refundable deposits and preserve debts after default. Companies can fund office construction through real material deliveries and builders. Shares, dividends, acquisitions and funded expansion are available through the company controls; broader investment planning and further job disciplines remain unfinished.
- **My life → Fire & rescue** handles alarms, safe evacuation, station equipment, firefighter jobs, response sponsorship and actual on-site extinguishing. New worlds expose accident frequency; older saves without that setting start with new accidents disabled. Crews require shifts, charged equipment, physical travel and funded calls. Extinguishers are also sold through DORMNET with actual delivery. Fire damages supplies/facilities and spreads through real openings; smoke fills connected upper floors over time. Intact slabs/partitions protect residents, and upper smoke can remain after containment; recovery uses the normal medical, remains and repair systems.
- **Housing/Management** expose property transactions, compensation/relocation, funded facility construction and renovation, extra beds, new top floors and footprint expansion. Materials, actual workers and attended work gate completion. Owners receive service payments; new construction belongs to its payer.
- Cheat permits inspection/possession, autonomous/manual control and bounded overrides for cash, health, needs, happiness, hygiene, strength and aggression. Cash changes go through the ledger. This is not yet an editor for every world rule.
- Menu → Settings → Save / Load opens eight **named save slots**, with mode, game time, population and backup-only previews. Existing slots require a second confirmation before replacement. Previews are advisory; missing or stale metadata never prevents loading the world. A previous `.bak` is retained and used if a slot is corrupt; the browser also offers explicit backup loading. Saving after recovery repairs the primary without destroying the good backup. Game over opens the same browser or returns to the main menu. Default location is `userdata/`; `--saves /path` overrides it. Do not overwrite your only save with an experimental build.

Nearby conversation controls now show **your** trust, friendship/rivalry and the last three encounters you actually know. Violence, observed theft, help and paid local trades can change each person’s opinion differently. [Encounter rules](docs/social.md) describe privacy, retention and save compatibility.

## Company investment and growth

**Work → Companies & teams → Shares, acquisitions & expansion** exposes real share listings, company financing, proportionate dividends, shareholder-approved subsidiary acquisitions, equity-heir nominations and funded office expansion. Browse the market for company IDs. New offices require the established material-delivery and builder workflow. Office desks can now be shared across shifts without paused briefs monopolizing them. See [capital rules and limits](docs/capital.md).

## Vehicles and workshop services

**DORMNET → Vehicles & parking** now includes fuel/condition, paid refueling, staffed repairs, physical towing and automatic driving to X/Z/floor. W/S accelerates or reverses, A/D steers, E enters or safely exits. **Management → Interior & structural works** commissions paid ramps and freight lifts through actual construction crews. New worlds have sparse starter connections when plots fit; existing saves retain their layouts. **Work → Tow operator** exposes real workshop requests and service trucks. See [traffic rules and current limits](docs/traffic.md).

## Washbasins and household hygiene

Use **My life → Walk to a washbasin**, visit one through Services, or press E nearby. A paid visit washes up to four owned tableware/cooker items; My life shows progress and cancellation. Very dirty equipment needs washing before reuse. New worlds have lobby/scattered basins; existing worlds can build them through Management. See [washing rules](docs/washing.md).

## Import a browser world

Open **Load a world → Import browser save**, enter the exported file’s full path, choose a mode and review the conversion report. Then save to a separate named slot. The original export is protected from overwrite during conversion. Existing paid services continue without a second charge; native geometry, staffing, schedules and later transactions use the native simulation.

Versions 1/2, gzip and plain JSON are accepted within bounded limits. Identities, wallets, property, possessions, queues and remaining service progress carry over. Browser-only family/pregnancy/history/experimental state is archived inside the save; those systems are **not yet continued natively**. See [conversion policy](docs/importing.md) for precise behavior and limitations. The prototype remains untouched.

```bash
bash run.sh --headless --import-browser /path/to/browser-export.mdsave --seconds 0 \
  --save /path/to/separate-native-world.mdsave --save-name 'Converted world'
```

Fans, tableware and rice cookers can be ordered through DORMNET with actual courier delivery. **My life** exposes their timed use at the resident’s bed. Recipes reserve real food, consume it only on completion, create waste and wear equipment. Movement, theft, combat and death can interrupt them; active tools/ingredients cannot also be sold or consumed. Activity progress persists through native saves.

## Current systems and limits

Interior partition plans accept horizontal angles and share oriented collision/sight with the renderer and vehicles. Strong fires can spread to exposed businesses on connected floors and damage actual belongings or supply/waste cargo. Solid walls and intact slabs block heat; only surviving shipped units reach their client, with real losses and proportional logistics settlement. See [fire and smoke rules](docs/emergencies.md).

Daily schedules include day/night shifts, commuting, sleep on assigned beds, meals, hygiene and preferences. Kitchens have relief staff. Unstaffed services pause. NPCs choose local needs and work tasks, communicate with nearby residents, and may commit theft or queue violence. Incoming residents bring capital; departing residents take money out. Happiness and deprivation influence migration.

Staffed shops also hand over actual packed meals after payment and queuing. Supply orders use funded imports, high-floor air cargo and real last-mile couriers. Sanitation workers carry waste. Repairers collect parts and work at the broken facility. Paid floating ambulances need crews and physically transport patients. Morgues and crematoria receive transported remains. Businesses replace dead managers; dorm ownership can change through sale, succession or force.

Distant floor structures use cached GPU instances (32/96/192 floor range, software capped at 24). Nearby geometry remains detailed; observer mode offers a cutaway toggle to hide/reveal upper floors. Instanced transforms run in the Vulkan vertex shader. Nearby rigged bodies use compute skinning, including clothing and normals, and write directly into the render buffer. Settings/`--cpu-skinning` selects the CPU reference; software Vulkan and storage-limit fallback use it automatically. Authoritative resident simulation remains on the CPU. See [rendering details and checks](docs/rendering.md). Narrow aisles have finer navigation fallback, route replacement is transactional, full queues redirect NPCs, and completed customers clear stations.

Vulkan shades local fire lights and bounded smoke volumes, alongside animated flame/nozzle geometry. Fire and smoke are localized approximations; Connected-floor ignition and delayed smoke are implemented; structural collapse and production effects remain unfinished.

Vulkan renders imported CC0 furniture and vehicles, PBR material maps, mipmaps, hardware multisample antialiasing, ceiling lighting, fog and a filtered directional shadow map. Nearby residents use imported CC0 male/female bodies with weighted skeletal posing, activity blends, textured faces and saved clothing colors. Distant residents and much of the structure remain procedural. Animation clips/retargeting, separate garment meshes, broader geometry streaming, production art, broader vehicle physics, general structural/load analysis, deeper company negotiations, advanced investment AI and full browser feature parity remain unfinished. The GPU pressure computation has been checked against CPU results; the authoritative simulation currently runs on the CPU.

Nearby remains retain the resident’s clothing appearance and use a static lying character pose, with a brief collapse for newly observed deaths. Collection uses a short crouch/reach or locker handover and visible item movement; capsule and camera positions stay authoritative. These procedural gestures are not ragdolls or contact IK.

## Sound and saved controls

The native mixer generates local footsteps, ladder contact, machinery, running water, fire and moving transport, plus pickup, combat and interface feedback. Settings has master/effects/ambience volumes, mute and background muting. World sounds fade while paused; interface feedback remains. Sound follows nearby real activity rather than creating simulation events. These are original procedural effects, not recorded dialogue, music or final Foley.

Preferences save separately from world slots to `settings.json` in the active save directory, with a previous `.bak`. Language, field of view, sensitivity, render range, cutaway, skinning preference and volume controls persist between normal launches. A damaged settings file reports an error and uses defaults; it is not automatically restored from its backup. Linux audio dynamically uses the installed ALSA runtime and falls back to silence when unavailable. Settings can retry the device; `--no-audio` disables it. Demos and scripted tests leave normal preferences untouched. See [audio and presentation details](docs/audio.md).

See [implementation status](docs/status.md), [art and licences](docs/assets.md), [customization](docs/customization.md), and [third-party utilities](third_party/manifest.json).

### Portable Linux development package

Build normally, then run `cpack --config build/CPackConfig.cmake` using your actual build directory. Extract the resulting archive and launch `./mega_dormitory`. Assets, compiled shaders and licence notices travel with the executable. Saves use `$XDG_DATA_HOME/mega-dormitory` or `~/.local/share/mega-dormitory`; source launches keep their existing `userdata` directory. `--verify-runtime` checks resource discovery without opening a window. The package still requires the system Vulkan loader/driver, GLFW, Zstandard and zlib. See [release checks](docs/releasing.md). This packages the current development build, not the complete accepted game.
