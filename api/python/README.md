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
