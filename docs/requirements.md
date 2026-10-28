# Accepted game direction

Latest user revisions override earlier outside-world and Creative-mode proposals. This is the design target, not a claim that these systems are all implemented. See status.md for current behavior.

## World and presentation

The dormitory is the main game: a cruel, overcrowded, resource-poor society with a deep central shaft, steel bunk structures, narrow galleries, bridges, ladders, visible clothing and personal clutter, and a crowded ground corridor leading to a lobby. The floating island and explorable outside city have been cancelled. Outside activity is represented by migration, supplies, and contracts when required.

Start at 2,000 floors and 100,000 residents by default, with editable generation settings. Game time is compressed for play, initially a 30-minute day. Ladders should feel responsive. Residents move smoothly, use real facilities, queue visibly, and sit/lie on actual furniture while resting and sleeping. Nobody should idle outside supported geometry.

Use a custom C++ engine and Vulkan rendering, with custom physics/navigation/animation/UI. Utility libraries may decode assets/fonts, provide window/input, audio and compression. Batch suitable work on the GPU and retain a CPU path; software Vulkan/headless options must be available for systems without a suitable GPU. Implement streamed visible floors and levels of detail rather than rendering every object in all 2,000 floors. Aim for good lighting, material textures, shadows, high-quality licensed assets, multiple asset formats, polished animation, readable production UI and responsive layouts. Linux first; prepare for Windows without tying game rules to platform APIs.

## Modes and persistence

- Survival: roleplay a new resident. Survive scarcity, earn money, improve housing, accumulate assets and businesses, and potentially change the world.
- Manager: play the dorm owner, monitor the building, manage its assets/budget, hire wardens and other staff, and invest in facilities.
- Cheat: roleplay or inspect/possess residents, inspect overall statistics, and adjust settings/stats. It replaces the former Creative mode.
- In Survival the owner is an AI. Ownership may change by a very expensive voluntary sale, retirement/succession, or force; it is not always for sale.
- Choosing to move out in Survival or Manager ends the run. A world with no living/present residents is game over. Resident death should support reload or continuation as another resident where appropriate.
- Multiple named save slots, versioning, backup/recovery, and a one-way import path for the existing browser prototype. Preserve the old prototype while porting its behavior.

## Individuals and daily life

Every resident has a persistent identity, English/French/Chinese display-name forms, age, personality, preferences, needs, health, money, home/tenure, owned items, clothing, relationships, communication history, work and schedules. Switching language changes all references to names without changing the identity. Localize the complete game in Chinese, English, and French.

Residents eat, drink, use toilets, shower, sleep, change/wash clothes, study, work, shop, socialize, relax, and seek care. Hygiene includes body/clothing/environment dirt and disease consequences. Model personal possessions and storage, theft, trade, maintenance, and delivery. Resident communication must affect relationships/information, not just decorative messages.

Provide toilets, showers, sinks, laundry, kitchens/canteens, drinking water, shops, lockers, waste disposal, cleaning/repair services, classrooms, public/work computers, and other daily-life businesses. Sparse recreation includes a few pools, arcades, playgrounds, cinemas, gardens, libraries, gyms, cafés, and additional suitable venues. Different residents prefer different activities; cost, travel, queues, crowding and satisfaction matter.

Facilities are unevenly distributed: residential floors may have none or a few; some floors are service floors, work floors, or commercial streets. Each facility should be usable/interactable and have real staff/capacity/resources, not a decorative prop.

## Scarcity, queues and conflict

Start with substantial service shortages and long actual queues. Queue order, available stations, waiting time and affordability matter. Both players and NPCs may cut queues; others may object, report it, yield, or fight. Behaviour varies by personality, relationships, strength and circumstances.

Combat includes melee and firearms, pain/fatigue/injuries, surrender, resistance, and death. Most defeated/outmatched people prefer surrender. Executing a surrendered person is a severe crime. Distinguish responsibility/self-defense, assault, excessive force, and murder; consequences should depend on evidence and fault. Wardens physically respond, fight, restrain, escort suspects to real available jail cells, and may use deadly force. Their misuse of force also has consequences. Interruptions, rescues, escapes and witness reactions must work.

Falling off galleries or out of the dorm uses real gravity and impact damage and can kill. Pushing someone must preserve causal responsibility.

## Economy, property and construction

Every service/business/facility/workspace has an individual or company owner. Customers pay that owner. Wages, leases, stock, supplies, repairs, construction, subscriptions, fines and dividends use real accounts. Residents can buy businesses for extra income. Money can buy most things in this dystopia, but purchases still depend on actors, resources, contracts, availability and consequences.

