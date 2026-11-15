# Schematic IPC client for this KiCad fork

This client supports live schematic item editing, connectivity, document commands
and symbol libraries in this source tree. It reuses the
`kicad-python` transport, but loads its protocol into an isolated descriptor
pool. It does not depend on the unfinished `kipy.schematic` wrapper for KiCad 11
or its missing `PageSettings`/`TitleBlockInfo` imports. The regular kipy PCB
client remains available in the same Python process.

Build the descriptors from the same checkout used to build KiCad (`protoc` must
be available), then install the resulting package in your Python environment:

```sh
python api/python/build_client.py --output /absolute/build/path/schematic-client
python -m pip install /absolute/build/path/schematic-client
```

```python
from kipy_schematic import SchematicClient

client = SchematicClient()  # honors KICAD_API_SOCKET and KICAD_API_TOKEN
page = client.active_document()
sheets = client.get_sheets(page)  # live state, including unsaved edits

with client.commit("Add power sheet", document=page):
    power = client.create_sheet(
        "power", "power.kicad_sch", document=page,
        position_nm=(25000000, 25000000), size_nm=(20000000, 15000000))

# Attach an existing file without reloading a screen already open in KiCad:
# client.create_sheet("existing", "existing.kicad_sch", document=page,
#                     link_existing=True)
```

The default socket is `ipc:///tmp/kicad/api.sock`. Choose an explicit document
from `get_open_documents()` when several schematic projects are open. The
response lists the active page first, followed by the other loaded pages.
All coordinates are integer nanometers; KiCad rounds them to its 100 nm
schematic resolution. `Sheet` and `SheetPin` are exported protobuf classes.
Sheet pins have absolute parent-page coordinates and must lie on the declared
edge. Creating ports does not manufacture wires or matching labels inside an
existing child schematic.

New child files are created immediately without overwriting existing files.
The parent remains modified until saved normally in the GUI. Undo/Redo and a
failed transaction retain child files on disk, including any later edits to
them. Existing child files are attached, not overwritten. Rebuild the client
when changing this fork's protobuf schema. The regular KiCad 10 API does not
provide these additional commands.

Run the protocol/client tests against the generated package:

```sh
PYTHONPATH=/absolute/build/path/schematic-client python api/python/tests/test_client.py
```

## Live connectivity (client 0.2)

All queries take an explicit schematic document (project path and sheet UUID
path). `active_document()` is a convenience only when one project is open.
The selected page disambiguates item UUIDs and reused sheet instances; returned
net members cover the entire loaded hierarchy, including unsaved changes.

```python
snapshot = client.get_pin_net('U8', 39, document=page)
for net in snapshot.nets:
    print(net.id, net.name, list(net.aliases))
    for member in net.members:
        if member.kind == 'pin':
            print(member.reference, member.unit, member.pin_number,
                  member.pin_name, member.electrical_type, member.value,
                  member.document.sheet_path.path_human_readable)

members = client.get_net_members(snapshot.nets[0].id, snapshot=snapshot)
client.highlight_net(snapshot.nets[0].id, snapshot=snapshot)
client.highlight_net(clear=True, document=page)

full = client.get_schematic_netlist(page)
selected = client.get_item_net(use_selection=True, document=page)
# client.get_item_net(['wire-uuid'], document=page)
pin_table = client.get_symbol_connections('U8', document=page)
diagnostics = client.get_connectivity_issues(page)

# Structured full netlist export; no temporary GUI export is necessary:
from google.protobuf.json_format import MessageToJson
from pathlib import Path
Path('netlist.json').write_text(MessageToJson(full))
```

A net ID is valid only for its `session_id` and `revision`. The revision is a
monotonic **connectivity snapshot** revision, not an editor undo counter: it
changes when the serialized graph, member geometry or metadata changes. It
does not change for selection, highlight, repaint or other unrelated edits.
Reopening an editor creates a different session. Pass `snapshot=` to lookups
and highlighting to reject stale results. An old ID is never reused at a new
revision. A later query after an edit recalculates connectivity without cleanup
or file writes. Each result is internally consistent on the GUI thread.

`GetItemNet` returns all nets for a symbol or multi-selection. Bus results
include the bus plus its resolved leaf nets (`is_bus`, `bus_member_ids`), using
KiCad's own hierarchy and bus alias resolution. Pin references omit the unit
suffix; `unit` distinguishes the placed units, and NC state is explicit.
`GetPinNet` prefers the selected sheet; ambiguous cross-sheet matches fail
instead of silently choosing a connection. Exact resolved names are accepted
by `get_net_members(name=...)`; aliases are metadata, not lookup shortcuts.

