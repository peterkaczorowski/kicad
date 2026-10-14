# KiCad Managed Design Workspace (MDW) Backend Protocol

This is the normative specification for the MDW backend protocol. It is written
for implementers building a backend in a language other than C++. The C++ SDK
under `mdw/sdk` is the reference implementation; where this document and the SDK
disagree, the SDK and the `.proto` files are authoritative.

All message and field names below come directly from the proto sources:

- `api/proto/mdw/mdw_envelope.proto` — the wire envelope
- `api/proto/mdw/backend_base.proto` — the mandatory surface
- `api/proto/mdw/backend_*.proto` — the optional capability groups
- `api/proto/workspace/workspace_types.proto` — the shared vocabulary

The proto package for envelope and backend messages is `kiapi.mdw`; the shared
vocabulary is `kiapi.workspace`.

## 1. Overview

KiCad manages the backend the way an LSP client manages a language server. KiCad
spawns the backend executable as a child process, owns its entire lifecycle, and
tears it down on exit. There is no long-running daemon and no network listener
that a backend author has to manage; the backend is a short-lived subprocess of
one KiCad instance.

All communication is Protocol Buffers (proto3) framed on nng sockets. There are
no proto `service` blocks. Dispatch is method-per-message: KiCad packs a request
message into an envelope, and the backend routes on the payload's protobuf type
name. This mirrors KiCad's existing `API_HANDLER` model, where the call status
rides the envelope and the payload is pure data.

## 2. Transport

Two nng sockets, both reliable request/reply (`REQ`/`REP`), never pub/sub.

**Control channel** — KiCad calls the backend.

- Backend opens a `REP` socket and `nng_listen`s on the control URL.
- KiCad is the `REQ` client and drives every call.
- One `MdwRequest` in, one `MdwResponse` out, strictly paired.

**Event channel** — backend notifies KiCad, roles reversed.

- Backend opens a `REQ` socket and `nng_dial`s the events URL (non-blocking dial,
  because KiCad may bind its event server slightly after spawn).
- Backend pushes a `PublishEvent`; KiCad replies with `PublishEventAck`.

Both URLs are `ipc://` endpoints (a local domain socket / named pipe). No TCP.

The event channel is deliberately reliable `REQ`/`REP` rather than lossy pub/sub.
Lock and lifecycle events are authoritative inputs to KiCad's state tracker, not
advisory hints. A dropped `ET_ITEM_LOCKED` would leave KiCad's view of the lock
table wrong. Reliable delivery with per-message acknowledgement, retry, and
dedup-by-`seq` (Section 8) is the price of correctness.

The backend may serve control requests concurrently; the reference server runs
four blocking worker threads over `nng_ctx` contexts on the one `REP` socket.

## 3. Launch contract

KiCad spawns the backend with a fixed argument vector:

```
backend --control <control-url> --events <events-url> --parent-pid <pid> --kicad-mdw
```

- `--control <url>` — the `ipc://` URL the backend must `listen` on for control.
- `--events <url>` — the `ipc://` URL the backend must `dial` to push events.
- `--parent-pid <pid>` — KiCad's process id, for parent-death handling.
- `--kicad-mdw` — a marker identifying an MDW launch. The reference parser
  ignores unrecognized tokens, so a backend may treat this as informational.

The **channel token** is delivered out of band, in the `KICAD_MDW_TOKEN`
environment variable. It is never passed on argv, because argv is visible to
other processes on most platforms. The reference parser
(`MDW_ParseLaunch`) reads `--control`, `--events`, `--parent-pid` from argv and
`KICAD_MDW_TOKEN` from the environment, and considers the launch valid when a
control URL is present.

The token is echoed in the `channel_token` field of every envelope in both
directions (`MdwRequest`, `MdwResponse`, `PublishEvent`, `PublishEventAck`). See
Sections 4 and 9.

## 4. Envelope

