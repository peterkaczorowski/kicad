# Schematic IPC client for this KiCad fork

This client supports the sheet IPC extension in this source tree. It reuses the
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
