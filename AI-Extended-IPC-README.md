# AI-extended KiCad IPC

This guide describes the extensions in this KiCad fork for AI agents, automation
clients and developers. It is an entry point for a fresh clone: no local task
notes, private project files or previous conversation are required to understand
the supported operations.

The implementation lives on the `schematic-ipc-extensions` branch of this
KiCad 10.0-series fork. The current Python adapter is **`kipy-schematic 0.16.0`**;
the latest functional checkpoint covered here is **`922c790d75`**. Later
documentation-only commits do not require rebuilding that executable. This is
fork-specific functionality, not a claim that an unmodified upstream KiCad or
the upstream Python package implements these commands.

The changes operate on KiCad's **live native models**. An agent can inspect an
unsaved schematic, create symbols and wires, maintain project libraries, update
the PCB and save through KiCad. It does not need to generate replacement project
files or drive editing dialogs for those supported operations. KiCad must still
be running and serving IPC; the Python adapter is not a standalone CAD engine.
No language model or agent framework is bundled with these extensions.

For detailed parameters and additional examples, read the
[Python API guide](api/python/README.md). The protobuf messages, C++ handlers and
tests linked below are the source of truth when extending the implementation.

## 1. Capability map

All rows describe implemented functionality. Version numbers identify the
adapter generation that introduced a feature, rather than unfinished milestones.

| Area | Python entry points | Behavior and important boundaries |
|---|---|---|
| Documents in five editors, 0.5 | `get_open_documents`, `save_document`, `save_copy_of_document`, `revert_document`, `run_action` | Schematic, symbol, PCB, footprint and drawing sheet editors. Explicit documents, dirty-Revert protection and native save/copy behavior. |
| Hierarchical sheets, 0.1 | `get_sheets`, `get_sheet_pins`, `create_sheet` | Read live sheet UUIDs, geometry and ports; create a child or explicitly attach an existing file, with native Undo/Redo. Generic sheet/port Update/Delete is not implemented. |
| Live connectivity, 0.2 | `get_schematic_netlist`, `get_pin_net`, `get_net_members`, `get_item_net`, `get_symbol_connections` | Native hierarchy, aliases, buses, power connections, absolute member information and unsaved changes. IDs belong to a session and connectivity revision. |
| Connectivity display and diagnostics, 0.2 | `highlight_net`, `get_connectivity_issues` | Highlight/clear through the native navigator; report connectivity issues without creating ERC markers. Diagnostics do not replace full ERC. |
| Net rename, 0.3 | `plan_net_rename`, `rename_net` | Preview and apply literal label/port changes across the hierarchy. An existing target net name causes refusal; merging is not an option. |
| Schematic paper, 0.4 | `get_page_settings`, `set_page_settings` | Read actual paper dimensions and custom formats; write standard formats/orientation with one paper-only Undo step. Shared screens require all loaded instances. |
| Live schematic items, 0.6–0.11 | `get_items`, `create_items`, `update_items`, `delete_items`, `add_wire`, `add_label`, `add_text`, `add_junction`, `add_no_connect`, `add_bus_entry`, `add_symbol`, `add_graphic`, `add_shape`, `add_text_box` | Wires/buses, labels, notes, junctions, no-connect flags, bus entries, library-backed symbols, power symbols, shapes and text boxes. Native commits, field masks and affected shared-sheet instances. |
| Symbol endpoints, 0.9 | `pin_position`; symbol readback | Absolute transformed pin positions and instance annotations. Pin definitions are not editable through a placed symbol's item update. Moving a symbol does not drag its wires. |
| Symbol libraries and recovery, 0.10 | `search_symbol_libraries`, `get_symbol_definition`, `create_symbol_library`, `save_symbol_definition`, `recover_schematic_symbol`, `open_library_symbol` | Search global/project tables, read structured definitions, create/register/save project libraries and recover an embedded symbol even without its original library. |
| PCB net safety, 0.12 | `create_items`, `update_items`, `add_track`, `add_via` | Unknown nets refuse by default. New nets require `create_net_if_missing=True` and participate in the same board commit. Pad-net mismatches need an explicit override. |
| PCB placement and endpoints, 0.13 | `place_footprint`, `get_footprint_pads`, `get_pad`, `get_ratsnest`, `get_nets`, `get_connected_items` | Native library placement/replacement, absolute pad data, ratsnest queries and copper primitives. Replacement preserves logical pad nets and protects connected pads/locked items. |
| Schematic-to-PCB update, 0.14 | `export_schematic_for_pcb`, `update_pcb_from_schematic` | Native live or explicitly saved schematic input, dry-run report and native board update. Can share one board Undo step with placement and routing. No implicit fallback to disk. |
| Footprint libraries and recovery, 0.15 | `search_footprint_libraries`, `get_footprint_definition`, `create_footprint_library`, `save_footprint_definition`, `recover_board_footprint` | Native project `.pretty` creation/registration, structured definitions and recovery from a live board, without a Footprint Editor dialog. |
| Project design settings, 0.16 | `get_project_settings`, `set_project_settings`, `edit_project_settings`, `reload_project_settings`, `get_nets_by_netclass` | Live classes, patterns, assignments, constraints, severities, predefined sizes and custom rules. Revision-checked atomic updates, explicit discard on reload and native persistence. |

