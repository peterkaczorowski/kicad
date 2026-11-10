"""Integration checks against an isolated, freshly opened test schematic.

Create a directory with a .ipc-test-project marker and an empty root.kicad_sch,
open it in the test KiCad, then pass --project to this script. Never targets a
real user project. KICAD_API_SOCKET selects the test editor instance.
"""
import argparse
import hashlib
from pathlib import Path
from uuid import uuid4

from kipy.errors import ApiError
from kipy_schematic import SheetPin, SchematicClient, _message

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--project', type=Path, required=True)
args = parser.parse_args()
project = args.project.resolve()
assert (project / '.ipc-test-project').is_file(), 'Missing isolated test project marker'
client = SchematicClient(timeout_ms=15000)
doc = client.active_document()
assert Path(doc.project.path).resolve() == project, 'Refusing to modify a different project'
assert not client.get_sheets(doc), 'Start with an empty test page'
assert client.get_open_documents(2) == [], 'Close the test symbol editor first'


def rejected(call, expected):
    try:
        call()
    except ApiError as error:
        assert expected in str(error), str(error)
    else:
        raise AssertionError('Expected the request to fail')


pin = SheetPin(name='DATA', side=1, shape=3)
pin.position.x_nm, pin.position.y_nm = 25000000, 30000000
power = client.create_sheet('power', 'power.kicad_sch', document=doc,
                            position_nm=(25000000, 25000000), pins=[pin])
assert (project / 'power.kicad_sch').is_file()
assert '(page "")' not in (project / 'power.kicad_sch').read_text(), 'Do not create invalid blank page instances'
sheets = client.get_sheets(doc)
assert len(sheets) == 1 and sheets[0].id == power.id
assert sheets[0].pins[0].name == 'DATA'
assert client.get_sheet_pins(doc, power.id.value)[0].name == 'DATA'
assert power.id.value not in (project / 'root.kicad_sch').read_text(), 'Read must include unsaved state'

before = hashlib.sha256((project / 'power.kicad_sch').read_bytes()).digest()
rejected(lambda: client.create_sheet('duplicate-file', 'power.kicad_sch', document=doc), 'already exists')
assert hashlib.sha256((project / 'power.kicad_sch').read_bytes()).digest() == before
rejected(lambda: client.create_sheet('missing', 'missing.kicad_sch', document=doc, link_existing=True), 'does not exist')
rejected(lambda: client.create_sheet('recursive', 'root.kicad_sch', document=doc, link_existing=True), 'recursive')
rejected(lambda: client.create_sheet('power', 'duplicate.kicad_sch', document=doc), 'name already exists')
bad_pin = SheetPin(name='invalid', side=1, shape=1)
bad_pin.position.x_nm = 1000000
rejected(lambda: client.create_sheet('bad', 'bad.kicad_sch', document=doc, pins=[bad_pin]), 'declared sheet edge')
assert not (project / 'bad.kicad_sch').exists()

try:
    with client.commit('Cancelled test transaction', document=doc):
        client.create_sheet('cancelled', 'cancelled.kicad_sch', document=doc)
        raise ValueError('drop')
except ValueError:
    pass
assert [sheet.name for sheet in client.get_sheets(doc)] == ['power']
assert (project / 'cancelled.kicad_sch').is_file(), 'Cancellation retains the new child file'
assert len(client.get_open_documents()) == 2, 'Cancelled sheet must leave the hierarchy'

power_doc = next(page for page in client.get_open_documents()
                 if page.sheet_path.path[-1].value == power.id.value)
child = client.create_sheet('nested', 'nested.kicad_sch', document=power_doc)
alias = client.create_sheet('alias', 'power.kicad_sch', document=doc, link_existing=True)
alias_doc = next(page for page in client.get_open_documents()
                 if page.sheet_path.path[-1].value == alias.id.value)
assert client.get_sheets(alias_doc)[0].id == child.id, 'Reuse the existing live screen, not stale disk content'

with client.commit('Two sheets in one Undo step', document=doc):
    client.create_sheet('group-a', 'group-a.kicad_sch', document=doc)
    client.create_sheet('group-b', 'group-b.kicad_sch', document=doc)
assert {sheet.name for sheet in client.get_sheets(doc)} == {'power', 'alias', 'group-a', 'group-b'}

request = _message('kiapi.common.commands.GetItems')()
request.header.document.CopyFrom(doc)
request.types.append(15)  # unsupported schematic item kind, never silently ignored
rejected(lambda: client._send(request, 'kiapi.common.commands.GetItemsResponse'), 'currently supports')
rejected(lambda: client.get_open_documents(999), 'not supported')
print('PASS: live reads, ports, create/link, hierarchy, transaction drop, shared screens, validation')
print('GUI checks remaining: Undo removes group-a/group-b together; Redo restores them; Save and Revert retain the active child and viewport.')
