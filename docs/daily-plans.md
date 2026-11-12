# Daily provisioning plans (M02.1)

In **My life → Household & decisions**, the daily plan shows meals, drinking water,
care, toilets, showering, clothes, waste/dishes, recreation, work and sleep. Each
row shows its current reason, a real destination where applicable, estimated
departure and quoted price. The existing autonomy switch enables or disables
execution for the player; previewing the plan never moves anyone or reserves money,
items, stock or a queue place. Survival retains these details in its submenu.

Plans forecast the same hunger/thirst/hygiene/bladder and clothing depletion rates
used by the simulation. Upcoming shifts, including overnight shifts and the day on
which each shift began, days off, sleep windows, walking/climbing time, capacity,
service duration and actual queues affect departure. Residents prepare before a
long work or sleep window when their needs and remaining time justify it. A shift
that is already in progress remains under the physical job's routine, preserving
cooks' workplace meals, deliveries, repairs, towing, patrols and other actual duties.
Urgent needs retain the existing ability to interrupt ordinary routines and sleep.

School-age residents and adults can plan their own consumption. Adult purchasing,
careers and investment retain their separate existing rules. Young children's care
and coordinated household purchasing are part of M02.2. School enrollment/attendance
continues through the existing physical education system; this task does not add a
new course timetable.

Urgent retrieval from an accessible real pantry runs before planning a paid meal
elsewhere. Owned, usable, unreserved carried food and medicine are consumed through the same
inventory actions as player items. Meals use owned clean tableware/cookers at home
when the real personal-use action is available and fits the schedule. Clean carried
spares can replace dirty clothes, including immediately after a service departure;
dirty garments travel to actual laundry. Missing garments remain an acquisition
need, and clothes precede optional device purchases in household shopping. Storage,
orders, delivery, stock and purchases use the retained real ownership/escrow/courier
systems; the plan creates no virtual water bottles or household stock.

Water, meals, infirmary care, sanitation, waste disposal, washing and recreation
require accessible actual facilities, real staffing where required, stock, capacity
and payment. Closed/unattended/empty/private/full/unaffordable services have explicit
waiting reasons. Estimates are refreshed rather than treated as reservations;
arrival and checkout still enforce the authoritative service rules. Normal estimate
failures do not add repeated error messages to the world journal.

Schooling and other scheduled duties run before discretionary recreation.
Due household shopping, active deliveries and committed item/household errands take
precedence over discretionary trips. Recreation follows the resident's strongest preference and fits around work/sleep,
including the trip from the venue to the next commitment. It can use only the money
above the existing protected buffer (essentials, rent/arrears, owned-business wages
and storage leases). This is checked at queue entry, admission to a station and
checkout. If funds change while a visit is pending, cancellation returns reserved
stock without paying the owner or consuming rent. A completed visit pays the real
operator and starts an eight-hour cooldown. Cancellation clears spending permission.
Manual choices remain deliberate player expenses. Urgent drinking/food/care can use
money earmarked for essentials even if future rent is underfunded.

The decision cooldown, recreation cooldown and pending discretionary-service identity
are saved. Older saves default to empty clocks and permission; loading or opening a
preview never replays a purchase, payment or consumption. The displayed plan is
derived from current state, so obsolete service observations are not persisted as
reservations.

## Verification

`mega_daily_plan_tests` covers forecasts, preparation, ownership, reserved goods,
medicine, spares/laundry, dishes/waste, unavailable workers/stock/access, queues,
protected money, urgency, schedule conflicts, night shifts/days off, job loss,
manual/custody protection, actual leisure payment/cancellation, real bed use and
new/older saves. Existing household, job, vehicle, security and world suites remain
regression checks.

`mega_dormitory --playtest-daily-plan --width 1000 --height 720 --lang en` runs an
isolated rendered journey: owned food, a clean spare, smooth actual travel, drinking
water service, real owner payment, conservation and save/reload. Repeat with `fr`
or `zh`; software Vulkan is supported. No player saves are used by this replay.

Coordinated household purchasing and care are covered by [household support](household-support.md).
M02.3–M02.4 independent trading/career/business decisions and comprehensive
interruption/replanning remain unfinished. The
larger visual/animation/scaling milestones remain open.