## 2. Build the application and matching client

### Native KiCad

Use the [upstream build documentation](https://dev-docs.kicad.org/en/build/) for
platform dependencies and toolchain setup. Configure **this checkout**, with
`KICAD_IPC_API=ON`. The project still requires **C++20**; the compatibility
patches described below do not make it a C++17 project.

The following shows the basic CMake shape. Supply any dependency locations,
toolchain and platform options required by your environment:

```sh
cmake -S . -B ../kicad-build -DKICAD_IPC_API=ON
cmake --build ../kicad-build --parallel 4
```

Keep build output outside the source tree. Rebuild affected editor modules and
shared libraries together after native changes; do not assemble an application
from an incomplete build. Install/package using the appropriate platform's
KiCad procedure. This repository does not provide a universal prebuilt package
or a copy of a developer's local dependency environment.

### Python adapter

Use Python **3.10 or later** and `protoc`. Generate descriptors from the same
protocol revision as the application, into a separate directory:

```sh
python3 -m venv ../kicad-ipc-env
. ../kicad-ipc-env/bin/activate
python api/python/build_client.py --output ../kicad-ipc-client
python -m pip install ../kicad-ipc-client
```

On Windows, activate the environment using its `Scripts` directory instead.
`build_client.py --protoc /path/to/protoc` can select a specific compiler.
The generated package declares `kicad-python>=0.8,<0.9` for transport and
`protobuf>=5.29,<8`. Its isolated descriptor pool contains this fork's messages;
installing only the upstream `kipy` package does not add the extensions.

Check the client and native executable separately:

```sh
python -c 'import importlib.metadata; print(importlib.metadata.version("kipy-schematic"))'
kicad-cli version --format commit
kicad-cli version --format about
```

The native executable is the one from your build/package, which may not be the
first `kicad-cli` on PATH. A client version alone does not prove that a running
application supports the same protocol. Regenerate/reinstall the adapter after
protocol changes. Documentation-only changes do not require a protocol bump.

## 3. Select the intended process and document

Enable IPC in KiCad's preferences and open the intended editors. The clients
honor `KICAD_API_SOCKET` and `KICAD_API_TOKEN`; explicit constructor arguments
override them. The default socket is `ipc:///tmp/kicad/api.sock`, but it must not
be assumed to belong to the intended application.

An application process can serve several editors. Separately launched editors
can have different sockets. Discover the socket belonging to the intended
process, then inspect its open documents before editing. On systems with `lsof`,
for example, `lsof -n -a -p "$KICAD_PID" -U` helps identify a selected process's
Unix sockets. Process discovery is platform-specific.

This read-only example uses two explicitly supplied socket environment variables;
they can have the same value when the editors share a server:

```python
import os
from kipy_schematic import SchematicClient, PCBClient

sch = SchematicClient(socket_path=os.environ['SCHEMATIC_IPC_SOCKET'])
pcb = PCBClient(socket_path=os.environ['PCB_IPC_SOCKET'])
for document in sch.get_open_documents():
    print(document)
for document in pcb.get_open_documents():
    print(document)
```

Select and retain the exact `Document` for each operation. Schematic documents
include the project and sheet UUID path; a reused child file can have multiple
instances. `SchematicClient.active_document()` is a convenience for one open
schematic project; `PCBClient.active_document()` requires exactly one board.
Use explicit selection when those assumptions do not match your session.

Document type values are schematic **1**, symbol **2**, PCB **3**, footprint
**4**, drawing sheet **5**. The common document methods on `SchematicClient`
also accept documents from the other editors. An empty result means that no
matching document is registered, not permission to create an arbitrary one.

## 4. Transactions, coordinates and persistence

| Operation | Transaction and persistence model |
|---|---|
| Ordinary schematic/board items | `with client.commit(..., document):` groups native changes into one Undo step. Exceptions drop the transaction. Read back after successful native cleanup. |
| Sheet creation | Part of schematic Undo, but new child files are created immediately and remain on disk after Undo or Drop. Existing child files are never silently overwritten. |
| Net rename / paper settings | Independent native Undo steps; finish any ordinary API commit first. |
| Library/table writes | Persist independently of schematic/board Undo and refuse pending item commits. Undoing placement does not delete a saved library definition. |
| Project design settings | One validated model update outside board-item Undo, like Board Setup. There is no project-settings Undo entry. Use native save or explicit discard/reload. |
| Document save/revert/copy and GUI actions | Refuse pending API commits, modal dialogs and active interactive tools. Dirty Revert needs explicit discard. |

Ordinary geometry is expressed in **integer nanometers**. Schematic coordinates
round to the native **100 nm** resolution. Project-settings dictionaries use
native units: PCB distances in **mm**, schematic netclass wire/bus widths in
**mil**. Wrapper rotation arguments use degrees; some protobuf fields use spin
or orientation enums instead. Inspect the relevant message before constructing
raw protobuf objects. Library definitions use local canonical coordinates;
placed symbol pins and board pads are returned in absolute document coordinates.

Native wire cleanup can merge or split segments and retire provisional UUIDs.
Re-query the committed model before connecting subsequent operations to those
IDs. CRUD responses can include per-item failures; wrap dependent batches in
`commit()` so the adapter can drop all staged changes on an error instead of
leaving a partially applied batch. Shared schematic screens report affected
documents. PCB item UUIDs survive normal Undo/Redo and reload; the root `BOARD`
object's own runtime UUID can change
on Revert and is not a persistent identity for the file.

Save through `save_document(document)` when persistence is intended. Schematic
save writes the loaded hierarchy; PCB save also persists dirty project design
settings and staged custom rules. The latter does not imply that every editor's
Save writes all project preferences.

`save_copy_of_document` keeps the source path and unsaved state. A schematic copy
contains one screen and retains its child links. Library symbol copies are
self-contained; footprint/drawing-sheet copies use their native formats. For
PCB, `include_project=True` can snapshot live project settings and pending rules
into a fresh copy without saving the source. The destination directory must exist;
overwriting requires `overwrite=True` and cannot target an open source file.

`revert_document(document)` refuses unsaved changes. Use
`discard_unsaved=True` only for an intentional discard. Schematic Revert reloads
the hierarchy and restores the sheet UUID path, zoom and center, with root fallback
if that sheet disappeared. Other editors preserve their viewport. Board Revert
does not discard unsaved project design settings; their reload has its own flag.

`run_action` returns acceptance, not completion. Action IDs are implementation
details and an accepted action may open a dialog or start an interactive tool.
Wait for observable state before sending dependent requests. Do not use a GUI
action as a substitute for a supported structured command.

## 5. Schematic editing, connectivity and reusable symbols

Create symbols from an explicit `lib_id`. KiCad resolves project/global tables,
maintains the schematic's embedded symbol cache and instance annotations, and
returns transformed pin positions. Power symbols and power flags use the same
library-backed placement operation. Re-read after committing:

```python
page = sch.active_document()  # after verifying the intended project and sheet
with sch.commit('Place a timer', page):
    timer = sch.add_symbol('Timer:NE555P', (100000000, 80000000), page)
output = sch.pin_position(timer.reference, '3', page)
print(output)
```

This requires the named library entry to be installed. For a missing part, use
`search_symbol_libraries` first and inspect both matches and library-load errors.
If it is absent, an agent can construct a `LibrarySymbolDefinition` from verified
component documentation, with fields, graphics, units and typed/numbered pins.
Create or select a project symbol library, save through
`save_symbol_definition`, then place its new `lib_id`. Library pin coordinates
use KiCad model axes (positive Y down) and quarter-turn pin angles. The API does
not infer a physically correct pinout from a part name or download a datasheet.

An embedded `lib_symbols` copy belongs to the schematic; it is not automatically
a reusable library entry. To recover one without the original library, read by
placed symbol UUID or call `recover_schematic_symbol`. The exact embedded
definition is used, inheritance is flattened and source connectivity/UUIDs/Undo
are preserved. Headless recovery is available; opening the recovered symbol in
Symbol Editor is optional. Recovery refuses an existing target ID and does not
relink the source instance. A missing embedded definition refuses recovery, even
if an external symbol with the same name exists. Damaged instances do not expose
placeholder pin coordinates as valid endpoints.

Connectivity queries use the live native graph, including unsaved hierarchical
changes. Pass the returned snapshot to subsequent ID-based lookups/highlighting;
session/revision changes invalidate old IDs. Selection or repaint alone does not
advance the connectivity revision. `get_connectivity_issues` reports graph
diagnostics without executing full ERC or adding markers. Run native ERC when
electrical-rule validation is required.

Net rename first returns a preview and blockers, then applies the server-held
plan through the same client. Plans are revision-bound and single-use. It renames
identical literal naming sources across the hierarchy, preserving wire geometry.
Existing target names, unsupported mixed aliases, power naming sources, buses,
variables and changes that would merge/split nets are refused. There is no
`allow_merge` option.

## 6. PCB design and schematic-to-PCB workflow

The intended workflow is:

1. Inspect or construct the live schematic and check connectivity. Assign valid
   footprint IDs to symbols that should appear on the PCB.
2. Search/read the footprint libraries. If needed, construct a verified structured
   definition, create/register a project `.pretty` library and save through KiCad.
3. Preview `update_pcb_from_schematic(..., dry_run=True)` and inspect `can_apply`,
   native messages and source provenance.
4. Apply the update, place components and add copper in a board commit. Read
   native pad endpoints/nets rather than estimating connections from graphics.
5. Inspect ratsnest/connectivity, finish the commit and save through KiCad. Run
   native ERC/DRC, including schematic parity, on the intended saved test files.

For example, after explicitly selecting `sheet_doc` and `board_doc`:

```python
preview = pcb.update_pcb_from_schematic(
    board_doc, schematic_document=sheet_doc, schematic_client=sch, dry_run=True)
if not preview.can_apply:
    raise RuntimeError('Inspect the native update report before applying')
with pcb.commit('Update board from schematic', board_doc):
    report = pcb.update_pcb_from_schematic(
        board_doc, schematic_document=sheet_doc, schematic_client=sch)
pcb.save_document(board_doc)
```

The live path exports native netlist data from the open schematic without saving
or generating a temporary schematic file. To deliberately use saved state, supply
`schematic_file` instead; the file must belong to the selected board project.
There is no automatic switch from a failed live export to a disk file. A live
snapshot identifies a connectivity revision; it is not a lock against later user
edits. Re-export when a fresh preview/application is required.

Footprint placement/replacement uses native library loading and exchange logic.
Pad endpoints include actual side/rotation and copper layers. Connected pads
cannot silently disappear during replacement, and locked footprints need an
explicit override. Unknown net names refuse by default; only an intentional
`create_net_if_missing=True` request creates a net, inside the same commit.
Prefer schematic update for pad-net changes. `allow_net_mismatch=True` is an
explicit manual override, not an automatic repair policy.

Footprint library recovery reads the live instance even if its source library is
missing or different. The reusable model is normalized to F.Cu, zero rotation and
local origin, with instance nets/links removed and the source left unchanged.
Integer coordinate round trips can differ by one nanometer. Definitions support
pads, supported graphics/text/fields, reference points, embedded files and 3D
models. Native persistence and cache refresh make a saved ID immediately usable.
For nested protobuf `Any` children, use normal `Pack()` with canonical
`type.googleapis.com` type URLs; custom prefixes and unknown fields refuse
validation instead of silently dropping data.

## 7. Live project classes, constraints and custom rules

`get_project_settings` returns structured native settings, an opaque `revision`
and a project-settings `modified` flag independent of geometry. Use the returned
schema rather than guessing JSON keys. A successful context-manager exit submits
one atomic update; an exception in its body submits nothing:

```python
board_doc = pcb.active_document()  # verify the intended project first
with pcb.edit_project_settings(board_doc) as settings:
    settings['board_design_settings']['rules']['min_track_width'] = 0.127
    settings['board_design_settings']['rule_severities']['track_width'] = 'error'
pcb.save_document(board_doc)
```

Finish item commits before any project-settings request. These commands also
refuse modal dialogs, active interactive tools and a running DRC operation.

Supported edits include netclass parameters/priority/vias/differential pairs,
patterns and explicit assignments, minimum constraints, violation severities,
predefined track/via/differential-pair sizes, and custom-rule text. Other returned
board-design sections are read-only for this operation. Advanced updates require
the current `expected_revision` and explicit, nonoverlapping field paths.
Unknown fields, invalid ranges/references, missing Default class, invalid native
rule syntax/conditions and stale revisions refuse before applying changes.

`get_nets_by_netclass` uses native effective membership, including composites and
existing implicit classes. Patterns such as `Net-(U1-*` use KiCad's matcher, not
Python regex syntax. Existing implicit label classes are not silently materialized
as explicit definitions.

If settings were changed externally, explicitly call `reload_project_settings`.
It reloads `net_settings`, `board.design_settings` and `.kicad_dru`, reassigns
classes, recompiles rules and refreshes the display while invalidating stale DRC
results. It preserves geometry, its dirty flag and Undo stack. Unsaved in-memory
settings refuse by default; only an intentional `discard_unsaved=True` discards
them. It does not reload every preference, library definition or editor's state.

Pending custom rules participate in the native DRC engine before persistence.
SaveDocument saves them with dirty native project settings; SaveCopy does not
consume them. Native project-save failure handling retains pending changes and
restores the prior rules file when the project write fails. Individual file
replacement is atomic; this is not a cross-file filesystem transaction guarantee.

## 8. Boundaries an agent must understand

- Generic schematic item Update/Delete does not cover sheets or sheet pins.
  `create_sheet` constructs hierarchy; port renaming can use the supported net
  rename policy. Creating a sheet/port does not create matching child labels or
  parent wires automatically.
- Symbol movement/rotation does not drag wires. Pin readback is an endpoint aid,
  not an electrical connection by itself.
- Schematic polygons support one point outline without holes. Unsupported shapes
  or library content refuse rather than silently losing data.
- Footprint library save/recovery refuses zones, groups, tables, barcodes and
  images. Grouped sources also refuse definition reads because the native
  serializer does not preserve group membership. Textual
  `ParseAndCreateItemsFromString` is not implemented by this extension.
- Library lookup errors are distinct from no match. Library creation/registration
  changes project tables. Symbol definition save requires a selected writable
  KiCad-format library; footprint definition writes are restricted to native
  project libraries. Existing destinations require explicit overwrite where the
  operation supports it; symbol recovery always refuses an existing target.
  Dirty destination editor buffers are protected, but protection covers editors
  in the same process, not every other KiCad process.
- Native schematic-to-PCB update refuses assembly-variant projects because that
  catalogue is outside BOARD_COMMIT Undo. It does not start asynchronous zone
  refill. Use the existing refill operation separately after the transaction.
- Tracks/vias and endpoint queries are building blocks, not a production
  autorouter. The test fixture router is not a general routing service.
- Connectivity diagnostics are not full ERC; a clear ratsnest is not a substitute
  for DRC or schematic parity. The adapter does not add a universal headless
  validation service for every editor.
- A busy/modal/tool refusal means finish or resolve the existing operation and
  re-read state. Do not work around it by replacing a live project's files or
  force-closing a user's editor.

## 9. Additional fork changes: images and macOS

Reference images support independent **X/Y scale** and a **nine-point anchor**:
corners, edge centers and center. Schematic and PCB image properties, native
serialization, rendering and plotting were updated; drawing-sheet bitmap scaling
also has per-axis persistence. Uniform scaling retains the legacy `scale` form;
nonuniform scaling uses `scale_x`/`scale_y`, with an anchor token where applicable.
Do not assume older builds without these extensions preserve that file data.
These are native editor/image changes, not new `kipy_schematic` image commands.

The fork also contains targeted compatibility changes for macOS Monterey and
Apple Clang 14's available standard library, while retaining C++20 as the build
requirement. This is not a guarantee for arbitrary older compilers or every
platform/dependency combination.

macOS packaging aligns the embedded Python framework/site-packages layout with
the interpreter used for the build. `KICAD_WXPYTHON_SOURCE_DIR` selects a wxPython
source tree built against KiCad's wxWidgets. Do not bundle a stock wxPython wheel's
second wxWidgets runtime alongside KiCad's own runtime. A validated combination
for this fork is wxPython 4.2.3 with wxWidgets 3.2.8; these are tested versions,
not a new requirement to downgrade unrelated platforms.

When updating an existing macOS bundle, preserve framework/dylib symlinks and
update the actual versioned libraries, not only their unversioned aliases.
Preserve existing symbol/footprint/3D/help resources, back up changed binaries and
signatures, and verify the final bundle signature. Do not replace a resource-rich
installation with a smaller staged bundle that would delete those resources.

## 10. Tests and reproducibility

The transport unit tests can run without an open KiCad after building the adapter:

```sh
python -m unittest discover -s api/python/tests -p 'test_*client.py'
```

For adapter 0.16.0, that selection contains **29 tests**. Broad discovery of every
`test_*.py` file is not the supported runner: some older live scripts parse CLI
arguments or require running editors and prepared fixtures.

Native QA includes `SchematicSheetApi`, `SchematicConnectivityApi`,
`SchematicNetRenameApi`, `SchematicItemsApi`, `PcbApiNetSafety`, `BoardItemIndex`,
`FootprintLibraryApi`, `ProjectSettingsApi` and `ProjectSettingsState`.
`BitmapBase/PerAxisScale` covers the image scaling foundation. Build the
appropriate QA targets with their platform dependencies and use the suites in
the source tree; a selected regression run is not the entire KiCad QA suite.

Live mutation tests must target **disposable copies** marked with
`.ipc-test-project`, explicit sockets and verified document/process ownership.
The marker alone does not prepare a valid fixture. Inspect each runner's inputs
and assertions before use; some acceptance fixtures, definitions and reference
snapshots are prepared separately and are not supplied as a turnkey environment
in a fresh clone. Never weaken guards to run acceptance against working designs.

| Runner | Scope / fixture requirements |
|---|---|
| [live_document_commands.py](qa/tests/api/live_document_commands.py) | Five-editor document operations; explicit type/source/socket and an unused fixture containing `STAGE1_INITIAL`. |
| [test_live_project_settings.py](api/python/tests/test_live_project_settings.py) | Settings/classes/rules, dirty protection, SaveCopy, Undo/Redo/Revert; explicit project/socket/artifacts and the prepared settings/net fixture asserted by the script. |
| [live_pcb_items.py](api/python/tests/live_pcb_items.py) | Board items, net safety, placement and endpoint queries on its prepared fixture. |
| [live_schematic_pcb_integration.py](api/python/tests/live_schematic_pcb_integration.py) | End-to-end timer schematic/PCB, live versus saved source and grouped Undo; prepared circuit/reference data and both editor sockets. |
| [live_footprint_libraries.py](api/python/tests/live_footprint_libraries.py) | Library creation/registration, save, recovery and refusals; begins with its prepared board and no project footprint table. |

The functional checkpoint was exercised with native unit tests, transport tests
and isolated real-editor acceptance: all five document editors, live schematic
editing/libraries, schematic-to-PCB update and routed timer validation, footprint
recovery, and settings reload/native DRC. The latest settings change passed
**20 selected C++ cases / 138 assertions**, **29 Python tests** and **27 live
scenarios**. Those counts describe a recorded selection, not a claim that a fresh
clone has already passed every platform or the full monolithic QA suite.

A useful end-to-end acceptance target is: create a small timer circuit entirely
through IPC in one schematic commit; verify its expected nets and full ERC;
preview/apply native PCB update; place and route through IPC; check native DRC,
unconnected items and schematic parity; verify Undo/Redo and explicit Revert;
save and reopen. Use deliberately failing custom rules separately to prove the
live rule engine is using edited settings; such expected violations are not a
DRC-clean production result.

## 11. Implementation map and extension workflow

| Responsibility | Source |
|---|---|
| Python methods, descriptor generation and detailed usage | [api/python](api/python), [build_client.py](api/python/build_client.py), [API guide](api/python/README.md) |
| Shared document guards and dispatch | [API_HANDLER_EDITOR](common/api/api_handler_editor.cpp), [editor protocol](api/proto/common/commands/editor_commands.proto) |
| Schematic item/connectivity/library/rename/export handlers and codecs | [eeschema/api](eeschema/api), [schematic protocol](api/proto/schematic) |
| PCB items, placement and native update | [PCB handler](pcbnew/api/api_handler_pcb.cpp), [integration handler](pcbnew/api/api_handler_pcb_integration.cpp), [integration protocol](api/proto/board/integration_commands.proto) |
| Footprint libraries and normalized definitions | [library handler](pcbnew/api/api_handler_pcb_library.cpp), [model validation](pcbnew/api/api_pcb_library_utils.cpp), [library protocol](api/proto/board/library_commands.proto) |
| Project design settings | [settings handler](pcbnew/api/api_handler_pcb_settings.cpp), [validation](pcbnew/api/api_pcb_settings_utils.cpp), [settings protocol](api/proto/board/project_settings.proto) |
| Native settings/save points and pending rules | [JSON settings](common/settings/json_settings.cpp), [project save](common/project/project_file.cpp), [DRC engine](pcbnew/drc/drc_engine.cpp) |
| Drawing sheet document handler | [drawing sheet handler](pagelayout_editor/api_handler_drawing_sheet.cpp) |
| Image scale and anchor model | [reference image](include/reference_image.h), [bitmap](common/bitmap_base.cpp), [anchors](include/anchor_point.h) |
| Native and Python regression tests | [qa/tests](qa/tests), [Python tests](api/python/tests) |

For a new protocol feature, update its **protobuf + C++ handler/native codec +
Python method + meaningful tests + documentation** together. Regenerate client
descriptors using `build_client.py`, bump the adapter version for protocol/client
changes, and verify a matching native build. Check existing handlers instead of
inventing field names, action IDs or unsupported operations. Keep changes in
focused commits and distinguish implementation status from platform-specific
build, installation or acceptance status.

Useful history entry points are `b2e7e6f955` (compiler compatibility),
`6aaf3684ce` (image scale/anchor), `e65e177ed4` (macOS packaging), `bb5f0baaf9`
(schematic paper and the earlier connectivity extensions), `9404b4399e`
(five-editor document operations), `f876f67eb5` (live schematic editing and symbol
libraries), `7943468842` (native PCB workflow), `59480deb8a` (footprint libraries)
and `922c790d75` (project settings). Inspect their ancestry/diffs for the full
history; this guide is a capability reference, not a replacement for source review.