Every control message is wrapped. The payload is a `google.protobuf.Any`.

```
message MdwRequest {
  kiapi.workspace.Version protocol_version = 1;
  string                  channel_token    = 2;
  string                  request_id       = 3;
  google.protobuf.Any     payload          = 4;
}

message MdwResponse {
  string                         channel_token = 1;
  string                         request_id    = 2;
  kiapi.workspace.WorkspaceError error         = 3;
  google.protobuf.Any            payload       = 4;
}
```

- `protocol_version` is a `Version { major, minor, patch, full }`. Only `major`
  and `minor` participate in negotiation (Section 5).
- `channel_token` carries the per-spawn secret on every message.
- `request_id` correlates a call with progress and with `CancelRequest`
  (Section 7). It is opaque to the backend; echo it back on the response.
- `payload` holds the request or reply message, packed with its type URL.

**The response `error` is the single error channel.** `WorkspaceError.code` is a
`WorkspaceErrorCode`; `WE_OK` (0) means success. Reply payload messages carry no
error field of their own — status rides the envelope and the payload is pure
data. On any non-`WE_OK` code, treat the payload as absent (the reference server
does not pack a payload unless the handler returned `WE_OK`).

**Dispatch is keyed by the payload's protobuf full type name.** The backend
takes the `type_url` of the `Any`, strips everything up to and including the last
`/`, and looks up a handler by the resulting fully-qualified message name (for
example `kiapi.mdw.Hello`). No handler registered for that name yields
`WE_NOT_SUPPORTED` (Section 6).

**Token validation.** If the backend has a non-empty token and the request's
`channel_token` does not match, it must reject the call with `WE_PROTOCOL`
("channel token mismatch") and no payload. Always set `channel_token` and
`request_id` on the response, even for rejections.

**Malformed input.** A request envelope that fails to parse, or a payload that
fails to unpack into the handler's request type, is answered with `WE_PROTOCOL`.

## 5. Handshake

The first call on the control channel is `Hello`; the reply is `HelloReply`.

```
message Hello {
  kiapi.workspace.Version protocol_version = 1;
  string                  kicad_version    = 2;
  string                  channel_token    = 3;
  string                  events_url       = 4;
}

message HelloReply {
  kiapi.workspace.Version      protocol_version = 1;
  kiapi.workspace.BackendInfo  backend_info     = 2;
  kiapi.workspace.Capabilities capabilities     = 3;
}
```

Version rule: the **major** protocol version must match between KiCad and the
backend. A major mismatch is a hard failure — the reference `Hello` handler
returns `WE_PROTOCOL` ("protocol major mismatch"). **Minor** versions are
additive-only within a major: a peer may speak a higher minor and must tolerate
a lower one, since higher minors only add fields and messages. The reference SDK
advertises the protocol version through the constants
`MDW_PROTOCOL_VERSION_MAJOR` and `MDW_PROTOCOL_VERSION_MINOR` (currently 1 and 0).

`HelloReply` returns `BackendInfo` (identity: `backend_id`, `display_name`,
`vendor`, `version`, and so on) and `Capabilities`. KiCad gates all UI and all
further calls on this reply, so the handshake must fully describe the backend.

## 6. Capability model

`Capabilities` advertises which optional feature groups the backend implements:

```
message Capabilities {
  repeated CapabilityGroup groups           = 1;
  bool                     supports_offline = 2; // reserved
  repeated CustomState     custom_states    = 3;
}
```

`CapabilityGroup` (in `kiapi.workspace`):

```
CG_UNKNOWN=0  CG_LOCKING=1  CG_REVISIONS=2  CG_ATTRIBUTES=3  CG_LIFECYCLE=4
CG_EVENTS=5   CG_SEARCH=6   CG_DIFF=7       CG_FORGE=8       CG_SHARED_LIBRARIES=9
CG_BOM=10     CG_ECO=11     CG_REVIEW=12    CG_PARTS=13      CG_PERMISSIONS=14
```

