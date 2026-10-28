# Interior and structural construction

Open **Management → Interior & structural works**. Dormitory owners can commission changes to the common structure; Cheat mode also permits them. Construction requires a real funded contract. Proposing a plan does not change collision or spend its construction budget.

## Interior layouts

Set the starting X/Z coordinates, orientation and length to create connected partitions and rooms. Segments are axis-aligned, 2.5–64 metres long, 18 cm thick and 2.7 metres tall. The optional central passage is 1.6 metres wide and 2.15 metres high. It is an open doorway, not an animated lockable door.

Placement protects bed footprints, facility service/queue space, ladder access and routes from homes, businesses and people to a ladder. Wall junctions are allowed; sealing someone inside is rejected. Construction rechecks the layout before completion and waits if someone occupies the installation footprint. Changed routes are invalidated rather than allowing residents to walk through the finished wall. Unrelated ladder traffic does not block construction. Close meshes, distant instances, navigation, capsule contacts and line of sight use the same wall segments.

Select an existing partition to propose its removal. Beds and facilities continue to use their funded construction/removal contracts and arbitrary clear X/Z plots. General angled walls, partial floor cutouts and moving an entire furnished business intact are not included.

## Crews and foremen

**Work as foreman** joins a hiring construction workshop as a builder and forms a crew. Recruit available builders from that workshop or voluntarily join another crew. A workshop owner or foreman manages membership, appoints a replacement foreman and assigns structural work. Crews have at most eight consenting builders; manual residents cannot be conscripted. A resident cannot hold two physical construction assignments.

Funded structural work can recruit an available automatic workshop crew. Its hauler actually visits the workshop, pays for four stock units from escrow, carries the material crate and unloads it at the site. Other members travel to separate standing positions. Work and wages require physical attendance during each member's shift. An attending foreman gives a modest productivity benefit; an absent foreman contributes neither labor nor supervision. Urgent needs, custody, medical transport and other physical interruptions take priority. Dead or departed members are released, a replacement foreman can take over, and dropped materials must be recovered. An empty crew releases its unfinished contract for reassignment.

Completion pays earned wages and the remaining agreed contract price to the workshop operator. Cancellation refunds only unspent escrow and releases the crew. Paid materials remain real owned crates. A builder can collect a cancelled crate and return it to the original operating workshop for a funded ¥800 buyback and four returned stock units. Collection and return require reaching both locations; neither payment nor stock can be credited twice.

Cooperative crews currently execute partitions and floor-slab work. Existing bed, facility, footprint-expansion and repair contracts retain their established individual-worker workflows.

## Intermediate floors

Propose removal on any level above the entrance. **Prepare separate clearance offers** creates outstanding bed-relocation and facility-demolition proposals without granting consent, spending their budgets or deleting property. Existing leases and active fires block the batch before proposals are created. Existing claims are reused rather than duplicated.

Resolve those contracts first: even vacant owned beds must be removed, businesses must complete demolition, and partitions, loose belongings and remains must be cleared. Residents, routes ending on the level and lift journeys must also clear it. Funding is refused until the level is clear; final clearance is checked again before the slab disappears. Funding locks out new beds, facilities and expansion on that level.

The crew works from a supported lower landing. Removal preserves the level number, other floors, continuous ladders and structural columns. It removes the slab in both renderer paths and from physical support. Bodies can fall through the resulting void and receive the existing fall damage. Residents cannot dismount ladders onto a removed level. Lift stops on that level are removed. Rebuilding the slab uses another funded, supplied and attended contract; lift stops are not silently restored. The entrance slab is protected. Load-bearing-column editing, collapse analysis and partial slab cutting remain separate future work.

## Compensation and eviction

Housing-removal proposals track separate moving compensation, property-owner buyout, replacement bed and outstanding demands. Affected residents can accept, refuse or counteroffer. Revised offers require renewed consent. Automatic residents compare offers with their requirements; a person who owns and occupies the bed must agree to both rights. Ownership or tenancy changes invalidate the old agreement.

Choose a replacement bed ID, or zero for the nearest available bed. Funding checks availability again, requires enough money for the work, buyout, compensation and first replacement rent, and settles old rent arrears from the compensation before paying the resident the remainder. Replacement housing is assigned without teleporting the tenant. Withdrawal before funding preserves the home and lease.

Refusal cannot be bypassed by a force checkbox alone. The existing forced-relocation path requires a nearby tenant who has physically surrendered, still requires property-owner agreement, replacement housing and funded compensation, and reports a crime with happiness consequences. Actual fights and warden responses use the existing combat/legal systems. A structural proposal never authorizes remote mass eviction.

Each clearance contract is independent. Cancelling a later slab-removal proposal does not undo already completed buyouts, relocation or demolition.

## Persistence and verification

Walls, slab presence, crews, contracts, delivered materials, attendance, counteroffers and refusals are saved. Older native floor plans default to intact slabs; older housing contracts keep their previous buyout defaults. The controls and errors are localized in English, French and Chinese.

`flexible_construction` tests real stock/money/work, collision and sight, room egress, demolition/rebuilding and gravity, clearance, negotiation, force, save/reload, death, cancellation/salvage, voluntary membership and interrupted staffing. `--playtest-construction` runs an isolated rendered world: two hired builders start at their actual homes, walk to collect and deliver materials, work and complete a partition with a walkable doorway. It does not load or overwrite player slots. This is a scripted in-game playthrough, not manual or exhaustive playtesting.

The rendered scenario also exposed a ladder-entry deadlock. Landing entrants now retreat through normal capsule motion to let traffic already on the ladder pass; collision is not disabled. This addresses that specific conflict, not all crowd congestion at every scale.
