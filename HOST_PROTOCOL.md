# Host-authoritative canvas replication

Persistent canvas objects use the hosted protocol (`host_protocol: 1`). Cursor,
laser and editor presence still use direct peer channels. Media connections are
unchanged. Object drawing previews remain local until approved by the host.

## Election and fencing

The signaling registry supplies a complete membership and a unique `host.term`.
The highest eligible peer ID is selected using the priority rule of the
[Bully algorithm](https://csis.pace.edu/~marchese/CS865/Papers/garcia_elections.pdf).
Because every participant already receives the full server membership, the
registry evaluates the priority directly rather than racing timeout elections.
An eligible host is an administrator or a participant whose groups cover every
active participant's groups. If nobody qualifies, editing stays blocked rather
than leaking group-restricted objects to an unauthorized host.

Joining/leaving changes the term and freezes editing. Loss of a DataChannel
alone never removes a participant or elects an independent host. The server
rejects persistence from any socket other than the bound elected host, and from
any obsolete term. This applies to direct legacy object messages too.

## Initial state and host changes

1. Load the server's persisted objects as a baseline, under a loading overlay.
2. The coordinator gathers all members' **approved** object records and deletion
   tombstones, merging by version. Local drafts are excluded.
3. Send an ACL-filtered complete snapshot to each member on the reliable ordered
   sync channel. Large snapshots are partitioned by object.
4. Wait for every snapshot acknowledgement before announcing `host_ready`.
5. Only then enable pointer interaction, keyboard shortcuts and the code API.

All active members must be reachable during this barrier. This intentionally
prefers consistent initialization over availability. A successor re-saves the
approved records so an unsaved predecessor update that reached another member
is not lost. Snapshot and live-commit sequence gaps force another sync barrier.

## Editing and rates

Editing handlers build private drafts. `useHostCanvas.setItems` turns field
differences into proposals; drafts do not update rendered object state. Text,
code and notes carry Automerge documents, which the host merges to preserve
concurrent edits. The host validates the whole proposal and the sender's object
permissions before assigning an approved version and sequence.

- Proposals to the host: 30 FPS target.
- Host-approved broadcasts to followers: 30 FPS target.
- Coalesced host persistence batches: 10 FPS target.

These are time-based sampling rates, not counts of browser paint frames. Timer
throttling can reduce delivery frequency; safety does not depend on the timer.
Snapshots, commits and acknowledgements share one ordered DataChannel so bulk
transfers cannot overtake initialization. Duplicate proposal IDs and old terms
are ignored. Storage errors freeze the session and require recovery instead of
silently applying independent follower state.

`host_batch_result: {ok: true, status: "queued"}` confirms admission to the
existing server FIFO persistence queue, **not** completion of a Redis write.
Host acknowledgement is likewise distinct from durable storage: if the host
and every replica disappear before storage completes, this protocol cannot
recover the lost state. A stronger guarantee needs durable server acknowledgements
or a consensus log before presenting changes as durable.

## Deployment and checks

Deploy the matching backend hosted protocol with the frontend. An older backend
does not provide host terms or support `host_item_batch`; the frontend therefore
keeps the loading gate closed. Existing legacy participants must reload before a
hosted session can become ready.

```sh
node --test tests/hostCanvas.test.js tests/canvasManifest.test.js
npm run build
```

The backend adds `cpp/tests/canvas_host_test.cpp` to CTest. A standalone election
and fencing check can also be built without the server's external dependencies:

```sh
c++ -std=c++17 -I cpp/include cpp/tests/canvas_host_test.cpp -o /tmp/canvas-host-test
/tmp/canvas-host-test
```
