#!/usr/bin/env python3
"""Acceptance checks for the four document commands in one already-open editor.

Use a disposable project with .ipc-test-project. The source must contain the
literal STAGE1_INITIAL marker. This script deliberately changes that file and
uses Select All/Delete/Undo/Redo on the open document. Never use user projects.
Requires the generated client 0.5 on PYTHONPATH. UI opening and modal/view tests
are separate so this script can run on every platform.
"""
import argparse
import json
import os
import time
from pathlib import Path

from kipy.errors import ApiError
from kipy_schematic import Document, SchematicClient, _message


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--type', type=int, choices=range(1, 6), required=True)
    parser.add_argument('--socket', required=True)
    args = parser.parse_args()
    project = args.project.resolve()
    source = args.source.resolve()
    if not (project / '.ipc-test-project').is_file() or not source.is_relative_to(project):
        parser.error('Both source and project must be inside a marked disposable test project')
    if any(part.lower() in ('codex800xl', 'codextest2') for part in project.parts):
        parser.error('User reference projects are never mutation test targets')
    original = source.read_text()
    if 'STAGE1_INITIAL' not in original:
        parser.error('Source must be an unused stage-1 fixture containing STAGE1_INITIAL')
    client = SchematicClient(socket_path=args.socket, timeout_ms=10000)
    documents = client.get_open_documents(args.type)
    documents = [doc for doc in documents if not doc.project.path
                 or Path(doc.project.path).resolve() == project]
    if len(documents) != 1:
        raise RuntimeError(f'Expected one matching editor document, found {len(documents)}')
    doc = documents[0]
    outputs = project / f'results-{args.type}'
    outputs.mkdir(exist_ok=True)
    copy = outputs / ('readback' + source.suffix)
    report = []

    def passed(name):
        report.append(name)
        (outputs / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        print('PASS', name, flush=True)

    def state():
        client.save_copy_of_document(doc, copy, overwrite=True)
        return copy.read_text()

    def wait_state(predicate):
        # RAS_OK accepts a queued GUI action; it is not a completion notification.
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            try:
                current = state()
                if predicate(current):
                    return current
            except ApiError as error:
                if error.code != 7:  # An action may still be updating the GUI.
                    raise
            time.sleep(0.05)
        raise AssertionError('Queued action did not produce the expected live model')

    def rejected(call, status=3):
        try:
            call()
        except ApiError as error:
            assert error.code == status, (error.code, str(error))
        else:
            raise AssertionError('Expected rejection')

    initial = state()
    assert 'STAGE1_INITIAL' in initial
    passed('SaveCopy reads the live initial model')
    source.write_text(original.replace('STAGE1_INITIAL', 'STAGE1_EXTERNAL'))
    client.revert_document(doc)
    clean = state()
    assert 'STAGE1_EXTERNAL' in clean and 'STAGE1_INITIAL' not in clean
    passed('Revert reloads external changes without a dialog')

    assert client.run_action(doc, 'does.not.exist') == 2
    passed('Unknown action returns RAS_INVALID')
    assert client.run_action(doc, 'common.Interactive.selectAll') == 1
    assert client.run_action(doc, 'common.Interactive.delete') == 1
    dirty = wait_state(lambda text: text != clean)
    assert dirty != clean
    rejected(lambda: client.revert_document(doc))
    assert state() == dirty
    rejected(lambda: client.run_action(doc, 'common.Control.revert'))
    assert state() == dirty
    passed('Default Revert and action Revert refuse unsaved changes without data loss')

    assert client.run_action(doc, 'common.Interactive.undo') == 1
    assert wait_state(lambda text: text == clean) == clean
    assert client.run_action(doc, 'common.Interactive.redo') == 1
    assert wait_state(lambda text: text == dirty) == dirty
    passed('RunAction Undo/Redo restores identical serialized items and UUIDs')
    client.save_document(doc)
    # A successful save clears the dirty flag; reload must now succeed without discard.
    client.revert_document(doc)
    assert state() == dirty
    passed('SaveDocument writes the changed model and clears the dirty flag')

    # Explicit Revert must bypass timestamp/preload caches, even if mtime is unchanged.
    saved_stat = source.stat()
    source.write_text(original.replace('STAGE1_INITIAL', 'STAGE1_EXTERNAL'))
    os.utime(source, ns=(saved_stat.st_atime_ns, saved_stat.st_mtime_ns))
    client.revert_document(doc)
    assert state() == clean
    client.run_action(doc, 'common.Interactive.selectAll')
    client.run_action(doc, 'common.Interactive.delete')
    assert wait_state(lambda text: text != clean) != clean
    rejected(lambda: client.revert_document(doc))
    client.revert_document(doc, discard_unsaved=True)
    assert state() == clean
    passed('Explicit discard_unsaved reloads the saved version')

    wrong = Document()
    wrong.CopyFrom(doc)
    if args.type in (2, 4):
        wrong.lib_id.entry_name = 'does-not-exist'
    elif args.type == 1:
        wrong.sheet_path.path[-1].value = '00000000-0000-0000-0000-000000000000'
    else:
        wrong.board_filename = 'does-not-exist' + source.suffix
    rejected(lambda: client.save_document(wrong))
    rejected(lambda: client.revert_document(wrong, discard_unsaved=True))
    rejected(lambda: client.run_action(wrong, 'common.Interactive.undo'))
    rejected(lambda: client.save_copy_of_document(wrong, copy, overwrite=True))
    rejected(lambda: client.save_copy_of_document(doc, copy))
    rejected(lambda: client.save_copy_of_document(doc, outputs / 'wrong.extension'))
    passed('Invalid document, destination extension and overwrite are rejected')

    # Use an independent client so Python's local guard cannot hide server regressions.
    observer = SchematicClient(socket_path=args.socket, timeout_ms=10000)
    with client.commit('Exclusion test', document=doc):
        for name, response in [('SaveDocument', 'google.protobuf.Empty'),
                               ('SaveCopyOfDocument', 'google.protobuf.Empty'),
                               ('RevertDocument', 'google.protobuf.Empty'),
                               ('RunAction', 'kiapi.common.commands.RunActionResponse')]:
            request = _message('kiapi.common.commands.' + name)()
            request.document.CopyFrom(doc)
            if name == 'RunAction':
                request.action = 'common.Interactive.undo'
            rejected(lambda request=request, response=response: observer._send(request, response))
    passed('All four commands refuse another client\'s open API commit')
    assert state() == clean
    passed('Rejections preserve the live model')

    valid_file = source.read_text()
    try:
        source.write_text('(deliberately_invalid_native_document)\n')
        rejected(lambda: client.revert_document(doc, discard_unsaved=True))
        assert state() == clean
    finally:
        source.write_text(valid_file)
    client.revert_document(doc)
    assert state() == clean
    passed('Malformed disk input is refused before replacing the live model')

    mode = source.stat().st_mode
    parent_mode = source.parent.stat().st_mode
    try:
        source.chmod(0o444)
        if args.type == 4:
            source.parent.chmod(0o555)
        rejected(lambda: client.save_document(doc))
    finally:
        source.chmod(mode)
        source.parent.chmod(parent_mode)
    passed('Read-only document/library refuses SaveDocument')
    print('Completed', len(report), 'checks for type', args.type)


if __name__ == '__main__':
    main()
