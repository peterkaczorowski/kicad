# The Odoo Reference Backend

A narrated tour of `plugins/mdw/odoo`, the reference MDW backend that maps an
Odoo model onto the workspace model over JSON-RPC. It is the counterpoint to the
[git backend](MDW_GIT_BACKEND_WALKTHROUGH.md): where git shells out to a CLI,
odoo speaks HTTP JSON-RPC through libcurl, exchanges real credentials, and
advertises `CG_ATTRIBUTES`. Read the
[author guide](MDW_BACKEND_AUTHOR_GUIDE.md) first for SDK mechanics.

Files:

- `kicad_mdw_odoo.cpp` — `main`, the descriptor, and every handler.
- `odoo_rpc_client.{h,cpp}` — the libcurl JSON-RPC 2.0 client.
- `odoo_error_mapper.{h,cpp}` — Odoo/HTTP faults to `WorkspaceError`.
- `org.kicad.mdw.odoo.kicad-mdw.json` — the manifest.

The backend links `kiapi`, `nng`, libcurl, and nlohmann_json — no wxWidgets, no
kicommon.

## The model

Items are records of an Odoo model (default `mdw.item`). Folders are the
distinct values of each record's `folder` field, split on `/` into a tree. Item
`item_id` is the numeric Odoo record id when present, else the record `name`.
Item attributes live in a JSON-encoded `attributes` field on the record.

## Environment configuration

The `ODOO_STATE` singleton is initialized once from four environment variables,
with defaults:

```cpp
state.client.SetServer( envValue( "KICAD_MDW_ODOO_URL", "http://127.0.0.1:8069" ),
                        envValue( "KICAD_MDW_ODOO_DB",  "kicad" ) );
state.model = envValue( "KICAD_MDW_ODOO_MODEL", "mdw.item" );
```

- `KICAD_MDW_ODOO_URL` — base server URL; trailing slashes are trimmed by
  `SetServer`, and `/jsonrpc` is appended per request.
- `KICAD_MDW_ODOO_DB` — the Odoo database name.
- `KICAD_MDW_ODOO_MODEL` — the model whose records are items.
- `KICAD_MDW_ODOO_USER` — the default login, used when `Authenticate` supplies no
  `username` field.

Handlers run on worker threads, so the client and its request counter are
serialized behind `ODOO_STATE::mutex`; every handler takes
`std::lock_guard<std::mutex>` before touching the client.

## The JSON-RPC client

`ODOO_RPC_CLIENT` (`odoo_rpc_client.cpp`) is a wx-free libcurl client that speaks
plain `std::string` and `nlohmann::json`. Its design points:

- **`curl_global_init` once.** `main` calls `curl_global_init(
  CURL_GLOBAL_DEFAULT )` at startup and `curl_global_cleanup()` at exit. Global
  init is not thread-safe, so it must happen once before any worker runs.
- **Per-request easy handle.** `sendRequest` calls `curl_easy_init` /
  `curl_easy_cleanup` around each POST. There is no shared handle, so a request
  cannot corrupt another's state; the mutex already serializes calls.
- **The JSON-RPC 2.0 envelope.** Each call POSTs
  `{"jsonrpc":"2.0","method":"call","params":<params>,"id":<n>}` to
  `<url>/jsonrpc` with `Content-Type: application/json`, capturing the body into
  a `std::string` via a write callback. `id` is a monotonically incrementing
  counter.
- **Timeouts and TLS.** `CURLOPT_TIMEOUT` is 30 s, `CURLOPT_CONNECTTIMEOUT` is
  10 s, and `CURLOPT_NOSIGNAL` is set
  so timeouts are safe in a threaded process. libcurl performs normal TLS
  verification for `https://` URLs; a verification failure surfaces as a curl
  error the mapper classifies as `WE_NETWORK`.

### Authentication and `execute_kw`

`Authenticate` calls the Odoo `common` service `login` method with
`{ database, login, password }`. On a positive integer uid it caches the uid,
login, and password for subsequent calls and sets `m_authenticated`. Crucially,
it calls `Reset()` at the very start of every attempt:

```cpp
// Start every attempt from a clean slate so a failed re-auth cannot leave the
// client running as the previously authenticated user.
Reset();
```

`Reset()` clears the login, password, uid, and authenticated flag. So a failed
re-authentication drops the prior session rather than silently keeping it — the
backend never runs as a stale user. `Logout` calls `Reset()` too, and
`IsAuthenticated` reports the flag.

Model calls go through `ExecuteKw`, which refuses to run unless authenticated
(returning error code 401) and otherwise POSTs the `object` service `execute_kw`
method with the positional argument vector Odoo expects:
`{ database, uid, password, model, method, args, kwargs }`. Three convenience
wrappers build on it:

- `SearchRead( model, domain, fields, limit )` → `search_read`
- `Read( model, ids, fields )` → `read`
- `Write( model, ids, values )` → `write`

### Response parsing

