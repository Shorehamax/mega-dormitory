# Cell interaction and first-person movement crash

The 7 November report ended with `MEGA DORMITORY: physics.error.body` when interacting with the warden cells after an escort. That exact sequence was reproduced against `86314fd`: a real arrest and escort reached jail; the old E interaction joined an ordinary jail service queue; the next custody update cleared detention but left the queue reservation; the next first-person movement step threw the reported exception.

## Corrected interaction

E while detained opens the custody/report controls. E at a warden desk opens its reports and recovered-property interface. Jails do not accept ordinary consumer service bookings. An active custody record remains authoritative across transient activity labels: walking, service queues, lifts, vehicles and ladder controls cannot bypass it. Arrest also releases a previous manual ladder grip before the real escort starts.

A still-active booked inmate saved in the old cell-service state is restored to detention at the same actual position. The invalid queue reservation is cleared without a charge, and the sentence is preserved. A genuinely interrupted escort still releases its suspect in place; a completed escape or sentence release restores normal movement.

## Movement boundary

First-person movement uses shared model readiness checks. A no-input frame waits while a service reservation, route or another activity remains active. Explicit walking cancels an interruptible service/task once before stepping. Expected refusals stay inside the normal UI action path. Malformed numeric inputs still fail validation.

Taking control retains the actual interpolated route position. Waking during a fall cannot snap the resident onto a floor; knockback, gravity, impacts and body contact remain active.

## Regressions

- The physics suite contains 29 cases, including ten new movement-state regressions.
- The security suite contains 60 cases, including six new cases for real booking, cell services, authoritative custody, saved-state recovery, release/escape and ladder capture.
- `--playtest-body-control` runs 180 real first-person frames through a pending idle-labelled service, explicit walking cancellation and departure.
- `--playtest-security --verify-custody-control` performs an actual robbery, report, cross-floor warden response, seizure and escort. It takes first-person control of the booked inmate, invokes the exact E handler, opens/closes custody controls and checks that walking remains blocked. The victim then physically reaches the desk and recovers the original stolen property.

All fixtures are isolated generated worlds. See [verification evidence](evidence/body-control-verification.json).