A request for a message belonging to an unadvertised group — or any message with
no registered handler — returns `WorkspaceError.code = WE_NOT_SUPPORTED`. This is
the capability-honesty guarantee: a backend cannot claim a group it does not
serve, and cannot silently ignore a call. KiCad consults `groups` before exposing
the corresponding UI or dispatching any group call.

`custom_states` lets the backend define display states (`CustomState`) layered on
the four universal `LifecycleCategory` values (`LC_EDITABLE`, `LC_PENDING_REVIEW`,
`LC_RESTRICTED`, `LC_RELEASED`). The framework shows the custom `display_name` but
reasons only about the `category`.

### Mandatory surface (always present)

Every backend answers these regardless of advertised groups
(`backend_base.proto`):

- Handshake / identity: `Hello`, `GetBackendInfoRequest`,
  `GetCapabilitiesRequest`, `GetSchemaRequest`.
- Auth: `AuthenticateRequest`, `LogoutRequest`, `IsAuthenticatedRequest`.
- Navigation: `GetRootFoldersRequest`, `GetChildFoldersRequest`,
  `ListItemsRequest`, `GetItemInfoRequest`.
- Checkout/checkin: `CheckoutRequest`, `CheckinRequest`, `CancelCheckoutRequest`.
- Liveness/lifecycle: `PingRequest`, `ByeRequest`, `CancelRequestRequest`
  (Section 7).

Checkout is not a capability group; it is part of the mandatory surface.

### Optional groups (advertise to enable)

| Group | Proto | Principal messages |
|-------|-------|--------------------|
| `CG_LOCKING` | `backend_locking.proto` | `LockRequest`, `UnlockRequest`, `GetLockInfoRequest` |
| `CG_REVISIONS` | `backend_revisions.proto` | `GetRevisionHistoryRequest`, `DownloadRevisionRequest` |
| `CG_ATTRIBUTES` | `backend_attributes.proto` | `GetAttributesRequest`, `SetAttributesRequest` |
| `CG_LIFECYCLE` | `backend_lifecycle.proto` | `GetAvailableTransitionsRequest`, `TransitionLifecycleRequest` |
| `CG_EVENTS` | `backend_events.proto` | `SubscribeEventsRequest`, `UnsubscribeEventsRequest` |
| `CG_SEARCH` | `backend_search.proto` | `GetSearchSchemaRequest`, `SearchRequest` |
| `CG_DIFF` | `backend_diff.proto` | `GetChangeSummaryRequest` |
| `CG_FORGE` | `backend_forge.proto` | `GetForgeInfoRequest`, `ListMergeRequestsRequest` |
| `CG_SHARED_LIBRARIES` | `backend_shared_libs.proto` | `ListSharedLibrariesRequest`, `DownloadSharedLibraryRequest` |
| `CG_BOM` | `backend_bom.proto` | `GetBomSchemaRequest`, `GetBomRequest` |
| `CG_ECO` | `backend_eco.proto` | `GetEcoSchemaRequest`, `GetEcoRequest` |
| `CG_REVIEW` | `backend_review.proto` | `GetReviewSchemaRequest`, `GetReviewRequest` |
| `CG_PARTS` | `backend_parts.proto` | `GetPartRequest`, `SearchPartsRequest` |
| `CG_PERMISSIONS` | `backend_permissions.proto` | `GetPermissionsRequest` |

Note that `CG_EVENTS` controls the subscription handshake only; the events
themselves flow on the reversed socket (Sections 2 and 8). The framework's
visual schematic/PCB diff needs only `CG_REVISIONS` (it downloads two revisions
and diffs locally); `CG_DIFF` is a separate backend-computed change summary.

Mutating requests (`Checkout`, `Checkin`, `SetAttributes`, `Lock`, `Unlock`,
`TransitionLifecycle`, …) carry a client-generated `idempotency_id`. A retry
after a mid-call crash presents the same id, and the backend must not double
apply.

