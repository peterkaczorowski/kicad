"""Acceptance checks on an isolated COPY of Codex800XL, never the user's project.

Open the copied root in the test editor. A .ipc-test-project marker and explicit
--project path are mandatory. An intentionally unsaved sheet/port is added at a
wire endpoint to verify live graph revision changes and stale-ID rejection.
"""
import argparse
import hashlib
from pathlib import Path
from kipy.errors import ApiError
from kipy_schematic import SchematicClient, SheetPin

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--project', type=Path, required=True)
args = parser.parse_args()
project = args.project.resolve()
assert (project / '.ipc-test-project').is_file(), 'Only a disposable test copy is allowed'
client = SchematicClient(timeout_ms=60000)
doc = client.active_document()
assert Path(doc.project.path).resolve() == project, 'Wrong editor instance'
files_before = {p: hashlib.sha256(p.read_bytes()).digest() for p in project.glob('*.kicad_sch')}

snapshot = client.get_pin_net('U8', 39, document=doc)
assert len(snapshot.nets) == 1
net = snapshot.nets[0]
assert net.name.endswith('PHI2_SALLY'), net.name
pins = {(p.reference, p.pin_number): p for p in net.members if p.kind == 'pin'}
assert set(pins) == {('U8', '39'), ('R20', '2'), ('U18', '12'), ('U19', '1')}, set(pins)
assert pins['U18', '12'].unit == 4
assert pins['U19', '1'].unit == 1
assert pins['U18', '12'].document.sheet_path.path_human_readable == '/cpu_gpu/'
assert pins['U19', '1'].document.sheet_path.path_human_readable == '/control/'
assert all(p.electrical_type and p.symbol_id.value for p in pins.values())
print('PASS: U8.39 ->', net.name, sorted(pins), flush=True)

members = client.get_net_members(net.id, snapshot=snapshot)
assert members == snapshot
by_name = client.get_net_members(name=net.name, document=doc)
assert by_name.nets == snapshot.nets
wire = next(p for p in net.members if p.kind == 'wire')
by_wire = client.get_item_net(wire.id.value, document=wire.document)
assert [n.id for n in by_wire.nets] == [net.id]
by_pin_uuid = client.get_pin_net(pin_id=pins['U8', '39'].id.value,
                                document=pins['U8', '39'].document)
assert by_pin_uuid.nets == snapshot.nets
symbol_nets = client.get_item_net(pins['U8', '39'].symbol_id.value,
                                  document=pins['U8', '39'].document)
assert len(symbol_nets.nets) > 1, 'A symbol must return all its nets'
table = client.get_symbol_connections('U18', document=doc)
assert len({p.pin.unit for p in table.pins}) > 1, 'All placed units are required'
assert any(p.pin.pin_number == '12' and net.id in p.net_ids for p in table.pins)
full = client.get_schematic_netlist(doc)
assert full.revision == snapshot.revision
assert len(full.nets) > 100
assert any(n.is_bus and n.bus_member_ids for n in full.nets), 'Bus membership missing'
all_ids = {n.id for n in full.nets}
assert all(i in all_ids for n in full.nets for i in n.bus_member_ids)
assert client.get_item_net(use_selection=True, document=doc).nets == []
client.highlight_net(net.id, snapshot=snapshot)
client.highlight_net(clear=True, document=doc)
issues = client.get_connectivity_issues(doc)
assert issues.snapshot.revision == snapshot.revision
assert any(i.code == 'PIN_INTENTIONALLY_UNCONNECTED' for i in issues.issues)
assert all(i.code and i.description and i.severity for i in issues.issues)
assert all(hashlib.sha256(p.read_bytes()).digest() == digest for p, digest in files_before.items())
print('PASS: member/item/symbol queries, full netlist, buses, highlight/clear, diagnostics, no file writes', flush=True)

# A new port touches an existing wire in the live model, without saving its parent.
pin = SheetPin(name='IPC_LIVE_ALIAS', side=1, shape=3)
pin.position.CopyFrom(wire.position)
sheet = client.create_sheet('ipc_live_connectivity', 'ipc_live_connectivity.kicad_sch',
    document=wire.document, position_nm=(wire.position.x_nm, wire.position.y_nm - 5000000),
    size_nm=(20000000, 10000000), pins=[pin])
after = client.get_pin_net('U8', 39, document=doc)
assert after.session_id == snapshot.session_id and after.revision > snapshot.revision
assert any(p.kind == 'sheet_pin' and p.text == 'IPC_LIVE_ALIAS' for p in after.nets[0].members)
assert after.nets[0].id != net.id
for callback in (lambda: client.get_net_members(net.id, snapshot=snapshot),
                 lambda: client.highlight_net(net.id, snapshot=snapshot)):
    try:
        callback()
    except ApiError as error:
        assert 'Stale' in str(error), str(error)
    else:
        raise AssertionError('Old connectivity revision was accepted')
assert all(hashlib.sha256(p.read_bytes()).digest() == digest for p, digest in files_before.items())
print('PASS: unsaved graph update, revision increment, stale IDs rejected; parent files unchanged', flush=True)
