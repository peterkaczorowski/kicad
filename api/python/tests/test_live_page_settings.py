"""Paper settings acceptance test on a disposable HyperSpeed copy.

Requires a .ipc-test-project marker and a dedicated settings directory with
IPC enabled. Starts its own editor, saves only the copy, and tests GUI Undo.
"""
import argparse
from contextlib import contextmanager
import os
from pathlib import Path
import re
import subprocess
import time
from kipy.errors import ApiError
from kipy_schematic import Document, SchematicClient

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--project', type=Path, required=True)
parser.add_argument('--config', type=Path, required=True)
parser.add_argument('--app', type=Path, required=True)
parser.add_argument('--capture', action='store_true')
args = parser.parse_args()
project = args.project.resolve()
assert (project / '.ipc-test-project').is_file(), 'Only a disposable copy is allowed'
assert args.config.resolve().parent == project.parent, 'Use an isolated test configuration'
executable = args.app.resolve() / 'Contents/Applications/eeschema.app/Contents/MacOS/eeschema'


def poll(action, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if result := action():
            return result
        time.sleep(.15)
    raise AssertionError('The editor did not reach the expected state')


@contextmanager
def editor():
    with (project.parent / 'editor.log').open('ab') as log:
        process = subprocess.Popen([str(executable), str(project / 'HyperSpeed.kicad_sch')],
            env=dict(os.environ, KICAD_CONFIG_HOME=str(args.config.resolve())), stdout=log, stderr=log)
        try:
            client = SchematicClient(timeout_ms=2000)
            def ready():
                assert process.poll() is None, 'Test editor stopped during startup'
                try:
                    return client.get_open_documents()
                except Exception:
                    return None  # Socket/GUI may not be ready yet.
            documents = poll(ready, timeout=30)
            assert all(Path(doc.project.path).resolve() == project for doc in documents), 'Wrong editor instance'
            yield client, process.pid, documents
        finally:
            if process.poll() is None:
                process.terminate()  # This is our disposable test process only.
                process.wait(timeout=15)


def gui(pid, *lines):
    command = ['osascript', '-e', f'tell application "System Events" to tell (first process whose unix id is {pid})']
    for line in lines:
        command += ['-e', line]
    return subprocess.check_output(command + ['-e', 'end tell'], text=True).strip()


def menu(pid, group, action):
    gui(pid, 'set frontmost to true',
        f'click (first menu item of menu "{group}" of menu bar 1 whose name starts with "{action}")')


def save(pid):
    menu(pid, 'File', 'Save')
    poll(lambda: not gui(pid, 'get name of window 1').startswith('*'))


def capture(pid, name):
    if not args.capture:
        return
    query = ('ObjC.import("CoreGraphics"); '
             'var windows = ObjC.deepUnwrap(ObjC.castRefToObject($.CGWindowListCopyWindowInfo(1,0))); '
             f'windows.filter(w => w.kCGWindowOwnerPID == {pid} && w.kCGWindowLayer == 0)[0].kCGWindowNumber;')
    window = subprocess.check_output(['osascript', '-l', 'JavaScript', '-e', query], text=True).strip()
    subprocess.run(['screencapture', '-x', '-l', window, str(project.parent / (name + '.png'))], check=True)


def paper(page):
    return page.paper_format, page.orientation, page.width_nm, page.height_nm


def refused(action, text):
    try:
        action()
    except ApiError as error:
        assert text in str(error), str(error)
    else:
        raise AssertionError('Invalid page batch was accepted')


# PAGE_INFO rounds the built-in metric dimensions to native whole mils.
def native_nm(mm):
    return round(mm / .0254) * 25400


A4 = ('A4', 1, native_nm(297), native_nm(210))
A4_PORTRAIT = ('A4', 2, native_nm(210), native_nm(297))
A3 = ('A3', 1, native_nm(420), native_nm(297))

with editor() as (client, pid, selected):
    assert len(selected) == 7, 'Start from a fresh copy with root and six children'
    root = selected[0]
    initial = [paper(client.get_page_settings(doc)) for doc in selected]
    assert initial == [A4] * 7, initial
    sentinel = client.create_sheet('Unchanged', 'unchanged.kicad_sch', document=root)
    sentinel_doc = next(d for d in client.get_open_documents() if d.sheet_path.path[-1].value == sentinel.id.value)
    untouched = paper(client.get_page_settings(sentinel_doc))
    save(pid)
    files = {p: p.read_bytes() for p in project.glob('*.kicad_sch')}

    invalid = Document()
    invalid.CopyFrom(selected[1])
    invalid.sheet_path.path[-1].value = '00000000-0000-0000-0000-000000000001'
    refused(lambda: client.set_page_settings('A3', 'landscape', documents=[root, invalid]), 'same schematic hierarchy')
    refused(lambda: client.set_page_settings('nonexistent', 'landscape', documents=[root]), 'standard schematic paper')
    assert paper(client.get_page_settings(root)) == A4
    assert not gui(pid, 'get name of window 1').startswith('*'), 'Refusal marked the project dirty'

    # An off-screen change must neither move the viewport nor change the root.
    child = selected[1]
    assert client.set_page_settings('A4', 'portrait', documents=[child]).changed_screens == 1
    assert paper(client.get_page_settings(child)) == A4_PORTRAIT
    assert paper(client.get_page_settings(root)) == A4
    assert client.active_document() == root
    menu(pid, 'Edit', 'Undo')
    poll(lambda: paper(client.get_page_settings(child)) == A4)
    menu(pid, 'Edit', 'Redo')
    poll(lambda: paper(client.get_page_settings(child)) == A4_PORTRAIT)
    menu(pid, 'Edit', 'Undo')
    poll(lambda: paper(client.get_page_settings(child)) == A4)
    assert client.active_document() == root
    print('PASS: explicit read, rejected batch unchanged, off-screen portrait and Undo/Redo', flush=True)

    capture(pid, 'A4-before')
    result = client.set_page_settings('A3', 'landscape', documents=[*selected, root])
    assert result.changed_screens == len(result.pages) == 7
    assert all(paper(page) == A3 for page in result.pages)
    assert all(paper(client.get_page_settings(doc)) == A3 for doc in selected)
    assert paper(client.get_page_settings(sentinel_doc)) == untouched
    assert client.active_document() == root
    capture(pid, 'A3-after')
    assert client.set_page_settings('A3', 'landscape', documents=selected).changed_screens == 0
    menu(pid, 'Edit', 'Undo')
    poll(lambda: all(paper(client.get_page_settings(doc)) == A4 for doc in selected))
    menu(pid, 'Edit', 'Redo')
    poll(lambda: all(paper(client.get_page_settings(doc)) == A3 for doc in selected))
    assert all(p.read_bytes() == data for p, data in files.items()), 'IPC must not save the files'
    print('PASS: seven selected sheets, no-op creates no Undo entry, one Undo/Redo restores all', flush=True)

    save(pid)
    for path, before in files.items():
        after = path.read_bytes()
        if path.name == 'unchanged.kicad_sch':
            assert after == before, 'Unselected sheet changed'
        else:
            assert re.sub(rb'\(paper "A4"\)', b'(paper "A3")', before, count=1) == after, path.name
    print('PASS: save changes only paper in the seven selected files; title blocks preserved', flush=True)

with editor() as (client, pid, documents):
    root = documents[0]
    assert all(paper(client.get_page_settings(doc)) == A3 for doc in selected)
    assert paper(client.get_page_settings(sentinel_doc)) == untouched
    assert len(documents) == 8
    print('PASS: A3 settings persisted after closing and reopening', flush=True)

    original = next(sheet for sheet in client.get_sheets(root) if sheet.filename.startswith('01_'))
    original_doc = next(doc for doc in documents if doc.sheet_path.path[-1].value == original.id.value)
    alias = client.create_sheet('Shared instance', original.filename, document=root, link_existing=True)
    alias_doc = next(doc for doc in client.get_open_documents() if doc.sheet_path.path[-1].value == alias.id.value)
    refused(lambda: client.set_page_settings('A4', 'portrait', documents=[original_doc]), 'every loaded instance')
    assert paper(client.get_page_settings(original_doc)) == paper(client.get_page_settings(alias_doc)) == A3
    result = client.set_page_settings('A4', 'portrait', documents=[original_doc, alias_doc])
    assert result.changed_screens == 1 and len(result.pages) == 2
    assert all(paper(page) == A4_PORTRAIT for page in result.pages)
    menu(pid, 'Edit', 'Undo')
    poll(lambda: all(paper(client.get_page_settings(doc)) == A3 for doc in [original_doc, alias_doc]))
    print('PASS: shared file requires every instance; changes once and undoes together', flush=True)
