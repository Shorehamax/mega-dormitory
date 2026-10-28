# Resident decisions and household economy

Automatic adults now make persistent shopping, resale, career and small-business decisions. Manual residents and children do not make these financial decisions. Urgent needs, sleep, work, medical care, custody, transport and unfinished physical work take priority. This is an initial autonomy system, not the complete social or investment simulation.

## Household provisioning

Residents protect three days of rent, existing housing arrears and a necessities allowance. Personally owned operating facilities add a staffing/supply buffer. Shopping normally commits at most ¥200 per game day, with a separate saved-for laptop decision for wealthy office workers. These are planning limits, not restrictions on player spending. The household panel shows the reserve, spendable balance, latest decision and purchase commitments; refunds do not reset the commitment counter.

Shopping considers medicines, food, a phone, spare clothing, tableware, preference-based appliances and work laptops. It counts actual usable possessions, rather than repeatedly buying the same device. Clean spare clothes can replace dirty outfits, and laundry can reserve owned spare garments. Stock, access, opening hours, paid computer sessions, funds and courier capacity must exist. A resident without a usable device walks to a paid public terminal before ordering. Busy terminals cause a later retry; they do not provide an invisible connection.

Orders reserve real shop stock and money in escrow. A local shop courier walks to the counter, collects the item and reaches its recipient for handover. A nearby recipient can wait briefly, but urgent thirst or other needs override waiting. Couriers retain parcels when recipients are temporarily between floors or in transport; a failed rendezvous does not teleport the item or double-book the courier. Uncollected orders can be cancelled/refunded. Lost shipped parcels settle the goods price to the shop and refund only the unearned delivery fee: the buyer still owns the actual dropped goods. Settlement occurs once. Dead or departed participants and changed courier occupations release/recover assignments.

## Secondhand goods and help

Residents can advertise surplus usable belongings. Worn clothing, stolen goods, reserved equipment and items in use are excluded. They retain essential possessions. Shoppers compare local ads with new prices, remember the agreed price and physically meet the seller. Payment and ownership change only during a valid nearby handover. A changed price, unavailable seller, expired agreement or repeated unreachable meeting cancels the plan. Ads are reconstructed from saved item listings.

Trusted nearby acquaintances may share an owned meal after conversation. The giver must have a spare, retain food for themselves and respect a six-hour pair cooldown. Sharing changes real item ownership and saved trust; it creates no food or money. Players can request a meal through the conversation menu. This is not yet a full household/family provisioning or relationship lifecycle.

## Careers and investment

Adults compare local funded vacancies, offered wages, commute, interests and shift capacity. Existing workers normally require a substantial pay improvement; unemployment, a closed workplace or unpaid wages also motivate a change. Real job assignment changes wages, attendance and sleep schedules. Unpaid claims survive changing employer. Assigned construction, housing, expansion and repair work must finish before an autonomous unrelated career change. Recruitment cannot create payroll funds or teleport staff to work.

Wealthy residents can buy an actually advertised, local operating consumer facility when its demand and price justify the risk and enough operating reserves remain. The purchase transfers real money and ownership. Subsequent income comes only from actual customers. This does not yet include autonomous company formation, shares, mergers, new construction investment, expansion or inheritance.

## Controls, saves and verification

Open **My life → Household & decisions** to inspect plans. Automatic/manual control is available for your own resident and in Cheat mode. Plans, purchase commitments, pending resale agreements and aid cooldowns survive native saves. Older native saves load with empty plans and existing items/orders intact. English, French and Chinese catalogues include the new controls and decision text.

The dedicated autonomy suite covers budgets, physical trades and deliveries, public computers, vacancies, unpaid claims, business purchases, sharing, interruptions, malformed saves, airborne recipients and work commitments. `--playtest-autonomy` runs an isolated rendered scenario: walking to a shop, enabling decisions, making a budgeted purchase and observing the courier collect and hand over the parcel. It does not use or overwrite a player save. `MEGA_VULKAN_TESTS=ON` includes this display-dependent scenario in CTest. This is a scripted in-game playthrough, not a claim of manual or exhaustive playtesting.
