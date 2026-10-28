# Vehicles, roads and workshop services

This pass extends the existing owned vehicles and scarce parking. It is an upright vehicle/contact simulation, not a complete rigid-body engine or a production traffic system.

## Driving and access

Open **DORMNET → Vehicles & parking**. Walk to an operating, staffed dealership to buy; a purchase does not include parking. **W/S** accelerates or reverses, **A/D** steers, and **E** enters or exits. Safe exits require a stopped vehicle, an intact landing and clear space beside a door. Driving input takes over an automatic route. Opening a menu or changing the camera does not suspend vehicle motion or gravity.

While driving, the DORMNET button also opens local vehicle controls without a personal phone; online transactions still enforce their normal device/counter requirements.

The panel displays condition and fuel and accepts a destination **X / Z / floor**. Floors in the UI start at 1. Automatic driving uses supported paths and connected road links; it cannot carry a car through a pedestrian ladder or teleport to an unconnected level. Obstructed traffic waits and can eventually stop its route. Manual intervention may be necessary in congested or tight spaces. Drivers and pedestrians have physical contact bounds; impacts can damage cars, hurt people and create assault consequences. Driving beyond a slab can cause falls, vehicle damage and death. Dead drivers leave persistent falling wrecks.

New modern worlds have a sparse starter ramp from floors 1 to 2 and a freight lift from floor 1 to floor 11 (or the top floor in shorter buildings), when their plots fit. They are initial world assets, not simulated completed building contracts. Existing saves retain their layouts and acquire no automatic roads or fleet. Their owners can commission the infrastructure.

Ramps charge on successful entry, subject to ownership, whitelist and active subscription. A blocked attempt does not collect repeated tolls. Their sloped support, guardrails and opening through the upper slab govern rendering and physical movement. Pedestrian access uses the same support. Roads do not connect every level.

Freight lifts have real queued vehicle calls, doors/gates, platform height, maximum load and one vehicle/rig at a time. A car drives aboard before the platform latches; a tow load must fit completely too. The carrier pays on departure. The platform moves continuously and the vehicle must drive clear to release it. Terminals at both served landings open the vehicle controls, including for a resident without a personal phone. The panel can call the opposite stop near the lift, or an automatic route can handle boarding and unloading. Empty platforms dispatch to waiting calls. Closing a provider pauses service rather than relocating occupants.

## Construction and ownership

Use **Management → Interior & structural works → Vehicle infrastructure**. Set X/Z, destination floor and ramp length. Ramps connect the next level and require 24–64 metres; freight shafts span two chosen endpoints. The complete plot must be clear on every affected level, with supported access and no conflicting property or work. Funding reserves and fences the plot. The established construction crew must obtain and carry real materials, attend the site and perform paid work. Completion opens the owned provider and the actual slab cuts. Prices, access and subscriptions use the existing owner policies.

Decommissioning uses another funded crew contract and requires the payer to own the road provider. Ownership is rechecked at funding and completion; a later sale blocks the old demolition authority. Buy the provider back before removing it; road-specific compensation negotiation is not yet implemented. Occupants, queued calls and vehicles must clear the structure. A connected active road prevents whole-floor removal. A proposal alone cannot erase a road, slab or occupied property. Newly completed parking facilities create up to two real bays within their plot; they are not unlimited capacity counters.

## Fuel, repair and towing

Motor vehicles consume finite fuel with distance. Scooters have 8-litre tanks and cars/vans 40 litres; bicycles use the rider's effort. Stop at an accessible, operating workshop pump and buy up to 5 litres per action. The workshop needs stock and receives payment. Owners/managers can fund bounded fuel imports from the external economy. Initial new-world workshop stock is an endowment; old saves default to zero.

Book a repair for an unoccupied vehicle physically within the workshop service area. An on-shift mechanic must actually reach it, consume stock parts and perform elapsed work. Escrow pays parts, attended wages and the workshop's remaining fee. Absence pauses work; a replacement can resume without consuming the same parts twice. Full repair restores condition, not fuel.

A remote tow request needs a phone/computer or a visit to the workshop. The workshop needs an owned service van and an available tow operator. That employee walks to the real truck, drives to the client, connects the actual vehicle and drives the rig back to its garage. Attachment changes references without moving either car. The load follows through contact checks. Service wages and operator revenue come from funded escrow. Unsupported routes, missing staff, empty fuel, blocked approaches or inadequate lift clearance prevent completion. Towing does not repair the client vehicle.

New workshops in a generated world receive safely placed initial service vans when a supported site is available. An owned delivery van can also be brought to a controlled workshop for a paid, timed tow-rig conversion requiring four parts and a mechanic. Completion contributes its title to the workshop operator. Assigned tow drivers can drive and refuel that fleet; other residents cannot use it as personal property.

Choose **Tow operator** or **Repairer** at a workshop in Work. Its unassigned service requests appear in Vehicles & parking. Accept and attend the job; the same actual travel/work rules apply to the player. A driver must leave a vehicle before changing jobs. Cancellation before a return tow refunds only unspent escrow, stops the abandoned assignment and lets an automatic tow driver attempt the real return journey. An already attached return tow finishes delivery even if the customer dies. Driver loss releases unfinished assignments and detaches loads at their actual positions.

## Resident traffic and saves

Automatic residents can use an existing owned, fueled vehicle for a sufficiently long work trip when a connected road and suitable affordable destination parking exist. They protect their household reserve. They physically approach the car, leave and release their former bay, pay for a destination bay, drive and park. A competing booking or blocked route can stop the trip. This does not give every resident a car, create free parking or turn all ordinary deliveries into van jobs. Most residents still walk and climb in the scarce initial world.

Fuel, condition, falls, road/cargo state, queued calls, automatic journeys, service escrow, attended work and tow references persist in native saves. New fields are optional for older saves. Old motor vehicles default to full tanks for compatibility; old worlds do not receive new structural cuts. English, French and Chinese controls cover these additions.

## Verification and remaining limits

`vehicle_traffic` covers actual ramp/freight travel, collision/falls, finite fuel/payment, staffing/work/wages, towing, conversion, interrupted repair, construction, occupied removal, malformed state, save continuation, paid parking and automatic commuting. Related regressions cover the existing world rules, construction and resident autonomy.

`--playtest-vehicles` generates an isolated world, follows a resident from their real home to a staffed dealership, buys a car, drives up/down a ramp, rides a freight lift and exits. It can wait for the vendor's shift and physical commute. It never loads or overwrites player slots. This is scripted rendered gameplay, not exhaustive manual testing or a claim of long-term traffic balance.

Still unfinished: suspension, wheel contact/rolling and overturning, deformable crash bodies, furniture stacking and comprehensive moving-body support, traffic lanes/signals and general congestion recovery, impound enforcement, autonomous fleet/fuel investment, a detailed workshop interior and towing/service animation. The car model and tow boom are visual approximations. Existing CPU simulation remains authoritative; this pass does not move traffic or physics to GPU compute. Reference-quality interiors, production art, sound and the other accepted feature areas remain separate work.