## 7. Lifecycle

```
message PingRequest {}          message PingReply {}
message ByeRequest {}           message ByeReply {}
message CancelRequestRequest { string request_id = 1; }   message CancelRequestReply {}
```

- **Ping** — idle health probe. Answer `PingReply` promptly; KiCad uses it to
  confirm the backend is alive between real calls.
- **Bye** — request for a clean shutdown. On receipt the backend flips its
  "bye received" flag; the main scaffold then stops the event publisher, stops
  the control server, and exits 0. Reply `ByeReply` before exiting.
- **CancelRequest** — best-effort abort of an in-flight `request_id`. It is
  advisory: a backend that cannot cancel simply lets the original call finish.

**Parent-death handling.** The backend must not outlive KiCad. On Linux the
reference scaffold calls `prctl(PR_SET_PDEATHSIG, SIGTERM)` so the kernel signals
the backend when KiCad dies. It also installs `SIGTERM`/`SIGINT` handlers that set
a terminate flag and shut down cleanly. On platforms without `PR_SET_PDEATHSIG`,
use `--parent-pid` to watch the parent and self-exit when it disappears.

**Control-socket close.** Closing the control socket aborts each worker's blocking
receive (`NNG_ECLOSED`); the workers then exit. A backend that observes its
control channel drop should treat it as a shutdown signal and exit.

## 8. Events

Events are pushed on the reversed channel and wrapped in `PublishEvent`; KiCad
replies `PublishEventAck`.

```
message PublishEvent    { string channel_token = 1; kiapi.workspace.EventEnvelope event = 2; }
message PublishEventAck { string channel_token = 1; uint64 seq = 2; }
```

`EventEnvelope` (in `kiapi.workspace`):

```
message EventEnvelope {
  EventType type               = 1;
  string    workspace_id       = 2;
  string    item_id            = 3;
  string    client_identity    = 4;
  string    payload            = 5; // JSON, opaque to the framework
  uint64    seq                = 6;
  int64     timestamp_unix     = 7;
  string    backend_event_type = 8; // set only for ET_BACKEND_EVENT
}
```

- `type` is an `EventType`: `ET_WORKSPACE_BOUND`, `ET_WORKSPACE_UNBOUND`,
  `ET_WORKSPACE_RECONNECTED`, `ET_ITEM_LOCKED`, `ET_ITEM_UNLOCKED`,
  `ET_REVISION_CHECKED_IN`, `ET_REVISION_CHECKED_OUT`,
  `ET_LIFECYCLE_CATEGORY_CHANGED`, `ET_CUSTOM_STATE_CHANGED`, `ET_BACKEND_EVENT`.
- `seq` is **monotonic per workspace**. KiCad uses gaps to detect missed events
  and resync via the getters, and uses `seq` to dedup duplicate deliveries.
- `client_identity` names the issuer of the originating mutation, so a client can
  distinguish its own echo from a teammate's change.
- `payload` is free-form JSON, opaque to the framework. `backend_event_type` is
  set only when `type == ET_BACKEND_EVENT`.

**Reliable delivery.** Send the `PublishEvent`, wait for the `PublishEventAck`,
and retry on failure or timeout until acknowledged or shutdown. The reference
publisher queues events, sends with a 2000 ms per-attempt timeout, and re-sends
every ~200 ms until acked. Because retries can duplicate an event, KiCad dedups
by `seq`; assign each event a fresh monotonic `seq` and reuse it across retries
of the same event.

## 9. Credentials and security

- **Credentials cross the socket only inside `Authenticate`.** `Credentials`
  carries `method` (`"password"`, `"token"`, `"oauth"`), a `fields` map of
  scheme-specific material, and an optional `refresh_token`. No credential
  material appears on any other message.