Diagnostics report unconnected pins, explicit NC pins, dangling wires/labels,
local naming-driver conflicts, and informational aliases. They do not run full
ERC, add markers, infer electrical types or repair the schematic. The canonical
pin type returned is the value actually stored in the model. Highlighting uses
the normal GUI net navigator; the returned members identify other sheets.
Generic symbol/wire/label item creation and editing remain outside this API.

## Rename a net (client 0.3)

Renaming has two explicit stages. Both use the live, unsaved hierarchy:

```python
snapshot = client.get_pin_net('U8', 39, document=page)
plan = client.plan_net_rename(snapshot.nets[0].id, 'PHI2_RENAMED', snapshot=snapshot)
print('Can apply:', plan.can_apply)
print('Refusals:', list(plan.blockers))
print('Existing nets:', [net.name for net in plan.collisions])
for change in plan.changes:
    print(change.item.document.sheet_path.path_human_readable,
          change.item.id.value, change.before, '->', change.after)

# After inspecting the plan, explicitly apply it:
# result = client.rename_net(plan)
# result.snapshot contains the new connectivity revision.
```

An existing target name anywhere in the loaded hierarchy causes refusal, even
when its current sheet scope would keep it separate. There is **no merge
option**. `PHI2_SALLY -> PHI2` is refused if PHI2 exists. Matching is case
sensitive, as in KiCad. Collision previews return `can_apply=False`, `collisions`
and `blockers`; they receive no executable plan ID.

The supported alias policy is explicit: rename **all identical literal labels
and hierarchical ports belonging to the source net**, throughout the selected
project hierarchy. The preview lists every affected instance, but a shared
physical object is modified only once. Heterogeneous literal aliases, variables,
power-symbol naming sources, generated names without labels, buses and bus
members are refused. New names must be valid scalar label text without spaces,
hierarchical paths or variables. The planner also compares the complete native
graph after a reversible text preview and refuses any merge, split or change
to another net, including side effects from shared sheet instances. Text and
graph caches are restored before a preview response is returned. No dirty flag,
Undo entry or schematic file is changed by preview/refusal.

Only the same `SchematicClient` can apply its latest issued plan. A new preview
supersedes its previous plan; plans are also invalidated by connectivity revision
changes, editor sessions, successful use, or bounded-cache eviction. On apply,
the server checks the current snapshot and recomputes the entire plan. Editing
the protobuf plan in Python cannot change the server's stored operation.

A successful rename creates **one independent Undo/Redo step**, updates all
relevant screens, recalculates connections, and preserves wire geometry. It
leaves the schematic unsaved. Finish any pending API transaction first;
`rename_net` cannot be nested inside `client.commit()`. The response contains
the applied plan and a fresh full connectivity snapshot. Read the net again
after editing or undoing; previous IDs/revisions are stale.

The real-editor acceptance test is `api/python/tests/test_live_net_rename.py`.
It requires an explicitly marked disposable copy of Codex800XL and the PID of
its isolated test editor, and exercises GUI Undo/Redo only on that process.

## Schematic paper format (client 0.4)

```python
page = client.active_document()
settings = client.get_page_settings(page)
print(settings.paper_format, settings.orientation, settings.width_nm, settings.height_nm)

# Supply the exact documents selected from get_open_documents().
result = client.set_page_settings('A3', 'landscape', documents=[page])
print(result.changed_screens, list(result.pages))
```

`GetPageSettings` takes an explicit project path and sheet UUID path. Orientation
is `PO_LANDSCAPE` (1) or `PO_PORTRAIT` (2) in the response. Dimensions are actual
oriented dimensions in nanometers; standard metric sizes follow KiCad's native
rounding to whole mils. Existing custom paper can be read. Writing supports
standard schematic paper (`A5`–`A0`, `A`–`E`, `USLetter`, `USLegal`, `USLedger`)
and requires an explicit portrait or landscape orientation.

`SetPageSettings` validates the entire nonempty target list before changing any
sheet. Duplicate documents are coalesced. Paper settings belong to the file:
when changing a shared file, select every loaded instance. A mixed project,
missing sheet, read-only target being changed or omitted shared instance is
rejected. An unchanged request creates no Undo entry. The response lists the
resulting settings in request order and counts distinct files changed.

A successful batch creates one independent Undo/Redo step and leaves the changed
sheets unsaved. Finish other API transactions first. Paper changes preserve the
sheet contents and title block; the current drawing frame updates without
switching sheets or reloading. Save normally in the editor to persist the result.

The live test `api/python/tests/test_live_page_settings.py` takes a marked,
disposable HyperSpeed copy and an isolated configuration with IPC enabled. It
starts its own editor and verifies A4 to A3, orientation, grouped Undo/Redo,
shared files and save/reopen. `--capture` records the test editor's page frame
before and after the batch on macOS.


## Document commands in every editor (client 0.5)

