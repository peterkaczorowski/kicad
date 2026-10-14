# Writing an MDW Backend in C++

This is the how-to for building a Managed Design Workspace (MDW) backend on the
C++ SDK under `mdw/sdk`. It is normative for backends written against the SDK.
The wire contract itself is specified in [`protocol.md`](protocol.md); read that
first if you are implementing in another language. Everything below is grounded
in the shipped SDK headers, the worked mock in `qa/tests/mdw/mock`, and the two
reference backends in `plugins/mdw`.

## 1. Overview

A backend is a standalone executable. KiCad spawns it the way an LSP client
spawns a language server: one child process per KiCad instance, whose entire
lifecycle KiCad owns. There is no daemon, no listener you manage, and no shared
state between KiCad runs.

The backend links `kiapi` and `nng` only, plus whatever it needs to talk to its
own store (the git backend adds nothing; the odoo backend adds libcurl and
nlohmann_json). It must **never** link `kicommon` or wxWidgets. That constraint
is the point, not an accident: keeping wxWidgets out means `wxString` can never
leak into the protocol. Every field crossing the socket is a protobuf message
built from `std::string` and plain scalars, so the contract stays
language-neutral and a third-party backend builds against a minimal surface. The
SDK header says it directly:

> The MDW backend SDK links kiapi and nng only. It must never include kicommon
> or wxWidgets; that constraint is what keeps wxString out of the protocol.

Transport is Protocol Buffers over two nng `REQ`/`REP` sockets. The SDK hides
both: you register typed handlers and publish typed events, and the scaffold
runs the sockets.

## 2. The manifest

KiCad discovers a backend from a JSON manifest named `<name>.kicad-mdw.json`.
`MDW_BACKEND_REGISTRY::Discover` scans two directories, each with a `/mdw`
suffix:

- `PATHS::GetStockPluginsPath() + "/mdw"` — the stock (install-tree) plugins dir
- `PATHS::GetUserPluginsPath() + "/mdw"` — the per-user plugins dir

Every `*.kicad-mdw.json` in those directories is parsed. `id` and `executable`
are required; a manifest missing either is rejected. `executable` is resolved
relative to the manifest's own directory, and on Windows the registry appends
`.exe`. The git reference manifest
(`plugins/mdw/git/org.kicad.mdw.git.kicad-mdw.json`):

```json
{
    "id": "org.kicad.mdw.git",
    "name": "Git Workspace",
    "version": "1.0.0",
    "protocol_version": {
        "major": 1,
        "minor": 0
    },
    "executable": "kicad-mdw-git",
    "schemes": [ "git" ],
    "capability_hints": [ "REVISIONS" ]
}
```

Fields, as read by `parseManifest`:

- `id` — the backend id, matched against `BackendInfo.backend_id` from the
  handshake. Required.
- `name` — display name.
- `version` — free-form version string.
- `protocol_version` `{ major, minor }` — the protocol the manifest claims;
  default 0/0 when absent.
- `executable` — the program to spawn, relative to the manifest directory.
  Required.
- `schemes` — URL schemes this backend handles (`"git"`, `"odoo"`, `"https"`).
- `capability_hints` — a pre-handshake advisory list of `CapabilityGroup` names
  (`"REVISIONS"`, `"ATTRIBUTES"`, …). Hints only; the authoritative capability
  set is the `Capabilities` returned in `HelloReply`.

## 3. The main scaffold

A backend is a `main()` that does five things: parse the launch, build a
descriptor, build a server and register handlers, build an event publisher if it
advertises `CG_EVENTS`, and hand all of it to `MDW_RunBackend`. This mirrors the mock
(`qa/tests/mdw/mock/kicad_mdw_mock.cpp`). A minimal skeleton:

```cpp
#include <mdw_sdk/mdw_backend_server.h>
#include <mdw_sdk/mdw_run_backend.h>

#include <api/mdw/backend_base.pb.h>
#include <api/workspace/workspace_types.pb.h>

namespace mdw = kiapi::mdw;
namespace ws  = kiapi::workspace;

int main( int argc, char** argv )
{
    MDW_BACKEND_LAUNCH launch = MDW_ParseLaunch( argc, argv );

    MDW_BACKEND_DESCRIPTOR descriptor;
    descriptor.info.set_backend_id( "org.example.mdw.demo" );
    descriptor.info.set_display_name( "Demo Workspace" );
    descriptor.info.mutable_version()->set_major( 1 );
    descriptor.info.mutable_version()->set_minor( 0 );
    descriptor.capabilities.add_groups( ws::CG_REVISIONS );   // advertise only what you serve
    descriptor.schema.set_schema_version( "1" );

    MDW_BACKEND_SERVER server;

    server.RegisterHandler<mdw::AuthenticateRequest, mdw::AuthResult>(
            []( const mdw::AuthenticateRequest&, mdw::AuthResult& aReply ) -> ws::WorkspaceError
            {
                aReply.set_authenticated( true );
                ws::WorkspaceError ok;
                ok.set_code( ws::WE_OK );
                return ok;
            } );

    // ... register the rest of the mandatory surface and each advertised group ...

    // No CG_EVENTS advertised above, so no publisher.
    return MDW_RunBackend( launch, descriptor, server, nullptr );
}
```

What the pieces do:

- **`MDW_ParseLaunch( argc, argv )`** pulls `--control`, `--events`, and
  `--parent-pid` from argv, and the channel token from the `KICAD_MDW_TOKEN`
  environment variable (never argv). It marks the launch `valid` when a control
  URL is present.
- **`MDW_BACKEND_DESCRIPTOR`** is the handshake payload: `info`
  (`kiapi::workspace::BackendInfo`), `capabilities`
  (`kiapi::workspace::Capabilities`, filled with `add_groups(CG_*)`), and
  `schema` (`kiapi::workspace::Schema`).
- **`MDW_BACKEND_SERVER::RegisterHandler<Req, Resp>`** installs one typed
  handler per request message. The lambda takes `const Req&`, fills `Resp&`, and
  returns a `WorkspaceError`. The template unpacks the payload, calls you, and,
  on `WE_OK`, packs your `Resp` into the response envelope. All handlers must be
  registered before `MDW_RunBackend` starts the workers; the handler map is not
  guarded for concurrent registration once serving.
- **`MDW_EVENT_PUBLISHER`** is only for a backend advertising `CG_EVENTS`.
  Construct it with the channel token and pass it in. Pass `nullptr` otherwise:
  one costs a dialed socket and a thread that would never carry anything.
- **`MDW_RunBackend( launch, descriptor, server, publisher )`** registers the
  built-in `Hello`, `GetBackendInfo`, `GetCapabilities`, `GetSchema`, `Ping`,
  and `Bye` handlers, sets the channel token on the server, starts the control
  server with four worker threads, starts the publisher on the events URL when
  one was given, installs parent-death and signal handling, then blocks until a
  `Bye` request, a termination signal, or parent death. It returns a process
  exit code.

You do not write the handshake, liveness, or shutdown handlers, and you do not
touch the channel token — the scaffold owns all of that.

## 4. Mandatory surface vs. capability groups

The **mandatory surface** is answered by every backend regardless of advertised
groups. It is defined in `api/proto/mdw/backend_base.proto`:

- Handshake / identity: `Hello`, `GetBackendInfoRequest`,
  `GetCapabilitiesRequest`, `GetSchemaRequest` — auto-registered by the scaffold.
- Auth: `AuthenticateRequest`, `LogoutRequest`, `IsAuthenticatedRequest`.
- Navigation: `GetRootFoldersRequest`, `GetChildFoldersRequest`,
  `ListItemsRequest`, `GetItemInfoRequest`.