- **The backend never touches the platform keychain.** KiCad brokers all
  persistence. When the backend mints or rotates a refresh token, it returns it
  in `AuthResult.refresh_token`; KiCad stores it in its own credential store and
  replays it inside a later `Authenticate` on reconnect.
- **Per-spawn random token.** The `channel_token` is a fresh secret generated by
  KiCad for each spawn, delivered via `KICAD_MDW_TOKEN` (never argv), and required
  to match on every control request. It authenticates the peer on a local socket;
  it is not a user credential.
- **Filesystem isolation.** The `ipc://` endpoints live in a private directory
  created mode `0700` with owner-only sockets, so no other local user can connect
  to the control or event channel.

`AuthResult` fields: `authenticated`, `user_id`, `display_name`,
`refresh_token`. `IsAuthenticatedReply.authenticated` reports current session
state; `WE_NOT_AUTHENTICATED` is returned by calls that require a session the
backend does not have.

## 10. Implementing a backend in another language

1. Generate code from `api/proto/mdw/*.proto` and
   `api/proto/workspace/workspace_types.proto` with your language's protoc plugin.
2. Bind an nng `REP` socket and `listen` on `--control`; open an nng `REQ` socket
   and non-blocking `dial` on `--events`. Read the token from `KICAD_MDW_TOKEN`.
3. Read one length-framed `MdwRequest` per control transaction, validate
   `channel_token`, unpack `payload` by its type name, dispatch, and reply with a
   single `MdwResponse` carrying `channel_token`, `request_id`, `error`, and, on
   `WE_OK`, the packed reply.
4. Return `WE_NOT_SUPPORTED` for any message you do not handle; return
   `WE_PROTOCOL` for malformed envelopes, unpack failures, token mismatch, and
   major version mismatch.
5. Implement the mandatory surface (Section 6). Advertise in `HelloReply` only
   the `CapabilityGroup`s you actually serve.
6. Echo `request_id`. Honor `idempotency_id` on mutating calls. Treat
   `CancelRequest` as best-effort.
7. For events, assign a per-workspace monotonic `seq`, push `PublishEvent`, wait
   for `PublishEventAck`, and retry until acked or shutdown.
8. Handle shutdown: reply to `Bye` then exit; self-terminate on parent death and
   on control-socket close; never orphan.
9. Keep the protocol pure protobuf — no host-string types, no framework
   dependencies — so the contract stays language-neutral.

## 11. Reserved: offline operation

The `supports_offline` capability scalar (Section 6) is reserved and not yet
implemented. The offline operation queue it will eventually gate — buffering
mutating requests while a backend or its store is unreachable and replaying them
on reconnect — is deferred. There is no queue, no persistence, and no replay in
the current framework or SDK.

Until it lands, treat `supports_offline` as always false: leave it at its
protobuf default in `HelloReply`, do not advertise offline behaviour, and do not
implement any offline-specific semantics. When the feature is specified, it will
be additive within protocol major version 1 (a higher minor), so a backend that
ignores the field today stays conformant.

## 12. Local-file to item binding

The workspace commands identify items by `item_id`. KiCad's editors, by
contrast, open a local file. The binding between the two lives on the KiCad
side, in the project file, so no protocol field carries it and a backend needs
to do nothing to support it.

`.kicad_pro` stores a `workspace.items` object mapping each checked-out file to
the item it came from:

```json
"workspace": {
    "backend": "org.kicad.mdw.git",
    "workspace_id": "github.com/example/widget",
    "items": {
        "board.kicad_pcb": "item-42"
    }
}
```

Keys are relative to the project directory, because projects are routinely moved
and shared and an absolute key would stop matching the moment they are. A file
with no entry is simply not under workspace control, which is what makes the
lookup free for unmanaged projects.

This is what lets an editor resolve the document it has open back to an item,
and it is what the save warning on a write-blocking item is built on. A backend
sees only that `GetLifecycleCategory` is called with an `item_id` it already
knows.
