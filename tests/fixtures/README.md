# Saved crowd regression

`common-exit.fixture` is an immutable native save from an isolated generated
600-resident, 20-floor Cheat-mode test world. Its people, companies and names are
procedural test data, not a player's save. The snapshot predates queue-tail and
departure routing. Many customers are trying to stop at the same former food
counter exit, preventing resident 226 (a dealership clerk) from returning to work.

The rendered crowd-flow test loads this exact state, preserves every resident's
position, needs, employment and budget, and runs the ordinary simulation. It
requires the clerk to physically reach their workplace and at least twelve of
the original blocked customers to leave the cluster. No reserve clerk is added.

The `.fixture` extension distinguishes this regression input from saved-game
slots. Loading it never writes it back. Save integrity is checked by the normal
CBOR/Zstandard loader.
