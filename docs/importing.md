# Browser world conversion

The former browser prototype exports gzip-compressed JSON. Those files are different from native CBOR/Zstandard `.mdsave` files, even when their extension is identical. Conversion is one way and does not modify the browser prototype.

From the new-world screen, Settings or game over, open **Load a world → Import browser save**. Enter the exported file's full path, choose Survival/Manager/Cheat, and convert. The current session is replaced only after conversion succeeds; save unsaved progress first. Conversion runs on a background thread. Review the report, then choose a separate named native slot. Nothing is autosaved. Importing directly over the source export is refused, including an equivalent filesystem alias.

The source export remains intact. Keep it: the native game does not yet execute all browser-only systems.

The same conversion is available headlessly:

```bash
bash run.sh --headless --import-browser /path/to/browser-world.mdsave \
  --mode cheat --seconds 0 --save /path/to/new-native-world.mdsave \
  --save-name 'Converted world'
```

Without `--mode`, imported worlds use Cheat. The native window also accepts `--import-browser /path/to/export` and opens its conversion report. Native `.mdsave` files use `--load` in headless runs or the ordinary save browser in the window.

## Conversion policy

- Versions 1 and 2 are supported, with 2–500,000 resident records and 1–2,000 floors. Gzip inputs are bounded to 100 MiB and decompressed JSON to 512 MiB. Invalid checksums, truncated data, oversized files, excessive nesting, conflicting tenants, invalid queues and out-of-range references are rejected. Plain JSON is also accepted for debugging.
- Resident IDs remain stable. Chinese prototype names gain corresponding romanized English/French forms; custom names remain unchanged. Age, sex, health, needs, cleanliness, personality-derived aggression, income accounts and infection/immunity state are carried over. New native schedules and job disciplines replace browser event scheduling.
- Present residents' wallets, historical dead residents' remaining wallets, business accounts and the institutional housing treasury become explicit opening transfers from the external account. Internal money is conserved. Departed residents' cash is recorded as outside money and is not counted as available building wealth. Earlier earned/spent totals and ledger history remain in the archive rather than being applied a second time.
- Property IDs, title holders, active units, prices, listings and valid tenants remain associated. Monthly rents become daily rents rounded up to a cent. Native geometry is larger: beds are mapped to its banks, and housing on floors with no native bed banks uses individually placed beds in a separate part of the footprint. Old routes are discarded and future travel is planned against actual native geometry.
- Facility kinds, prices, stock, capacity, condition and opening intervals are converted. Businesses get real native staff requirements and physical supply/work rules. Office businesses are incorporated into the native company system. New reception, sanitation, security, remains and fire buildings have no invented cash or stock; owners must fund supplies and work. This is a deliberate economic transition, not a balance guarantee.
- All six browser personal-item kinds are preserved: phone, fan, coat, tableware, rice cooker and food. Ownership/listings remain associated; used or departed possessions stay out of the active world. Coat condition maps to cleanliness, rather than incorrectly making dirty clothing mechanically broken. Unrecorded version-one possessions are seeded using the old prototype's rule.
- Queue order and arrival times remain associated. Existing paid services retain their remaining progress and are resumed when real staff are available, without charging twice. Their old payments stay with the business. Canceling one of these converted prepaid services does not refund its historical payment; subsequent services follow normal native settlement. Imported activity counters report transferred activities, not a claim that old motion paths replay exactly.
- Friendship encounter counts map to bounded trust. Original family, pregnancy, birth, social, earnings/spending, historical logs/statistics and experimental parameters are retained in a bounded `browserArchive` inside the native save. **Those browser-only systems are archived, not continued.** Historical deaths do not create fresh bodies to transport. Full browser feature parity remains unfinished.
- A conversion report persists with the native world. Reopen it from **Settings → Conversion report**. Previews, archival records and report totals never authorize extra money, free stock or completed work.

## Household activities

Owned fans, tableware and rice cookers can be ordered through DORMNET with real escrow and courier delivery. Use them from **My life** after reaching your own bed. Fan rest takes 20 game minutes, eating with tableware takes 10 and cooking takes 15. Food is reserved during preparation and consumed only on completion. Completed meals produce waste, and equipment wears and becomes dirty. Benefits use actual elapsed time. NPCs can use their own equipment and food as part of their routines.

Interruption, movement, ownership loss, combat, surrender and death cancel the activity. Unfinished recipes retain their food. Active tools/ingredients cannot be simultaneously sold or consumed; a successful robbery can take an ingredient and interrupts its owner's activity. Progress survives native save/load. The current effects and poses are procedural; this is not yet a thermal/electrical appliance simulation or full cooking minigame.