`revert_document`, `save_document`, `save_copy_of_document` and `run_action`
accept an explicit `Document` from `get_open_documents(document_type)`:
schematic=1, symbol=2, PCB=3, footprint=4, drawing sheet=5. This same adapter
also serves PCB documents; no separate PCB wrapper is required. Footprint and
drawing sheet editors now register handlers and enumerate their current document.
Library documents use `lib_id` and project context, not a tree selection.
Drawing sheets use `board_filename` for their filename; an untitled layout
uses an empty filename and can be exported with SaveCopy.

```python
board = client.get_open_documents(3)[0]
client.save_document(board)
client.save_copy_of_document(board, '/absolute/path/copy.kicad_pcb', overwrite=True)
client.revert_document(board)  # refuses unsaved changes
# Only after choosing to discard those changes:
client.revert_document(board, discard_unsaved=True)
assert client.run_action(board, 'common.Interactive.undo') == 1
assert client.run_action(board, 'does.not.exist') == 2
```

All four commands refuse an open API transaction (including another client's),
a modal dialog or an active interactive tool. Foreign document types are routed
to the appropriate handler. Revert preserves zoom/center; schematics additionally
restore the active sheet UUID path, falling back to root if it disappeared.
Revert reloads the entire schematic hierarchy; symbol/footprint editors reload
one library item from disk. Other unsaved symbol buffers remain intact.
`run_action(doc, 'common.Control.revert')` follows the same safe policy and
cannot silently discard changes. Action names are unstable implementation details.
Other actions are queued with their declared default arguments: `RAS_OK` means
accepted, not that a dialog or interactive tool has finished. The IPC socket
remains responsive while a queued action displays a modal dialog.
Wait for the expected live state before issuing dependent edits or saves after
an action; an immediate read can precede the queued action.

Save writes the complete schematic hierarchy once per screen, including
non-active sheets. It does not implicitly save project settings. Library Save
writes only the current item through global/project library tables and refuses
read-only libraries. Schematic-linked symbols and board-linked footprints must
be saved through their parent editor; explicit SaveCopy remains available.

SaveCopy preserves the active path and unsaved flags. Schematic copies contain
one selected sheet, keeping child file links unchanged. Symbol copies are
self-contained single-symbol `.kicad_sym` libraries (inherited symbols are
flattened). Footprint and drawing sheet copies are `.kicad_mod`/`.kicad_wks`
files. `include_project=True` is supported only for PCB; other types reject it.
The destination directory must exist, the extension must match, and overwriting
requires `overwrite=True`. SaveCopy refuses to overwrite an open source file.
Regenerate descriptors with `build_client.py` when updating from client 0.4.


The repeatable live acceptance runner is `qa/tests/api/live_document_commands.py`.
It requires a `.ipc-test-project` marker, an already-open test document and a
source fixture containing `STAGE1_INITIAL`. It refuses reference project names,
changes the test file on disk and exercises reload, dirty protection, copy/save,
Undo/Redo, malformed requests, transaction exclusion and read-only libraries.
Never pass a working user document to mutation tests. Modal and viewport checks
must additionally be performed against the private test app.


## Live schematic items (client 0.6, stage 2 in progress)

Wire/bus/graphical `Line` CRUD is available through `get_items`, `create_items`,
`update_items(fields=[...])`, `delete_items` and `add_wire(start_nm, end_nm)`.
Object type 23 selects lines. Layer values are WIRE=1, BUS=2, NOTES=3; coordinates
are integer nm and round to the schematic's 100 nm resolution. Zero-length,
out-of-range and invalid UUID data are rejected before applying that item.

```python
page = client.active_document()
with client.commit('Connect two points', document=page):
    line = client.add_wire((10000000, 10000000), (20000000, 10000000), page)
    line.end.y_nm = 15000000
    client.update_items([line], page, fields=['end'])
```

Items are attached immediately, so reads/connectivity in the same open commit
see current edits. Drop restores the old live model; EndCommit performs normal
native connectivity cleanup in the same Undo step. Cleanup may merge/split
segments and retire their provisional IDs; read the model again after commit.
Undo/Redo preserves the final committed UUIDs. Updates without a mask replace
all supported protocol properties; fields absent from the protocol are preserved.
Per-item failures carry explicit status. Wrap dependent batches in `commit()`:
the adapter raises on failure, causing Drop and rollback of the transaction.
Read-only queries remain available during a transaction; other clients cannot
mutate it. `last_affected_documents` reports all instances of a shared screen.
Symbol editing and library operations are still pending until later stage-2 commits.


### Labels and text (client 0.7)

`add_label(text, position_nm, document, kind='local', rotation_degrees=0,
shape=0, properties=None)` supports local/global/hierarchical/directive labels.
Object types are 30–33. All label protobufs now carry spin, shape and properties;
`text.text.text` is the literal string in these pre-existing nested messages.
Spin values 0–3 mean native left/up/right/bottom. Shapes are 0 input, 1 output,
2 bidirectional, 3 tristate, 4 passive; directive shapes instead mean
0 dot, 1 circle, 2 diamond, 3 rectangle. Properties carry native fields such as
`Net Class`. The same generic read/update/delete and transaction rules apply.
`add_text` creates ordinary schematic notes (object type 27); font/position data
also uses nm. Text and labels preserve core formatting through native codecs.

Version 0.8 adds `Junction`, `NoConnect` and `BusEntry` to generic CRUD, with
`add_junction(position, diameter_nm=0)`, `add_no_connect(position)` and
`add_bus_entry(position, size, bus_to_bus=False)`. Dimensions use nm; zero junction
diameter selects the project default. Bus entries need a nonzero diagonal size.
All use the same explicit document, transaction, field-mask and affected-instance
contract as wires. Native commit cleanup may remove redundant junctions.

Version 0.9 adds `Symbol` and `add_symbol('Timer:NE555P', (x_nm, y_nm),
reference='U1', fields={'Value': 'NE555'})`. Empty references are assigned the next
free designator; explicit conflicts are refused, while different units of the same
library part may share a reference. Every live shared-sheet instance gets its own
annotation (or explicit `instances` entries). `lib_id` loads from native project /
global tables; native screen insertion maintains embedded definitions and pin UUIDs.
Power symbols and PWR_FLAG use this same operation. Fields include absolute text
position, style and visibility; `unit`, `body_style`, quarter-turn `rotation_degrees`,
`mirror_x/y`, optional `dnp`, `in_bom` and `on_board` are editable. Readback includes
absolute pin positions after transforms and instance annotations. Pins are read-only;
field masks targeting pins are refused. Moving/rotating a symbol does not drag wires.
`pin_position('U1', '3')` resolves an absolute endpoint; `add_wire(('U1', '3'), (x, y))`
also accepts reference/number pairs (missing or ambiguous endpoints raise errors).
Library writes are a separate persistence operation from schematic Undo transactions.

Version 0.10 adds structured symbol libraries and embedded recovery:

```python
found = sch.search_symbol_libraries('SII8784', doc)
for error in found.errors:  # distinguish unreadable libraries from no match
    print(error.nickname, error.message)
definition = sch.get_symbol_definition('Timer:NE555P', doc).definition
sch.create_symbol_library('ProjectSymbols', 'ProjectSymbols.kicad_sym', doc)
definition.lib_id.library_nickname = 'ProjectSymbols'
definition.lib_id.entry_name = 'MyTimer'
sch.save_symbol_definition(definition, doc)
with sch.commit(document=doc):
    placed = sch.add_symbol('ProjectSymbols:MyTimer', (100000000, 80000000), doc)
embedded = sch.get_symbol_definition(document=doc, symbol_id=placed.id.value)
recovered = sch.recover_schematic_symbol(placed.id.value, 'ProjectSymbols:Recovered', doc,
                                        open_in_editor=False)
editor_doc = sch.open_library_symbol('ProjectSymbols:Recovered', doc)
```

`LibrarySymbolDefinition`, `LibraryPin`, `SymbolField` and `LibraryGraphic` are
public structured messages. Definitions use canonical library coordinates in nm,
include all units/body styles, fields, pin electrical types, sizes, shapes,
alternates, shapes/text/text boxes, native schematic fill/color (including body
background), power/visibility/exclusion metadata and footprint filters.
Library coordinates use KiCad model axes (positive Y downward); library pin angles are counterclockwise 0/90/180/270; unit/body zero means common.
Overlapping duplicate pin numbers are refused. Unsupported native graphic types
produce a clear error rather than an incomplete export. Library inheritance is
flattened for independent reusable copies.

Search resolves native global/project tables (project precedence), names and
keywords, with full IDs and scope. It returns up to 100 results by default (1000
maximum); use offset/total_matches for pagination. Reading by symbol UUID uses only
its embedded definition, even when the external library is missing or different.
A missing/dummy embedded definition is an error; no guessed replacement is used.

Create/register affects only the project table; link_existing must explicitly
select a preexisting .kicad_sym. Saving requires an explicitly selected, writable
KiCad-format library; existing IDs require overwrite=True. Definition validation
happens before persistence, then native KiCad serialization writes an atomic file
replacement and refreshes the native library cache. No schematic Revert/restart
is needed. Library changes persist independently and are refused inside pending
schematic commits. Schematic SaveDocument remains explicit.

Recovery never overwrites, relinks, copies instance rotation/reference into the
library, or modifies source UUIDs/fields/connectivity/Undo. Headless is the default;
open_in_editor=True opens the new library-backed copy, independently of the source
schematic. The target symbol's dirty editor buffer, active tools and linked dirty
symbol edits are protected with refusal. Other symbol buffers, including dirty siblings in the same library, remain intact.