Keep money conserved internally. New residents bring tracked capital in; emigrants take money out. External supplies/contracts are explicit sources/sinks. Population happiness, deaths, unemployment, deprivation and insecurity influence migration. Prevent accidental creation of money or rewards without an economic counterparty.

Homes can be rented/bought/sold. Beds near useful services, commerce, jobs or transport are more valuable; actual accessibility, queue times, fares, danger, noise and condition affect premiums. Respect existing leases, ownership and occupants.

Layouts, beds, facilities and floors can change while the game runs. A planner/owner commissions funded projects; construction companies, foremen and workers carry out actual surveying, materials delivery, construction/renovation/demolition and repair. Players may be planners, contractors or workers. Initial ownership of a newly constructed facility belongs to whoever pays the construction company, not automatically the builder.

Displaced tenants and owners have rights/compensation claims. Ordinary projects require consent and funded compensation. Extreme forced evictions involve physical enforcement, resistance, combat/casualties, crime/reputation and population effects. Building earns future revenue only when real customers use the result.

## Work and organizations

People choose among available inside-dorm jobs, workplaces and employers. Companies rent/buy work areas. Owners/team leaders assign work, manage staffing, wages, contracts, workspace and profit. Include delivery, programming, video production, administration, construction, maintenance/repair, sanitation, retail/food, teaching, medicine, pilots, wardens, and other causal jobs.

All jobs should have real tasks. A delivery worker must reach pickup, carry an item, reach its recipient, and hand it over. Builders require materials and site access. Wardens apprehend and escort. Medics reach/treat/transport. Cleaners move waste. Failure, interruption, unavailable tools/staff/resources and travel must matter.

Office jobs need meaningful discipline-specific minigames. Video editing assembles/trims clips for a brief, continuity, pacing and quality. Programming, administration and other work need distinct tasks rather than identical timers. Work quality/time affect contracts, employer revenue, pay and promotions.

Phones, laptops, public computers and work computers provide an attractive in-world network with shopping, actual deliveries, messages, jobs/news, company tools and relevant services. Computer access may require payment or employment. This is a simulated network, not a promise to browse the actual public internet.

## Medicine, emergencies and death

There is no separate outside hospital. Internal infirmaries/medical businesses can own treatment rooms, ambulance hangars, equipment and staff. Flying ambulances pick up actual patients. Subscriptions determine priority, response times and optional services such as armed escort for an active fight. Tiers depend on available vehicles, crews and resources, not instant rescue.

Use temporary morgues and a few internal cremation sites, with actual corpse transport, storage capacity, costs, staffing and sanitation effects. Include infrequent accidents/disasters and contagious disease with causal spread and responses. Death must clean up jobs, queues, property, items and relationships consistently.

## Transport and vehicles

Vehicles are expensive owned assets sold by businesses: bicycles, scooters, cars, delivery/service/emergency vehicles and appropriate further types. Scarce parking has physical spaces, owners, fees/leases, allocation and occupancy. Purchasing a car never creates a free parking spot. Obstruction affects deliveries/emergency access and can lead to fines, actual towing, disputes or violence.

Vehicles must respect corridor widths, pedestrian congestion, ramps, load limits and freight-lift access. Include fuel/energy, maintenance, storage, collisions and ownership/driver permission. Do not move vehicles between floors by teleportation.

Elevators are sparse owned facilities, serving selected floors. Owners may charge per ride, sell subscriptions or whitelist free users. Queues, cabin capacity, stops, travel and maintenance must be simulated.

## Acceptance principles

A feature is complete only when its rules, persistent state, visible actions, player interaction, NPC behavior, failure/interruptions, localization and economic consequences agree. References and user examples are not exhaustive checklists: design the connecting systems so the world can continue causally without scripted shortcuts. Keep implementation status honest and preserve all existing browser features as parity targets.

## Latest scale and completion requirements

Default each ordinary floor to **512 × 512 metres**, exceeding 500 × 500. The entrance floor starts larger, at **640 × 640 metres**. Expose independent width/depth settings for both in new-game creation. Floor dimensions belong to saved world state and may later increase through funded construction-company projects. Continue toward a finished game, rather than treating an isolated development checkpoint as the full requested release.

## Current implementation priorities (27 October 2026)

The user explicitly prioritizes gameplay features before further optimization. Work in this order: resident autonomy and relationships/provisioning/careers/investment; flexible construction and displacement workflows; vehicles and physical interactions; companies, jobs, schooling and inheritance; crime, investigations and disasters; then visual density, animation, character/clothing assets and sound. Play through the game while implementing these systems and repair problems revealed by gameplay. Keep performance work to correctness and necessary functional bounds until those feature priorities are addressed.
