# Company shares, acquisitions and expansion

Open **Work → Companies & teams**. **Browse company market** lists companies in pages of eight; IDs are shown beside their names. Select a company and open **Shares, acquisitions & expansion**. Existing Operations & staff controls remain available on the other page.

## Shares and control

A company has exactly 10,000 indivisible voting units. Initially its founder holds all units. Personal share sales pay the holder; **Company financing** pledges units from the director's own holding and pays the company treasury. This is a transfer of existing equity, not issuance or dilution. In a wholly owned subsidiary, its controlling company's holding can be offered by that company's director. Corporate sale proceeds belong to the corporate seller, not to its director personally.

Enter units and price **in cents per unit**. Offers reserve real owned units, so overlapping listings cannot oversell them. Buyers can purchase blocks of up to 100 units through the interface; the simulation supports other valid partial quantities. Cancellation releases only the unsold reservation. A positive price, sufficient money and an active offer are required. Adult buyers need a usable personal device, paid computer access, an employee workstation session or presence at the issuer's company counter. Trading does not move a resident or fabricate an item.

The largest aggregate eligible voting holding appoints the director; an incumbent keeps an equal tie. Corporate holdings vote through their own current controller. Dead, departed, leaving or detained people cannot exercise direct control. There is no circular ownership, and deeply nested ownership is bounded. Authority changes immediately after a trade: the old director loses permissions, applicable facility managers and unfinished company-building clients change, and active landlord-management snapshots follow the new controller. Appointed team leaders retain their established operational permissions without gaining personal ownership.

Dividends distribute the requested total across **all** holders proportionately, including corporate holders. Rounding remainder goes to the largest holding, so every cent is accounted for. Accrued wages, a payroll shift and rent reserves remain protected. A director cannot use a dividend to appropriate minority holders' money. Ordinary additional capital contributions retain their previous behavior: a contribution by itself does not create new shares.

New worlds advertise 15% of each initial non-player company's equity for company financing. Existing native saves receive their old owners' full equity titles and acquire no new unsolicited offers or automatic expansion policy. Neither a company nor its shares are automatically for sale thereafter.

The displayed business value is a bounded game heuristic using cash after reserves, facilities, vehicles, limited historical work revenue and corporate holdings. It is not a guaranteed sale price, a full accounting valuation or a simulated stock exchange. Automatic adults occasionally buy small reasonably priced blocks, using at most one eighth of their spendable funds and preserving their household necessities reserve. They require an actual working device, public units and actual payment.

## Company acquisition

Select the buying company and enter the target's displayed company ID and **total bid in cents**. The buyer's director funds escrow from company cash after protected reserves. A target can have only one pending acquisition at a time. The proposal expires after three game days.

Current shareholders vote with their current holdings. More than 5,000 units must approve; a manual player holder is not automatically voted in favor. Automatic shareholders can approve an adequately priced proposal. Once approved, all holders are cashed out at the same price per unit, including opposing minority holders. This majority cash-out rule is displayed before voting. Changing holdings changes voting weight; past votes do not reserve or duplicate shares.

The acquired company becomes a wholly owned **subsidiary**. Its treasury, property titles, leased spaces, deposits, staff, wages, debts, customer escrow, active office work and construction contracts remain in that legal entity. Its controller follows the parent; that does not deposit the subsidiary's cash into the director's wallet. Subsequent subsidiary dividends flow into the parent's treasury. This preserves obligations rather than erasing them in an accounting merge.

Withdrawing or expiring a pending bid returns its actual escrow to the buying company once. Losing the proposing director's control cancels the old proposal and refunds the company. Other owners' consent cannot be replaced by simply clicking Buy. Cycles and excessive nesting are rejected before funding or completion. Legal entity amalgamation, share exchanges, negotiated mergers, corporate loans and insolvency/liquidation are not implemented by this acquisition system.

## Equity inheritance

Enter another living adult resident's ID as your equity heir; **-1** clears the nomination. After death the nominated eligible resident receives the deceased holder's company shares, not newly created money. Old listings of those inherited units are cancelled. Automatic holders without an eligible nomination can pass equity to a living team leader or adult employee; this is a simplified succession rule. If no eligible heir exists, the title stays in the estate instead of inventing an owner. Control updates through corporate holdings as appropriate.

Moving out preserves a holder's absentee property rights. A dividend paid to an absent personal holder flows to the external economy. This does not implement personal wallet, housing or possession probate, family law, inheritance taxes or a general estates interface. Those remain separate work.

## Workspace expansion

Directors can fund a further workspace immediately or enable automatic expansion. Player-founded companies start with automatic expansion disabled. Existing initial NPC companies enable it in new worlds, with an initial delay. Automatic expansion needs at least eight recorded successful work completions, 2,000 yuan of recorded work revenue and an office staff roster of at least two workers per workstation. Attempts are spaced; successful expansion waits another game day before reconsideration.

A company is limited to four operating or pending office spaces and cannot expand again while its own construction remains unopened. A nearby offered lease retains the landlord's title, charges actual rent and deposit, and transfers operating rights through the established employment/lease workflow. Automatic policy seeks unoccupied offered workspaces; it does not repeatedly strip staff from an occupied competitor. Explicit manual expansion may accept an advertised occupied workspace under its existing lease terms.

Without a suitable lease, expansion tries bounded clear plots near the existing operation and commissions a real two-station office. Company cash pays the construction budget and dormitory permit while retaining payroll/rent and a future hiring buffer. The new office remains closed. Existing builders must collect and carry supplies and perform paid attended work before it opens. There is no free office, instant build or fabricated worker. The ownership page shows funded work and whether its materials have arrived; Management retains detailed construction controls. These office jobs use the established individual-builder contracts, not the newer structural crew workflow.

Companies continue hiring real available workers and assigning attended contracts through their existing team rules. School curricula, fuller video timelines, broader office disciplines and autonomous formation of entirely new companies remain unfinished.

## Workstation behavior and persistence

Office computer access now uses the same three-metre standing area as payroll and office contracts. A paused brief belongs to its worker but no longer reserves an empty physical computer across shifts. Actual on-site, on-shift working employees determine capacity. A returning employee must still wait if other people occupy every station. Progress pauses when a worker leaves, and contracts/escrow remain saved.

Share registers, outstanding listings, ballots, bid escrow, subsidiary control, heir nominations and expansion policies are saved in native state. Old companies gain their existing owner's 10,000 units only when the old file lacks equity fields; a malformed new register is rejected. New optional fields do not rebuild existing layouts, move residents or overwrite slots.

`company_capital_and_expansion` exercises financing versus sales, reservations, access, control, dividends, absentee outflows, consent/cash-out, cycles, escrow/refunds, succession, interrupted construction authority, automation, funding and save continuation. `--playtest-company` runs an isolated rendered scenario: real home-to-office travel, attended client work, a share purchase, a dividend and a funded further workspace. Expansion may acquire an open lease or leave a real construction contract awaiting workers; the playthrough does not claim to complete that new building. Test worlds never load or overwrite player slots.

This remains a development feature pass. It adds no production art, advanced financial market, full estate system, complete schooling or deeper video-editing timeline. Authoritative economy and resident decisions remain CPU simulation; existing Vulkan rendering/skinning paths remain in use.

Rendered regression testing also exposed a pre-existing car/crowd deadlock under the changed economy. Vehicle plans now consider actual nearby people. Existing contacts may separate without allowing deeper penetration; a blocked driver can make bounded slow reverse/replan attempts on supported ground. Recovery state survives saves. Fresh bodies, furniture and floor edges remain physical constraints. This is limited recovery, not a complete traffic scheduler.