- Checkout/checkin: `CheckoutRequest`, `CheckinRequest`, `CancelCheckoutRequest`.
- Liveness/shutdown: `PingRequest`, `ByeRequest`, `CancelRequestRequest` — the
  first two auto-registered by the scaffold; `CancelRequest` is best-effort and
  you register a handler only if you can act on it.

Checkout is **not** a capability group. It is part of the mandatory surface, so
you always register the auth, navigation, and checkout handlers yourself.

The **optional groups** are the `CapabilityGroup` enum in
`kiapi::workspace` (`workspace_types.proto`):

```
CG_UNKNOWN=0  CG_LOCKING=1  CG_REVISIONS=2  CG_ATTRIBUTES=3  CG_LIFECYCLE=4
CG_EVENTS=5   CG_SEARCH=6   CG_DIFF=7       CG_FORGE=8       CG_SHARED_LIBRARIES=9
CG_BOM=10     CG_ECO=11     CG_REVIEW=12    CG_PARTS=13      CG_PERMISSIONS=14
```

The rule the mock demonstrates: advertise a group in `descriptor.capabilities`
**and** register that group's handlers, or do neither. The mock guards each
group's registration behind its advertised set:

```cpp
if( hasGroup( "LOCKING" ) )
    registerLockingHandlers( server, &publisher, &seq, events );

if( hasGroup( "REVISIONS" ) )
    registerRevisionHandlers( server );
```

A request for a message with **no registered handler** is auto-answered
`WE_NOT_SUPPORTED` by the server. That is the capability-honesty guarantee, and
it is why you must not hand-answer an unadvertised message: leaving it
unregistered is the correct, checked behaviour. The conformance test
`CapabilityHonestyBothDirections` verifies exactly this — an unadvertised
`GetForgeInfoRequest` must come back `NOT_SUPPORTED`.

## 5. Error handling

Every handler returns a `kiapi::workspace::WorkspaceError`. `WE_OK` (code 0)
means success and causes the scaffold to pack your reply; any other code is
returned with no payload. Build both with the helpers in
`mdw_sdk/mdw_backend_server.h`:

```cpp
return MdwOk();
return MdwFail( ws::WE_ITEM_NOT_FOUND, "no such item" );
```

The `MdwResponse.error` envelope field is the **single** error channel. Reply
payload messages carry no error field of their own — status rides the envelope,
the payload is pure data. Common codes seen in the reference backends:
`WE_ITEM_NOT_FOUND`, `WE_INTERNAL`, `WE_AUTH_FAILED`, `WE_NOT_AUTHENTICATED`,
`WE_PERMISSION_DENIED`, `WE_VERSION_CONFLICT`, `WE_NO_CHANGES`,
`WE_INVALID_PARAMETER`, `WE_REVISION_NOT_FOUND`, `WE_TIMEOUT`, `WE_NETWORK`.

Mutating requests (`Checkout`, `Checkin`, `SetAttributes`, `Lock`, `Unlock`,
`TransitionLifecycle`, …) carry a client-generated `idempotency_id`. A retry
after a mid-call crash presents the same id; do not double-apply.

## 6. Events

Events flow on the reversed socket, and only matter if you advertise
`CG_EVENTS`. Construct an `MDW_EVENT_PUBLISHER` with the channel token, pass it
to `MDW_RunBackend` (which dials the events URL and starts it), then `Publish`
a `kiapi::workspace::EventEnvelope`. The mock's emitter:

```cpp
ws::EventEnvelope event;
event.set_type( ws::ET_REVISION_CHECKED_IN );
event.set_workspace_id( "mock" );
event.set_item_id( itemId );
event.set_seq( seq.fetch_add( 1 ) + 1 );   // monotonic per workspace
publisher.Publish( event );
```