`parseResponse` distinguishes three outcomes: a JSON-RPC `error` object (records
`error_code`, `error_message`, and the nested `data.message`/`data.debug` as
`error_data`); a `result` node (success); or neither (a malformed body). A body
that fails to parse is flagged `is_discarded()` and reported as an invalid
response. A curl transport failure short-circuits earlier in `sendRequest`,
carrying the `CURLcode` in `curl_code`.

## Error mapping

`OdooMapError` (`odoo_error_mapper.cpp`) folds two failure surfaces into one
`WorkspaceErrorCode`:

1. **curl transport codes** (`fromCurl`): `CURLE_OPERATION_TIMEDOUT` →
   `WE_TIMEOUT`; resolve/connect/TLS failures → `WE_NETWORK`.
2. **JSON-RPC faults**, classified by error code and by lowercased substring
   match on the message plus data. `401`/`100`/"session expired"/"authentication"
   → `WE_AUTH_FAILED`; `403`/"permission"/"forbidden"/"accesserror" →
   `WE_PERMISSION_DENIED`; `404`/"not found"/"missingrecord" →
   `WE_ITEM_NOT_FOUND`; "timeout" → `WE_TIMEOUT`; "connection"/"network" →
   `WE_NETWORK`.
3. When nothing matches, it **folds in the HTTP status** (`fromHttpStatus`): 401
   → `WE_NOT_AUTHENTICATED`, 403 → `WE_PERMISSION_DENIED`, 404 →
   `WE_ITEM_NOT_FOUND`, 5xx → `WE_NETWORK`, else the caller's fallback (default
   `WE_INTERNAL`). This catches non-JSON error pages — a proxy's 401 HTML or a
   gateway 5xx that carried no JSON-RPC fault to key off.

Each caller passes a fallback suited to context: `Authenticate` uses
`WE_AUTH_FAILED`, item lookups use `WE_ITEM_NOT_FOUND`.

## The descriptor and the attributes schema

`buildDescriptor` advertises `CG_ATTRIBUTES` and declares a two-attribute
schema (`owner` and `status`, both `AT_STRING`):

```cpp
aDescriptor.info.set_backend_id( "org.kicad.mdw.odoo" );
aDescriptor.info.set_display_name( "Odoo Workspace" );
aDescriptor.capabilities.add_groups( ws::CG_ATTRIBUTES );
aDescriptor.schema.set_schema_version( "1" );
// ... add_attributes( owner ), add_attributes( status ) ...
```

Only the mandatory surface plus the attribute handlers are registered, so the
SDK auto-answers `WE_NOT_SUPPORTED` for every other group. It never advertises
`CG_EVENTS`, so `main` passes `nullptr` for the publisher.

## The handlers

### Item id resolution

`resolveRecordId` is the shared lookup: a numeric `item_id` is taken verbatim; any
other value is resolved by a `search_read` on `[[ "name", "=", item_id ]]`
limited to one record. It returns `-1` when the item does not resolve, and its
out-param carries the underlying RPC result so a lookup failure can be
distinguished from a genuine "no such item".

### Navigation

- `GetRootFolders` returns the single root folder (id `""`, `has_children`
  true) without a round trip.
- `GetChildFolders` runs one `search_read` for the `folder` field across the
  model, then, for the requested `folder_id` prefix, collects the distinct
  next-level path components (deduped through a `std::set`), each becoming a
  `Folder` with `parent_folder_id` set.
- `ListItems` runs `search_read` with domain `[[ "folder", "=", folder_id ]]`,
  fields `{ id, name, folder, state }`, and turns each record into an
  `ItemInfo` via `itemInfo`.
- `GetItemInfo` resolves the id, then `read`s the record's
  `{ id, name, folder, state }`, returning `WE_ITEM_NOT_FOUND` when it does not
  resolve.

`itemInfo` maps a record to `ItemInfo`: `item_id` is the numeric id (or name if
absent), `display_name` is the name, `folder_id` is the `folder` field, and
`category` is `LC_EDITABLE`. `fieldString` normalizes Odoo's untyped JSON —
notably Odoo returns JSON `false` for an unset char/text field, which becomes an
empty string.

### Checkout / checkin

- `Checkout` resolves the id, `read`s the record, returns the `ItemInfo`, and
  echoes the request `local_path`. There is no lock.
- `Checkin` resolves the id and `write`s `{ last_comment: <comment> }` to the
  record, then returns a synthesized `Revision` whose id and timestamp are the
  current Unix time (Odoo records are not version-addressed).
- `CancelCheckout` is a no-op returning `WE_OK`.

### Attributes — `CG_ATTRIBUTES`

- `GetAttributes` resolves the id and `read`s the record's `attributes` field, a
  JSON-encoded string. It parses it (non-throwing; a discarded or non-object
  value yields an empty set) and emits one `AttributeValue` per key, stringifying
  non-string values.
- `SetAttributes` resolves the id, folds the request's `AttributeValue`s into a
  JSON object, and `write`s it back as the `attributes` field's dumped string.

Both return `WE_ITEM_NOT_FOUND` on an empty or unresolvable `item_id`, and route
RPC failures through `OdooMapError`.
