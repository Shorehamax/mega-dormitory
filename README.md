# Mega Dormitory — native development build

A C++20 dormitory society simulation with a custom Vulkan renderer, UI, navigation, movement and game rules. This is a playable development build, **not the complete requested production game**. The browser prototype is preserved separately. The accepted design and remaining work are recorded in `docs/requirements.md` and `docs/status.md`.

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
- Free camera: WASD pans, right-drag orbits, the wheel zooms. The floor selector changes the observed floor; Entire floor fits the footprint.
- **Tab or Esc** opens/closes the animated detail drawer and releases the mouse. Survival starts in first person with only essential health, energy, hunger and thirst meters. Life, facilities, work, housing, management, journal and settings retain their detailed controls inside the drawer. Settings exposes language, draw distance, time controls and eight save slots.
- First person: click the scene for mouse look; WASD walks, Shift runs, Space jumps, Tab or Esc opens the menu. E interacts. At a gold-marked ladder, E grabs, W/S climbs; release/repress at each landing to continue and E dismounts. Gravity and impact damage are real.
- Base walking is 3.6 m/s; running is 7.2 m/s, up to 25% faster with fitness. Injuries slow movement and exhaustion/dehydration prevent sprinting. Actual movement, climbing, gym sessions and swimming gradually improve fitness and strength, consume energy/food/water, and persist in saves. Climbing travels at 3 m/s and stops at each landing.
- Facilities: Walk follows a route to the business and joins its queue. Service requires access, money, available supplies, capacity and any necessary on-site staff. Cutting a queue can provoke conflict. Sparse elevators have real cabins, served stops, queues, fares, subscriptions and whitelists.
- Combat: F strikes, Q surrenders, and left-click fires a carried, loaded firearm while mouse look is active. Wardens can physically respond, apprehend and escort. Executing a surrendered resident is treated as murder.
- **DORMNET** requires a phone/laptop or access to a computer. It includes shops and delivered orders, messages, work contracts, vehicles/parking, cosmetic customization, medical subscriptions and resident item listings. Personal resale requires meeting the seller for the handover.
- **Work** includes actual courier, sanitation, construction, maintenance and medical tasks, and distinct programming/administration/video commissions. Administration routes actual shortages, repair needs, incidents and injured subscribers to funded work or available security/medical crews. Its fees come from the dormitory treasury; owners still fund the service itself. **Work → Companies & teams** creates businesses with separate treasuries, payroll reserves, owned or leased offices, recruited employees, team leaders and assigned briefs. Leases retain the landlord’s title, accrue rent, hold refundable deposits and preserve debts after default. Companies can fund office construction through real material deliveries and builders. Shareholding, advanced investment AI and further job disciplines remain unfinished.
- **My life → Fire & rescue** handles alarms, safe evacuation, station equipment, firefighter jobs, response sponsorship and actual on-site extinguishing. New worlds expose accident frequency; older saves without that setting start with new accidents disabled. Crews require shifts, charged equipment, physical travel and funded calls. Extinguishers are also sold through DORMNET with actual delivery. Fire damages supplies/facilities and smoke can affect nearby upper floors; recovery uses the normal medical, remains and repair systems.
- **Housing/Management** expose property transactions, compensation/relocation, funded facility construction and renovation, extra beds, new top floors and footprint expansion. Materials, actual workers and attended work gate completion. Owners receive service payments; new construction belongs to its payer.
- Cheat permits inspection/possession, autonomous/manual control and bounded overrides for cash, health, needs, happiness, hygiene, strength and aggression. Cash changes go through the ledger. This is not yet an editor for every world rule.
- Menu → Settings → Save / Load opens eight **named save slots**, with mode, game time, population and backup-only previews. Existing slots require a second confirmation before replacement. Previews are advisory; missing or stale metadata never prevents loading the world. A previous `.bak` is retained and used if a slot is corrupt; the browser also offers explicit backup loading. Saving after recovery repairs the primary without destroying the good backup. Game over opens the same browser or returns to the main menu. Default location is `userdata/`; `--saves /path` overrides it. Do not overwrite your only save with an experimental build.

## Import a browser world

Open **Load a world → Import browser save**, enter the exported file’s full path, choose a mode and review the conversion report. Then save to a separate named slot. The original export is protected from overwrite during conversion. Existing paid services continue without a second charge; native geometry, staffing, schedules and later transactions use the native simulation.

Versions 1/2, gzip and plain JSON are accepted within bounded limits. Identities, wallets, property, possessions, queues and remaining service progress carry over. Browser-only family/pregnancy/history/experimental state is archived inside the save; those systems are **not yet continued natively**. See [conversion policy](docs/importing.md) for precise behavior and limitations. The prototype remains untouched.

```bash
bash run.sh --headless --import-browser /path/to/browser-export.mdsave --seconds 0 \
  --save /path/to/separate-native-world.mdsave --save-name 'Converted world'
```

Fans, tableware and rice cookers can be ordered through DORMNET with actual courier delivery. **My life** exposes their timed use at the resident’s bed. Recipes reserve real food, consume it only on completion, create waste and wear equipment. Movement, theft, combat and death can interrupt them; active tools/ingredients cannot also be sold or consumed. Activity progress persists through native saves.

## Current systems and limits

Daily schedules include day/night shifts, commuting, sleep on assigned beds, meals, hygiene and preferences. Kitchens have relief staff. Unstaffed services pause. NPCs choose local needs and work tasks, communicate with nearby residents, and may commit theft or queue violence. Incoming residents bring capital; departing residents take money out. Happiness and deprivation influence migration.

Staffed shops also hand over actual packed meals after payment and queuing. Supply orders use funded imports, high-floor air cargo and real last-mile couriers. Sanitation workers carry waste. Repairers collect parts and work at the broken facility. Paid floating ambulances need crews and physically transport patients. Morgues and crematoria receive transported remains. Businesses replace dead managers; dorm ownership can change through sale, succession or force.

Distant floor structures use cached GPU instances (32/96/192 floor range, software capped at 24). Nearby geometry remains detailed; observer mode offers a cutaway toggle to hide/reveal upper floors. Instanced transforms run in the Vulkan vertex shader. Nearby rigged bodies use compute skinning, including clothing and normals, and write directly into the render buffer. Settings/`--cpu-skinning` selects the CPU reference; software Vulkan and storage-limit fallback use it automatically. Authoritative resident simulation remains on the CPU. See [rendering details and checks](docs/rendering.md). Narrow aisles have finer navigation fallback, route replacement is transactional, full queues redirect NPCs, and completed customers clear stations.

Vulkan shades local fire lights and bounded smoke volumes, alongside animated flame/nozzle geometry. Fire and smoke are localized approximations; general fire spread, structural collapse and production effects/audio remain unfinished.

Vulkan renders imported CC0 furniture and vehicles, PBR material maps, mipmaps, hardware multisample antialiasing, ceiling lighting, fog and a filtered directional shadow map. Nearby residents use imported CC0 male/female bodies with weighted skeletal posing, activity blends, textured faces and saved clothing colors. Distant residents and much of the structure remain procedural. Animation clips/retargeting, separate garment meshes, broader geometry streaming, production art, broader vehicle physics, arbitrary structural editing, company shareholding/succession, advanced investment AI and full browser feature parity remain unfinished. The GPU pressure computation has been checked against CPU results; the authoritative simulation currently runs on the CPU.

See [implementation status](docs/status.md), [art and licences](docs/assets.md), [customization](docs/customization.md), and [third-party utilities](third_party/manifest.json).
