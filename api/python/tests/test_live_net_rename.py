"""Rename acceptance test on a disposable Codex800XL COPY with an isolated editor.

Requires .ipc-test-project, an explicit --project and the test editor PID. Uses
GUI Undo/Redo on that PID only. Never save or run this against the real project.
"""
import argparse
import hashlib
from pathlib import Path
import subprocess
import time
from kipy.errors import ApiError
from kipy_schematic import SchematicClient, SheetPin, _message

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--project', type=Path, required=True)
parser.add_argument('--editor-pid', type=int, required=True)
args = parser.parse_args()
project = args.project.resolve()
assert (project / '.ipc-test-project').is_file()
command = subprocess.check_output(['ps', '-p', str(args.editor_pid), '-o', 'args='], text=True)
assert str(project / 'Codex800XL.kicad_sch') in command and 'eeschema' in command
client = SchematicClient(timeout_ms=60000)
doc = client.active_document()
assert Path(doc.project.path).resolve() == project
files = {p: hashlib.sha256(p.read_bytes()).digest() for p in project.glob('*.kicad_sch')}

def rejected(callback, text):
    try:
        callback()
    except ApiError as error:
        assert text.lower() in str(error).lower(), str(error)
    else:
        raise AssertionError('Request should have been refused')

def pin_net():
    return client.get_pin_net('U8', 39, document=doc)

def unchanged_files():
    assert all(hashlib.sha256(p.read_bytes()).digest() == digest for p, digest in files.items())

def wire_geometry(snapshot):
    return sorted((m.document.sheet_path.SerializeToString(), m.id.value,
                   m.position.x_nm, m.position.y_nm, m.end_position.x_nm, m.end_position.y_nm)
                  for n in snapshot.nets for m in n.members if m.kind in ('wire', 'bus'))

def gui_undo_redo(name, redo=False):
    action = 'Redo' if redo else 'Undo'
    subprocess.run(['osascript', '-e', f'tell application "System Events" to tell (first process whose unix id is {args.editor_pid})',
                    '-e', 'set frontmost to true', '-e', f'click (first menu item of menu "Edit" of menu bar 1 whose name starts with "{action}")', '-e', 'end tell'], check=True, stdout=subprocess.DEVNULL)
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        result = pin_net()
        if result.nets[0].name == name:
            return result
        time.sleep(.15)
    raise AssertionError(f'GUI Undo/Redo did not restore {name}')

before = pin_net()
assert before.nets[0].name == 'PHI2_SALLY'
assert len([m for m in before.nets[0].members if m.kind == 'global_label']) == 2
blocked = client.plan_net_rename(before.nets[0].id, 'PHI2', snapshot=before)
assert not blocked.can_apply and not blocked.id and blocked.collisions
assert any(n.name == 'PHI2' for n in blocked.collisions)
assert pin_net() == before, 'Collision preview must preserve the revision and model'
request = _message('kiapi.schematic.commands.RenameNet')()
request.document.CopyFrom(doc)
request.plan_id = blocked.id
rejected(lambda: client._send(request, 'kiapi.schematic.commands.RenameNetResponse'), 'not found')
unchanged_files()
print('PASS: PHI2 collision refused without mutation or a merge override', flush=True)

plan = client.plan_net_rename(before.nets[0].id, 'IPC_PHI2_RENAMED', snapshot=before)
assert plan.can_apply and plan.id and len(plan.changes) == 2, plan
assert pin_net() == before, 'Preview must leave the snapshot unchanged'
assert {c.item.document.sheet_path.path_human_readable for c in plan.changes} == {'/cpu_gpu/', '/control/'}
other = SchematicClient(timeout_ms=60000)
rejected(lambda: other.rename_net(plan), 'not found')
geometry = wire_geometry(client.get_schematic_netlist(doc))
result = client.rename_net(plan)
after = pin_net()
assert after.nets[0].name == 'IPC_PHI2_RENAMED'
assert after.revision > before.revision
assert {(m.reference, m.unit, m.pin_number) for m in after.nets[0].members if m.kind == 'pin'} == {
       (m.reference, m.unit, m.pin_number) for m in before.nets[0].members if m.kind == 'pin'}
assert {m.text for m in after.nets[0].members if m.kind == 'global_label'} == {'IPC_PHI2_RENAMED'}
assert wire_geometry(client.get_schematic_netlist(doc)) == geometry
rejected(lambda: client.rename_net(plan), 'not found')
gui_undo_redo('PHI2_SALLY')
gui_undo_redo('IPC_PHI2_RENAMED', redo=True)
gui_undo_redo('PHI2_SALLY')
unchanged_files()
print('PASS: two-sheet rename, client-bound/consumed plan, geometry unchanged, GUI Undo/Redo', flush=True)

# An unsaved port edit changes the graph after a plan was issued. It also tests
# parent-sheet ownership when renaming/undoing a sheet pin alongside labels.
before = pin_net()
stale_plan = client.plan_net_rename(before.nets[0].id, 'IPC_WITH_PORT', snapshot=before)
wire = next(m for m in before.nets[0].members if m.kind == 'wire')
pin = SheetPin(name='PHI2_SALLY', side=1, shape=3)
pin.position.CopyFrom(wire.position)
sheet = client.create_sheet('rename_port', 'rename_port.kicad_sch', document=wire.document,
    position_nm=(wire.position.x_nm, wire.position.y_nm - 5000000),
    size_nm=(20000000, 10000000), pins=[pin])
current = pin_net()
assert current.revision > before.revision
rejected(lambda: client.rename_net(stale_plan), 'stale')
rejected(lambda: client.plan_net_rename(before.nets[0].id, 'IPC_WITH_PORT', snapshot=before), 'stale')
plan = client.plan_net_rename(current.nets[0].id, 'IPC_WITH_PORT', snapshot=current)
assert plan.can_apply and len(plan.changes) == 3, plan
assert any(c.item.kind == 'sheet_pin' for c in plan.changes)
geometry = wire_geometry(client.get_schematic_netlist(doc))
client.rename_net(plan)
assert pin_net().nets[0].name == 'IPC_WITH_PORT'
assert client.get_sheet_pins(wire.document, sheet.id.value)[0].name == 'IPC_WITH_PORT'
assert wire_geometry(client.get_schematic_netlist(doc)) == geometry
gui_undo_redo('PHI2_SALLY')
assert client.get_sheet_pins(wire.document, sheet.id.value)[0].name == 'PHI2_SALLY'
gui_undo_redo('IPC_WITH_PORT', redo=True)
assert client.get_sheet_pins(wire.document, sheet.id.value)[0].name == 'IPC_WITH_PORT'
gui_undo_redo('PHI2_SALLY')
assert client.get_sheet_pins(wire.document, sheet.id.value)[0].name == 'PHI2_SALLY'
unchanged_files()
print('PASS: stale plan/snapshot refused; unsaved labels+port renamed atomically with Undo/Redo', flush=True)

# Preview unsupported sources as well; no attempted mutation is needed.
full = client.get_schematic_netlist(doc)
bus = next(n for n in full.nets if n.is_bus)
assert not client.plan_net_rename(bus.id, 'IPC_BUS', snapshot=full).can_apply
power = next(n for n in full.nets if any(m.reference.startswith('#PWR') for m in n.members))
assert not client.plan_net_rename(power.id, 'IPC_GND', snapshot=full).can_apply
unchanged_files()
print('PASS: bus and power naming sources refused; existing schematic files unchanged', flush=True)
