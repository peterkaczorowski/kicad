#!/usr/bin/env python3
"""Destructive acceptance on an explicitly marked disposable PCB project only.

The external JSON writes here deliberately reproduce a stale open project's settings;
production edits use SetProjectSettings. No default socket or project is assumed.
"""
import argparse
import copy
import hashlib
import json
import time
from uuid import uuid4
from pathlib import Path
from google.protobuf.json_format import MessageToDict
from kipy.errors import ApiError
from kipy_schematic import PCBClient, Document, _message, _pool


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', required=True, type=Path)
    parser.add_argument('--socket', required=True)
    parser.add_argument('--artifacts', required=True, type=Path)
    args = parser.parse_args()
    project = args.project.resolve()
    assert (project / '.ipc-test-project').is_file(), 'An owned disposable fixture is required'
    args.artifacts.mkdir(parents=True, exist_ok=True)
    client = PCBClient(socket_path=args.socket, timeout_ms=20000)
    doc = client.active_document()
    assert Path(doc.project.path).resolve() == project, 'Socket is serving a different project'
    path = project / (doc.project.name + '.kicad_pro')
    rules_path = path.with_suffix('.kicad_dru')
    assert path.is_file()
    checks = []
    trace_type = _pool.FindEnumTypeByName('kiapi.common.types.KiCadObjectType').values_by_name['KOT_PCB_TRACE'].number

    def passed(name):
        checks.append(name)
        print('PASS', name, flush=True)

    def data(state):
        return MessageToDict(state.settings, preserving_proto_field_name=True,
                             always_print_fields_with_no_presence=True)

    def record_items():
        records = []
        for item in client.get_items(document=doc):
            # BOARD itself gets a fresh runtime UUID on native Revert. Its children
            # retain their persisted identities; compare all their other fields.
            item.ClearField('parent')
            records.append(item.SerializeToString(deterministic=True).hex())
        return sorted(records)

    def refuses(fn, substring=None):
        before = client.get_project_settings(doc)
        before_items = record_items()
        files = (path.read_bytes(), rules_path.read_bytes() if rules_path.exists() else None)
        try:
            fn()
        except ApiError as error:
            if substring:
                assert substring.lower() in str(error).lower(), str(error)
        else:
            raise AssertionError('Expected an atomic refusal')
        after = client.get_project_settings(doc)
        assert after == before, (after, before)
        assert before_items == record_items()
        assert files == (path.read_bytes(), rules_path.read_bytes() if rules_path.exists() else None)

    initial = client.get_project_settings(doc)
    assert not initial.modified, 'Start with a clean disposable project'
    initial_items = record_items()
    (args.artifacts / 'initial-items.json').write_text(json.dumps(initial_items))
    original = path.read_bytes()
    (args.artifacts / 'before.kicad_pro').write_bytes(original)
    disk = json.loads(original)
    video = next(nc for nc in disk['net_settings']['classes'] if nc['name'] == 'VIDEO_CLASS')
    assert video['track_width'] == .2032
    video['track_width'] = .127
    default = next(nc for nc in disk['net_settings']['classes'] if nc['name'] == 'Default')
    for i, name in enumerate(['ExternalA', 'ExternalB', 'ExternalC']):
        disk['net_settings']['classes'].append(dict(default, name=name, priority=i+1))
    disk['net_settings']['netclass_patterns'].extend([
        {'pattern': 'GND', 'netclass': 'ExternalA'},
        {'pattern': '+5V', 'netclass': 'ExternalB'},
        {'pattern': 'Net-(D2-*', 'netclass': 'ExternalC'},
    ])
    disk['board']['design_settings']['rules']['min_track_width'] = .127
    severities = disk['board']['design_settings']['rule_severities']
    severities.pop('hole_to_hole')
    severities['hole_near_hole'] = 'warning'  # Native KiCad 8 spelling, migrated on reload.
    path.write_text(json.dumps(disk, indent=2) + '\n')
    external_bytes = path.read_bytes()
    assert data(client.get_project_settings(doc))['net_settings'] != disk['net_settings']
    client.save_document(doc)
    assert path.read_bytes() == external_bytes, 'Clean board save must not overwrite external settings'
    loaded = client.reload_project_settings(doc)
    assert not loaded.modified
    loaded_data = data(loaded)
    assert {n['name'] for n in loaded_data['net_settings']['classes']} >= {'ExternalA', 'ExternalB', 'ExternalC'}
    assert next(n for n in loaded_data['net_settings']['classes'] if n['name'] == 'VIDEO_CLASS')['track_width'] == .127
    assert loaded_data['board_design_settings']['rules']['min_track_width'] == .127
    assert loaded_data['board_design_settings']['rule_severities']['hole_to_hole'] == 'warning'
    assert record_items() == initial_items
    assert path.read_bytes() == external_bytes
    passed('External classes/patterns/constraints reloaded without changing board items or disk')

    members = client.get_nets_by_netclass('VIDEO_CLASS', doc)
    assert {net.name for net in members} == {'Net-(D1-A)', 'Net-(D1-K)'}, members
    query = _message('kiapi.board.commands.GetNetClassForNets')()
    query.net.extend(members)
    effective = client._send(query, 'kiapi.board.commands.NetClassForNetsResponse')
    assert all(effective.classes[net.name].board.track_width.value_nm == 127000 for net in members)
    assert {net.name for net in client.get_nets_by_netclass('ExternalA', doc)} == {'GND'}
    passed('Native effective classes and membership, including literal parentheses')
    assert {net.name for net in client.get_nets_by_netclass('ExistingImplicit', doc)} == {'Net-(D1-A)'}
    assert 'ExistingImplicit' not in {nc['name'] for nc in loaded_data['net_settings']['classes']}
    passed('Native implicit label-class membership without creating an explicit definition')

    with client.edit_project_settings(doc) as settings:
        settings['board_design_settings']['rules']['min_track_width'] = .13
        settings['board_design_settings']['track_widths'] = [0, .127, .2, .3]
        settings['board_design_settings']['via_dimensions'] = [{'diameter': 0, 'drill': 0}, {'diameter': .6, 'drill': .3}]
        settings['board_design_settings']['diff_pair_dimensions'] = [{'width': 0, 'gap': 0, 'via_gap': 0}, {'width': .15, 'gap': .2, 'via_gap': .3}]
        settings['board_design_settings']['rule_severities']['track_width'] = 'error'
        settings['net_settings']['classes'].append({
            'name': 'IPC_PAIR', 'priority': 0, 'track_width': .18, 'clearance': .18,
            'via_diameter': .6, 'via_drill': .3, 'microvia_diameter': .3, 'microvia_drill': .1,
            'diff_pair_width': .15, 'diff_pair_gap': .2, 'diff_pair_via_gap': .2,
        })
        settings['net_settings']['netclass_patterns'].append({'pattern': 'Net-(D2-*', 'netclass': 'IPC_PAIR'})
        settings['net_settings']['netclass_assignments']['GND'] = ['ExternalA', 'IPC_PAIR']
        settings['custom_rules'] = '(version 1)\n(rule "IPC Video width" (condition "A.NetClass == \'VIDEO_CLASS\'") (constraint track_width (min 0.4mm)))\n'
    edited = client.get_project_settings(doc)
    assert edited.modified
    assert path.read_bytes() == external_bytes
    assert record_items() == initial_items
    assert {n.name for n in client.get_nets_by_netclass('IPC_PAIR', doc)} >= {'GND', 'Net-(D2-A)'}
    definition = next(n for n in data(edited)['net_settings']['classes'] if n['name'] == 'IPC_PAIR')
    assert definition['microvia_drill'] == .1 and definition['diff_pair_width'] == .15
    refuses(lambda: client.reload_project_settings(doc), 'unsaved')
    passed('Whole settings batch remains live/unsaved and default reload refuses atomically')
    copy_path = project / ('ipc-settings-copy-' + uuid4().hex + '.kicad_pcb')
    client.save_copy_of_document(doc, copy_path, include_project=True)
    assert copy_path.with_suffix('.kicad_dru').read_text() == data(edited)['custom_rules']
    assert json.loads(copy_path.with_suffix('.kicad_pro').read_text())['board']['design_settings']['rules']['min_track_width'] == .13
    assert client.get_project_settings(doc) == edited
    passed('Project SaveCopy includes unsaved rules/settings and leaves the source dirty')
    # Failed persistence must not pair newly written rules with an old read-only project.
    mode = path.stat().st_mode
    try:
        path.chmod(0o444)
        refuses(lambda: client.save_document(doc), 'could not be saved')
    finally:
        path.chmod(mode)
    passed('Failed project save retains dirty settings and restores the previous rules file')
    # SaveDocument must persist both files, preserving externally added classes.
    client.save_document(doc)
    saved = client.get_project_settings(doc)
    assert not saved.modified
    saved_disk = json.loads(path.read_text())
    assert {n['name'] for n in saved_disk['net_settings']['classes']} >= {'ExternalA', 'ExternalB', 'ExternalC'}
    assert saved_disk['board']['design_settings']['rules']['min_track_width'] == .13
    assert rules_path.read_text() == data(saved)['custom_rules']
    passed('SaveDocument persists native settings and staged rules without reverting new classes')

    with client.edit_project_settings(doc) as settings:
        settings['board_design_settings']['rules']['min_track_width'] = .15
        settings['custom_rules'] = '(version 1)\n'
    refuses(lambda: client.reload_project_settings(doc), 'unsaved')
    discarded = client.reload_project_settings(doc, discard_unsaved=True)
    assert not discarded.modified
    assert data(discarded)['board_design_settings']['rules']['min_track_width'] == .13
    assert data(discarded)['custom_rules'] == rules_path.read_text()
    assert record_items() == initial_items
    passed('Explicit discard reload restores both native settings and custom rules')

    with client.edit_project_settings(doc) as settings:
        settings['net_settings']['classes'] = [n for n in settings['net_settings']['classes'] if n['name'] != 'IPC_PAIR']
        settings['net_settings']['netclass_patterns'] = [p for p in settings['net_settings']['netclass_patterns'] if p['netclass'] != 'IPC_PAIR']
        settings['net_settings']['netclass_assignments']['GND'] = ['ExternalA']
    refuses(lambda: client.get_nets_by_netclass('IPC_PAIR', doc), 'unknown')
    client.reload_project_settings(doc, discard_unsaved=True)
    assert {n.name for n in client.get_nets_by_netclass('IPC_PAIR', doc)} >= {'GND', 'Net-(D2-A)'}
    passed('Native class creation/deletion, parameters, patterns and composite assignments')

    # The reader reports live rules, not an external file that the engine has not loaded yet.
    previous_rules = rules_path.read_text()
    external_rules = previous_rules.replace('(min 0.4mm)', '(min 0.35mm)')
    assert external_rules != previous_rules
    rules_path.write_text(external_rules)
    assert data(client.get_project_settings(doc))['custom_rules'] == previous_rules
    assert data(client.reload_project_settings(doc))['custom_rules'] == external_rules
    assert rules_path.read_text() == external_rules
    passed('External custom rules stay separate from live engine state until explicit reload')

    with client.edit_project_settings(doc) as settings:
        settings['custom_rules'] = ''
    assert data(client.get_project_settings(doc))['custom_rules'] == ''
    assert client.get_project_settings(doc).modified
    client.reload_project_settings(doc, discard_unsaved=True)
    assert data(client.get_project_settings(doc))['custom_rules'] == external_rules
    passed('Empty rule text is an explicit staged clear, and can be discarded')

    for invalid in ('negative', 'duplicate', 'missing-default', 'missing-class', 'severity', 'unknown', 'syntax', 'condition', 'range'):
        current = client.get_project_settings(doc)
        candidate = data(current)
        if invalid == 'negative': candidate['net_settings']['classes'][0]['track_width'] = -1
        elif invalid == 'duplicate': candidate['net_settings']['classes'].append(copy.deepcopy(candidate['net_settings']['classes'][0]))
        elif invalid == 'missing-default': candidate['net_settings']['classes'] = [n for n in candidate['net_settings']['classes'] if n['name'] != 'Default']
        elif invalid == 'missing-class': candidate['net_settings']['netclass_patterns'].append({'pattern': 'X*', 'netclass': 'Missing'})
        elif invalid == 'severity': candidate['board_design_settings']['rule_severities']['track_width'] = 'broken'
        elif invalid == 'unknown': candidate['board_design_settings']['rules']['invented'] = .1
        elif invalid == 'syntax': candidate['custom_rules'] = '(version 1) (rule "unclosed"'
        elif invalid == 'condition': candidate['custom_rules'] = '(version 1) (rule "bad" (condition "A.NoSuchProperty == 1") (constraint clearance (min 0.2mm)))'
        elif invalid == 'range': candidate['board_design_settings']['rules']['min_track_width'] = 900
        refuses(lambda: client.set_project_settings(candidate, doc, expected_revision=current.revision,
                fields=['net_settings', 'board_design_settings', 'custom_rules']))
        passed('Atomic validation: ' + invalid)

    current = client.get_project_settings(doc)
    client.set_project_settings({'board_design_settings': {'rules': {'min_track_width': .135}}}, doc,
            expected_revision=current.revision, fields=['board_design_settings.rules.min_track_width'])
    partial = data(client.get_project_settings(doc))
    assert partial['board_design_settings']['rules']['min_track_width'] == .135
    assert partial['net_settings'] == data(current)['net_settings']
    assert partial['custom_rules'] == external_rules
    client.reload_project_settings(doc, discard_unsaved=True)
    passed('Leaf field mask changes one constraint without replacing omitted settings')

    with client.edit_project_settings(doc) as settings:
        settings['board_design_settings']['rules']['min_track_width'] = .145
    client.revert_document(doc, discard_unsaved=True) # Reverts the board, not .kicad_pro settings.
    assert data(client.get_project_settings(doc))['board_design_settings']['rules']['min_track_width'] == .145
    assert client.get_project_settings(doc).modified
    refuses(lambda: client.reload_project_settings(doc), 'unsaved')
    client.reload_project_settings(doc, discard_unsaved=True)
    passed('Board Revert retains unsaved project settings and their discard protection')

    stale = client.get_project_settings(doc)
    with client.edit_project_settings(doc) as settings:
        settings['board_design_settings']['rules']['min_track_width'] = .14
    refuses(lambda: client.set_project_settings(stale.settings, doc, expected_revision=stale.revision,
                                               fields=['board_design_settings.rules.min_track_width']), 'stale')
    client.reload_project_settings(doc, discard_unsaved=True)
    wrong = Document(); wrong.CopyFrom(doc); wrong.project.path = str(project.parent / 'not-this-project')
    refuses(lambda: client.reload_project_settings(wrong), None)
    passed('Stale revision and foreign project refused')

    # Use a second transport to prove the native guard, not only the local context-manager guard.
    other = PCBClient(socket_path=args.socket, timeout_ms=20000)
    with client.commit('pending settings protection', doc):
        try: other.reload_project_settings(doc, discard_unsaved=True)
        except ApiError: pass
        else: raise AssertionError('Native pending-commit guard did not refuse')
    passed('Native pending transaction guard across clients')

    # A failed external parse must leave the live state and board untouched.
    valid_bytes = path.read_bytes()
    path.write_text('{broken')
    refuses(lambda: client.reload_project_settings(doc, discard_unsaved=True))
    path.write_bytes(valid_bytes)
    passed('Malformed external file reload leaves model intact')

    with client.commit('Settings reload retains geometry Undo', doc):
        track = client.add_track((300000000, 100000000), (301000000, 100000000), 250000,
                                 document=doc, net='+5V')
    client.reload_project_settings(doc)
    assert any(item.id == track.id for item in client.get_items([trace_type], doc))
    refuses(lambda: client.revert_document(doc), 'unsaved')

    def wait_track(present):
        for _ in range(100):
            try:
                if any(item.id == track.id for item in client.get_items([trace_type], doc)) == present:
                    return
            except ApiError:
                pass
            time.sleep(.05)
        raise AssertionError('Queued Undo/Redo did not reach expected geometry')

    assert client.run_action(doc, 'common.Interactive.undo') == 1
    wait_track(False)
    assert client.run_action(doc, 'common.Interactive.redo') == 1
    wait_track(True)
    passed('Settings reload preserves geometry, board dirty protection and native Undo/Redo')

    client.revert_document(doc, discard_unsaved=True)
    final_items = record_items()
    (args.artifacts / 'final-items.json').write_text(json.dumps(final_items))
    assert final_items == initial_items
    assert not client.get_project_settings(doc).modified
    passed('Explicit Python document Revert and final native readback')
    (args.artifacts / 'live-settings-result.json').write_text(json.dumps({
        'checks': checks, 'project': str(project), 'document': doc.board_filename,
        'settings': data(client.get_project_settings(doc)),
        'project_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
        'rules_sha256': hashlib.sha256(rules_path.read_bytes()).hexdigest(),
        'board_items_preserved': len(initial_items),
    }, indent=2) + '\n')


if __name__ == '__main__':
    main()
