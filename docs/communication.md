# Resident information and messages

Residents can observe and pass on service reports, hiring notices, personal item offers, requests for food/water/care, threats and eyewitness accounts. Each report stores its original observer, observation time, reported place, the most recent messenger and when the recipient learned it. Passing a report on preserves the original snapshot; it does not refresh its contents from the live world.

## Player controls

Open **My life → Information**. **Look around** records a nearby visible accessible service, its price/stock/queue/availability, any hiring notice there, and the actor's own serious needs. Nearby conversation controls can ask another resident about each information category; a resident without a matching report cannot supply one. Choose a recipient by resident number to tell them a report in person or send it through a usable phone, laptop or computer.

Report cards show the source, messenger, observed/learned times, channel, reported floor and freshness. **Visit reported place** starts real walking to that saved address. Older reports can be wrong about current stock, prices, queues or a moving seller; visiting them does not locate the subject's hidden current position. Remote item listings do not reveal the seller's live location at all. Market offers and job controls can save an offer or hiring notice. **Request meeting** needs the awake seller's own usable device: they reply with a dated location, and the buyer walks to that shared snapshot. Subsequent seller movement is not tracked remotely.

**Network → Messages** shows ordinary text and structured report attachments with wrapped content and scrolling. A queued device message is pending until its recipient is awake and has usable access. A paid public terminal requires actual arrival, payment to its operator, an available station and an unexpired session. Leaving the terminal removes access. Knowledge already acquired remains with the resident. A message marked received means delivery, not proof of eye contact or attention.

Threat/eyewitness cards offer **Contact original witness / file report**. The original recorded witness must still be alive, present, awake and reachable face to face or through devices. Existing evidence and reporting rules then apply to that witness. The recipient of hearsay does not gain eyewitness testimony. A body-only or unidentified observation never learns the culprit's name by being forwarded.

## NPCs and limits

Actual autonomous conversations exchange at most one new report each way, with proximity, line of sight and awake/peaceful participants required. Automatic residents read pending device messages when they have access. Trusted contacts can share one saved report in each direction per hour using available devices. Witness and body-discovery reports are collected from the real perception records. None of this moves money or items or grants remote access to a private facility.

Residents keep at most 24 recent source-specific reports. New observations replace the older report about the same subject from the same observer. Independent sources remain distinct. Requests for help become stale after 30 game minutes; other current service/work/trade reports after one game hour. Historical threat/witness accounts retain their observation time without being labelled current service information. Death and departure preserve existing historical identities; an unavailable source cannot answer a callback.

Reports, pending messages, original source/time/place and the sharing cooldown survive saves. Delivery and duplicate-send indices are reconstructed on load. Older text-only saves default to no invented report knowledge. Free text remains free text and is never promoted to a trusted structured claim.

Personality-dependent interpretation/refusal and deeper decisions using this information remain **M01.3**. The broader relationship/history presentation and milestone acceptance review remain **M01.4**. This checkpoint completes communication plumbing, not those later tasks or the entire social milestone.

## Reproduce

- `ctest --test-dir BUILD -R social_information_channels --output-on-failure`
- `BUILD/mega_dormitory --playtest-information --width 1000 --height 720`

The rendered playthrough starts an offline recipient with a pending service report. They actually walk to a public computer, pay its owner, receive the report, leave device range and walk to the reported service. It checks continuous movement, delivery/source/time and the real account transfer.
