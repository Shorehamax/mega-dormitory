# Continuing Mega Dormitory

The user asked for a complete ordered plan that future work follows one task at a time. These instructions apply when continuing implementation. They do not turn a question, review or planning-only request into authorization to implement unrelated features. Current user instructions take precedence.

## Resume the recorded task

1. Read `docs/requirements.md`, `docs/completion-plan.md` and `docs/completion-progress.json` before choosing implementation work. The JSON tracker is authoritative; the Markdown is generated from it. `docs/status.md` is historical evidence, not a replacement queue.
2. Resume `current.milestone` / `current.task`. Follow M00–M32 in order, one milestone at a time. M00 is a one-time inventory; do not restart it or rebuild already delivered systems on every session.
3. Finish the current task and its required connecting work, then continue to the next task. Record a prerequisite fix under the milestone it unblocks and return to that task. Do not substitute an unrelated polish pass or a fresh general backlog.
4. If the user changes priorities or scope, record a dated reason in `change_log` and update the affected tasks and pointer. Optional ideas go in `future_ideas`; they do not become new release blockers without an accepted requirement. Preserve the dormitory focus and the cancelled island/city/Creative-mode decisions.

## Completion must be evidenced

- Existing code is the retained baseline, not evidence that every remaining acceptance criterion is complete. Preserve existing features, player saves and the browser prototype.
- A feature includes rules, real actor movement/actions, NPC choice, player interaction, staffing/resources, ownership/accounting, failure/interruption, persistence, localization and presentation. A button, counter, archived import field or isolated test is not a substitute for its complete gameplay loop.
- Mark a task done only with relevant evidence in its `evidence` list. Mark a milestone `complete` only after every task and acceptance criterion passes; populate `completed_on`, milestone `evidence` and one `acceptance_evidence` entry per gate criterion.
- Use focused checks and relevant regression/rendered/manual play. Broaden testing when changes, failures or unresolved concerns justify it. Do not rerun all native game suites or rebuild packages for documentation-only edits.
- If task implementation is finished but the milestone gate remains under review, keep the milestone active and use a null current task with an explicit next action. A blocker needs a concrete explanation and the pending action; it does not erase the milestone.
- Before ending an implementation turn, update the evidence, actual status, blocker and exact next task. Advance `current` to the first unfinished task in the first unfinished milestone. Regenerate with `python3 tools/render_completion_plan.py` and validate with `python3 tools/render_completion_plan.py --check`.
- Report completed IDs, relevant verification and the exact next ID. Do not ask which feature to do next when the tracker already answers it. Do not claim the whole game is finished or shippable until M32 passes its full gate. Do not report a whole-game percentage from this remaining-work tracker.

## Project workflow

Keep source changes in the authorized project checkout, preserve existing save locations, and use the established verification/install/publish workflow for implementation checkpoints. Do not stage generated builds, player data or unrelated workspace files. Store reproducible verification evidence or durable references with the tracker so another session can continue without reconstructing the chat.