`seq` must be monotonic per workspace. The publisher delivers reliably: it
queues the event, sends with a per-attempt timeout, and re-sends until KiCad
acknowledges. Retries can duplicate an event, so KiCad dedups by `seq` — assign
each event a fresh `seq` and reuse it across retries of the same event. Lock and
lifecycle events are authoritative tracker inputs, not advisory hints; that is
why the channel is reliable `REQ`/`REP` rather than pub/sub. `Publish` returns
false (a no-op) until the publisher is running.

## 7. Credentials and security

- **Credentials arrive only inside `Authenticate`.** `Credentials` carries
  `method` (`"password"`, `"token"`, `"oauth"`), a `fields` map of
  scheme-specific material, and an optional `refresh_token`. No credential
  material appears on any other message. The odoo backend reads `username` and
  `password`/`api_key` out of `credentials().fields()`.
- **Never touch the platform keychain.** KiCad brokers all persistence. When you
  mint or rotate a refresh token, return it in `AuthResult.refresh_token`; KiCad
  stores it and replays it inside a later `Authenticate` on reconnect.
- **The per-spawn channel token authenticates the peer.** KiCad generates a
  fresh secret per spawn, delivers it via `KICAD_MDW_TOKEN`, and the scaffold
  matches it on every control request. You never handle the token yourself.
- **Validate and escape any request field before it reaches a shell, CLI, or
  URL.** The git backend is the model here. `IsSafeRef` rejects an empty ref, a
  leading `-`, and any character outside `[0-9A-Za-z._/-]`. `IsSafePath` rejects
  empty, a leading `-`, an absolute path, and any `..` component. Every git
  handler that consumes `item_id` or `revision_id` guards it first:

```cpp
if( !IsSafePath( aRequest.item_id() ) )
    return fail( ws::WE_ITEM_NOT_FOUND, "invalid item path" );
```

## 8. Building and installing

The SDK is the static library `mdw_backend_sdk`, which links `kiapi` and the nng
library publicly. A backend links it plus its own dependencies. The git backend:

```cmake
add_executable( kicad-mdw-git kicad_mdw_git.cpp git_operations.cpp )

target_link_libraries( kicad-mdw-git
    mdw_backend_sdk
    ${NNG_LIBRARY}
    )

target_include_directories( kicad-mdw-git PRIVATE ${NNG_INCLUDE_DIR} )
```

The odoo backend adds `${CURL_LIBRARIES}` and the nlohmann_json include dir. To
install, drop the built executable and its `<name>.kicad-mdw.json` manifest into
`${KICAD_PLUGINS}/mdw/` (stock or user plugins tree), where the manifest's
`executable` resolves next to the manifest.

## 9. Conformance

Run the conformance suite (`qa/tests/mdw/conformance`) against your backend. It
spawns a backend through the real supervisor and drives it as a black box. The
behavioural contract it checks:

- **Handshake and identity** — the backend handshakes and reports its
  `backend_id` and advertised groups.
- **Mandatory-method coverage** — auth, navigation, and checkout all answer.
- **Capability honesty in both directions** — an advertised group answers
  normally; an unadvertised message answers `NOT_SUPPORTED`.
- **Schema stability** — repeated `GetSchema` calls return an identical schema.
- **Request timeout** — a slow handler makes a short-timeout call fire `TIMEOUT`
  rather than hang.
- **Recovery from backend death** — killing the backend makes the next call fail
  cleanly (`BACKEND_LOST`/`TIMEOUT`), not deadlock.
- **Clean shutdown** — `Bye` leaves no orphaned process.

The suite parameterizes the mock's advertised set through `MDW_MOCK_CAPABILITIES`
and injects latency through `MDW_MOCK_LATENCY_MS`; a real backend must exhibit
the same black-box behaviour.

## 10. Languages other than C++

The SDK is a convenience, not a requirement. The protocol is protobuf-over-nng
with no `service` blocks and method-per-message dispatch. To implement a backend
in another language, generate code from `api/proto/mdw/*.proto` and
`api/proto/workspace/workspace_types.proto`, bind the two nng sockets, and
follow the wire contract in [`protocol.md`](protocol.md), section 10.
