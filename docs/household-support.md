# Household provisioning and care (M02.2)

Open **My life → Household & decisions → Household support**. A resident can request
food, medicine or usable clean clothes when they lack that supply and need it.
Another adult member may volunteer, with a default limit of ¥50.00. The limit is
visible before accepting; the API supports limits from zero (an owned gift) to
¥200.00. The board shows the helper, actual spending, limit and current reason.
The player can continue or cancel their own errand, or cancel their own request.
An item owner can withdraw their commitment. Promising support grants no access
to another member's money, private storage, home or possessions.

A request can be heard in person or read through working devices while both
parties are awake. A helper retains their own accepted commitment and the supplied
meeting location. Fresh remote locations require actual usable devices at both
ends; an unavailable contact causes waiting rather than fictional tracking.
Other households cannot see or accept the request. Ordinary private wallets and
ownership remain separate even when a household shares a pantry.

Only one volunteer may accept a request, and only one active support errand may
belong to a volunteer. Each real item can have one support reservation. Normal
consumption, sale, wearing, washing and unrelated pantry withdrawal respect that
reservation. The owner or a request participant can cancel it. Shared pantry
access still requires the original owner's explicit sharing permission. A theft
can take a promised carried item, cancelling the stale promise and preserving its
actual theft provenance; a reservation does not make property immune to crime.

Helpers first use an eligible owned spare or an explicitly accessible pantry
item. They keep their own last meal, worn outfit and needed medicine. Pantry
items are collected by walking to the actual locker, opening it, withdrawing the
item under its permissions and closing it to its prior state. A request then
requires a real meeting and handover, transferring the particular item's title
and carrier. Changing household membership cannot confiscate the item.

Otherwise the helper uses their own spare money above the existing protected
rent/arrears/necessities/business-wage/storage buffer. Shopping requires a usable
owned device or an actual paid public-computer session, an accessible staffed
shop, stock and available couriers. Checkout pays the real escrow account. The
courier collects and delivers to the helper before any onward gift. The helper's
limit includes computer fees and paid personal transport while the errand is
active; fare checks retain the minimum money needed for an outstanding purchase.
Subscriptions/whitelists and self-owned free transport retain their existing
rules. No projected gift is a new resource or a second authorization to spend.

Cancellation before courier pickup refunds the actual payer and releases stock
through the existing order rules. Fees already paid remain expenses. After pickup,
the shipment and cost stay with its buyer; it is never teleported back or
fictionally refunded. Completed support records preserve spending and the actual
item/order identities. Repeated decisions cannot create another checkout.

NPCs post their own current needs and may volunteer according to generosity,
relationships, grievances, availability and affordability. Their essential
preparation and professional shifts remain ahead of optional support. Normal
stock/device/contact waits set retry times instead of journal failure spam.
Urgent self-care can release a promise; expiry, death, departure, withdrawal of
pantry access and cancelled/lost parcels release stale commitments. A cancelled
meeting stops at the body's actual interpolated position.

Trusted adults and nearby consenting automatic young residents can agree on a
household caregiver through **Offer household care**. This is a voluntary household
arrangement, not a biological-family or public-care employment system. Known,
trusted NPCs may make the same decision after real contact. A caregiver can help
post a nearby dependent's need. For children under six, basic feeding, medicine
and dressing require the same physical collection/purchase/handover, followed by
consumption or wearing of the child's newly owned item. A manual adult recipient
chooses whether to consume or wear their gift. Coming of age clears caregiver
authority; a caregiver leaving releases dependents without taking their property.
Clinical treatment and emergencies retain the existing staffed medical systems.

Requests, caregivers, limits, real expenses, orders and item claims persist. On
load, indexes are rebuilt from those records without replaying actions. Older
saves gain empty support state and retain their accounts, inventory and membership.
Requests are unique per household/member/kind. The current retained-register limit
is one million records; longer-term archive/scale work belongs to the later scaling
milestone. M02.3 independent trade/careers/business expansion and M02.4 comprehensive
replanning remain separate unfinished tasks.

## Verification

`mega_coordination_tests` exercises consent and visibility, duplicate requests and
claims, private money/storage, real escrow/couriers, refunds before/after pickup,
pantry collection, computer/transport costs, gift ownership, dependent care,
membership changes, age transitions, urgent self-care, interrupted meetings, theft,
and current/older saves.

`mega_dormitory --playtest-household-care --width 1000 --height 720 --lang en`
runs an isolated connected journey: an NPC volunteers for medicine, a real on-site
vendor sells it, a courier collects and delivers it, the helper walks to the patient,
the patient receives and consumes their owned medicine, and a save/reload retains
the result. Conservation, separate wallets and continuous movement are checked.
Use `fr` or `zh` for localized controls; software Vulkan is supported.
