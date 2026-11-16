"""Native footprint-library acceptance. Explicit marked project/socket only."""
from pathlib import Path
from uuid import uuid4
import argparse
import hashlib
import json
import shutil
import time
from kipy_schematic import PCBClient, _message
from kipy_schematic.pcb import Footprint, Pad, layer_id
from kipy.errors import ApiError


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--project', type=Path, required=True)
    p.add_argument('--socket', required=True)
    p.add_argument('--artifacts', type=Path, required=True)
    a = p.parse_args()
    project, out = a.project.resolve(), a.artifacts.resolve()
    assert (project / '.ipc-test-project').is_file()
    assert project.name not in ('CodexTest2', 'Codex800XL')
    out.mkdir(parents=True, exist_ok=True)
    c = PCBClient(socket_path=a.socket, timeout_ms=60000)
    docs = [d for d in c.get_open_documents() if Path(d.project.path).resolve() == project]
    assert len(docs) == 1
    doc = docs[0]
    table = project / 'fp-lib-table'
    assert not table.exists(), 'Start with the marked fixture and no project table'
    report = []

    def ok(text):
        report.append(text)
        (out / 'footprint-libraries-report.json').write_text(json.dumps(report, indent=2) + '\n')
        print('PASS', text, flush=True)

    def refuses(fn):
        try:
            fn()
        except (ApiError, RuntimeError, ValueError) as error:
            return str(error)
        raise AssertionError('Expected explicit refusal')

    def state():
        records = []
        for item in c.get_items(document=doc):
            # BOARD identity is regenerated on native reload; child/item UUIDs are retained.
            if 'parent' in item.DESCRIPTOR.fields_by_name:
                item.ClearField('parent')
            if 'definition' in item.DESCRIPTOR.fields_by_name:
                children = sorted(item.definition.items, key=lambda v: v.SerializeToString(deterministic=True))
                del item.definition.items[:]
                item.definition.items.extend(children)
            records.append(item.SerializeToString(deterministic=True).hex())
        return sorted(records)

    def undo(expected):
        c.run_action(doc, 'common.Interactive.undo')
        for _ in range(100):
            if state() == expected:
                return
            time.sleep(.05)
        raise AssertionError('Board Undo did not restore complete original item records')

    def target(definition, name):
        copy = Footprint()
        copy.CopyFrom(definition)
        copy.definition.id.library_nickname = 'ProjectFP'
        copy.definition.id.entry_name = name
        return copy

    original = state()
    footprints = c.get_items([1], doc)
    source = next(f for f in footprints if f.reference_field.text.text.text == 'R1')
    definition = c.get_footprint_definition(document=doc, footprint_id=source.id.value).definition
    assert definition.layer == layer_id('F.Cu') and not definition.symbol_path.path
    assert all(not p.net.name for p in (c._unpack_item(x) for x in definition.definition.items)
               if p.DESCRIPTOR.name == 'Pad')
    assert state() == original
    created = c.create_footprint_library('ProjectFP', 'ProjectFP.pretty', doc)
    assert created.scope == 'project' and created.writable
    assert '${KIPRJMOD}/ProjectFP.pretty' in table.read_text()
    persisted = table.read_bytes()
    saved = c.save_footprint_definition(target(definition, 'Recovered'), doc).definition
    path = project / 'ProjectFP.pretty/Recovered.kicad_mod'
    assert path.is_file()
    assert c.search_footprint_libraries('Recovered', doc, library='ProjectFP').total_matches == 1
    assert c.get_footprint_definition('ProjectFP:Recovered', doc).source == 'library'
    # Registration and library persistence do not dirty or save the board.
    assert state() == original
    c.revert_document(doc)
    assert state() == original
    before = state()
    placed = c.place_footprint('ProjectFP:Recovered', (290000000, 150000000), doc,
                             reference='R90', side='B.Cu', rotation_degrees=37)
    assert len(placed.pads) == 2
    live = state()
    rotated = c.get_footprint_definition(document=doc, footprint_id=placed.footprint.id.value)
    assert rotated.source == 'board' and state() == live
    c.recover_board_footprint(placed.footprint.id.value, 'ProjectFP:BackSide', doc)
    assert state() == live
    # Recovering a source must not consume, add to or alter its PCB Undo step.
    undo(before)
    assert (project / 'ProjectFP.pretty/BackSide.kicad_mod').is_file()
    ok('Missing project table: native create/save/search/read/place; no board dirty, source edits or library Undo')

    # Link an existing fixture library and preserve earlier native entries.
    existing = project / 'TestFP.pretty'
    linked = c.create_footprint_library('LinkedFP', existing, doc, link_existing=True)
    assert linked.scope == 'project'
    after_link = table.read_bytes()
    assert c.create_footprint_library('LinkedFP', existing, doc, link_existing=True) == linked
    assert table.read_bytes() == after_link
    malformed = out / 'MalformedExisting.pretty'
    shutil.copytree(project / 'Broken.pretty', malformed, dirs_exist_ok=True)
    refuses(lambda: c.create_footprint_library('MalformedExisting', malformed, doc, link_existing=True))
    assert table.read_bytes() == after_link
    refuses(lambda: c.create_footprint_library('LinkedFP', 'Other.pretty', doc))
    refuses(lambda: c.create_footprint_library('AnotherNick', existing, doc, link_existing=True))
    refuses(lambda: c.create_footprint_library('Missing', 'Missing.pretty', doc, link_existing=True))
    refuses(lambda: c.create_footprint_library('Wrong', 'Wrong.kicad_mod', doc))
    refuses(lambda: c.create_footprint_library('NoParent', 'Absent/Part.pretty', doc))
    assert table.read_bytes() == after_link
    assert 'ProjectFP' in table.read_text() and persisted != after_link
    matches = c.search_footprint_libraries(document=doc, library='LinkedFP')
    assert matches.matches and not matches.errors
    full = c.search_footprint_libraries(document=doc, library='ProjectFP')
    page = c.search_footprint_libraries(document=doc, library='ProjectFP', limit=1, offset=1)
    assert full.total_matches >= 2 and page.total_matches == full.total_matches
    assert len(page.matches) == 1 and page.matches[0].lib_id == full.matches[1].lib_id
    part = matches.matches[0].lib_id
    with c.commit('Immediately place linked library', doc):
        c.place_footprint(part.library_nickname + ':' + part.entry_name,
                          (290000000, 150000000), doc, reference='R91')
    undo(original)
    ok('Existing library: native link, idempotency, path/nickname/URI conflicts and immediate placement')

    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    refuses(lambda: c.save_footprint_definition(saved, doc))
    assert hashlib.sha256(path.read_bytes()).hexdigest() == digest
    changed = target(saved, 'Recovered')
    pad_index = next(i for i, item in enumerate(changed.definition.items) if item.type_url.endswith('.Pad'))
    pad = c._unpack_item(changed.definition.items[pad_index])
    pad.position.x_nm += 500000
    changed.definition.items[pad_index].Pack(pad)
    refreshed = c.save_footprint_definition(changed, doc, overwrite=True).definition
    assert hashlib.sha256(path.read_bytes()).hexdigest() != digest
    assert c._unpack_item(refreshed.definition.items[pad_index]).position.x_nm == pad.position.x_nm
    placed = c.place_footprint('ProjectFP:Recovered', (290000000, 150000000), doc, reference='R92')
    got = next(p for p in placed.pads if p.number == pad.number)
    assert got.position.x_nm == 290000000 + pad.position.x_nm
    # Change the external library again while retaining the already placed model.
    c.save_footprint_definition(target(saved, 'Recovered'), doc, overwrite=True)
    exact = c.get_footprint_definition(document=doc, footprint_id=placed.footprint.id.value).definition
    exact_pad = next(p for p in (c._unpack_item(v) for v in exact.definition.items)
                     if p.DESCRIPTOR.name == 'Pad' and p.number == pad.number)
    assert exact_pad.position.x_nm == pad.position.x_nm
    c.recover_board_footprint(placed.footprint.id.value, 'ProjectFP:ExactLive', doc)
    recovered = c.get_footprint_definition('ProjectFP:ExactLive', doc).definition
    recovered_pad = next(p for p in (c._unpack_item(v) for v in recovered.definition.items)
                         if p.DESCRIPTOR.name == 'Pad' and p.number == pad.number)
    assert recovered_pad.position.x_nm == pad.position.x_nm
    undo(original)
    invalid = target(saved, 'Invalid')
    invalid.definition.items.add().type_url = 'type.googleapis.com/unknown.FootprintChild'
    refuses(lambda: c.save_footprint_definition(invalid, doc))
    invalid = target(saved, 'Invalid')
    bad = c._unpack_item(invalid.definition.items[pad_index]); bad.net.name = 'TYPO'
    invalid.definition.items[pad_index].Pack(bad)
    refuses(lambda: c.save_footprint_definition(invalid, doc))
    invalid = target(saved, 'Invalid'); invalid.orientation.value_degrees = float('nan')
    refuses(lambda: c.save_footprint_definition(invalid, doc))
    assert not (project / 'ProjectFP.pretty/Invalid.kicad_mod').exists()
    assert not list((project / 'ProjectFP.pretty').glob('.ipc-*'))
    ok('Overwrite explicit; cache and immediate placement use new geometry; invalid data/unknown children/net/NaN refuse before writing')

    path.chmod(0o444)
    try:
        content = path.read_bytes()
        refuses(lambda: c.save_footprint_definition(changed, doc, overwrite=True))
        assert path.read_bytes() == content
    finally:
        path.chmod(0o644)
    directory = project / 'ProjectFP.pretty'
    directory.chmod(0o555)
    try:
        refuses(lambda: c.save_footprint_definition(target(saved, 'ReadonlyDirectory'), doc))
        assert not (directory / 'ReadonlyDirectory.kicad_mod').exists()
    finally:
        directory.chmod(0o755)
    table.chmod(0o444)
    try:
        refuses(lambda: c.create_footprint_library('ReadOnlyTable', 'ReadOnlyTable.pretty', doc))
        assert not (project / 'ReadOnlyTable.pretty').exists()
    finally:
        table.chmod(0o644)
    with c.commit('Library changes refused inside PCB transaction', doc):
        refuses(lambda: c.save_footprint_definition(target(saved, 'Pending'), doc))
        refuses(lambda: c.create_footprint_library('Pending', 'Pending.pretty', doc))
    assert not (project / 'Pending.pretty').exists()
    assert not (project / 'ProjectFP.pretty/Pending.kicad_mod').exists()
    global_matches = c.search_footprint_libraries(document=doc, library='GlobalTestFP')
    assert global_matches.matches
    global_def = target(saved, 'Forbidden')
    global_def.definition.id.library_nickname = 'GlobalTestFP'
    refuses(lambda: c.save_footprint_definition(global_def, doc))
    refuses(lambda: c.search_footprint_libraries(document=doc, library='Unregistered'))
    errors = c.search_footprint_libraries(document=doc, library='BrokenTestFP')
    assert errors.errors and not errors.matches
    assert state() == original
    ok('Read-only files/table, pending commit, global writes and missing/broken libraries protected')
    c.save_document(doc)
    c.revert_document(doc)
    assert c.get_footprint_definition('ProjectFP:Recovered', doc).source == 'library'
    ok('Native board save/revert retains library registration and definitions')


if __name__ == '__main__':
    main()
