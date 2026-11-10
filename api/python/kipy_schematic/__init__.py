"""Schematic IPC client for this KiCad fork, using its exact protobuf schema.

The public kipy transport is reused; the unfinished KiCad 11 kipy.schematic
wrapper is deliberately not imported. DescriptorPool isolation allows this
client and the installed kipy PCB API to coexist without schema collisions.
"""
import os
from contextlib import contextmanager
from pathlib import Path
from uuid import uuid4

from google.protobuf import descriptor_pb2, descriptor_pool, message_factory
from kipy.client import KiCadClient


def _load_protocol():
    descriptors = descriptor_pb2.FileDescriptorSet.FromString(
        Path(__file__).with_name('_protocol.pb').read_bytes())
    pool = descriptor_pool.DescriptorPool()
    pending = list(descriptors.file)
    while pending:
        added = []
        for descriptor in pending:
            try:
                pool.Add(descriptor)
            except TypeError:
                continue
            added.append(descriptor)
        if not added:
            raise ImportError('Schematic client contains an incomplete protobuf descriptor set')
        pending = [descriptor for descriptor in pending if descriptor not in added]
    return pool


_pool = _load_protocol()


def _message(name):
    return message_factory.GetMessageClass(_pool.FindMessageTypeByName(name))


Document = _message('kiapi.common.types.DocumentSpecifier')
Sheet = _message('kiapi.schematic.types.Sheet')
SheetPin = _message('kiapi.schematic.types.SheetPin')


class SchematicClient:
    """Coordinates in Sheet and SheetPin messages are integer nanometers."""
    def __init__(self, socket_path=None, token=None, timeout_ms=5000, *, transport=None):
        self._transport = transport or KiCadClient(
            socket_path or os.environ.get('KICAD_API_SOCKET', 'ipc:///tmp/kicad/api.sock'),
            'schematic-' + uuid4().hex,
            token if token is not None else os.environ.get('KICAD_API_TOKEN', ''), timeout_ms)
        self._in_commit = False

    def _send(self, command, response):
        return self._transport.send(command, _message(response))

    def get_open_documents(self, document_type=1):
        request = _message('kiapi.common.commands.GetOpenDocuments')(type=document_type)
        response = self._send(request, 'kiapi.common.commands.GetOpenDocumentsResponse')
        return list(response.documents)

    def active_document(self):
        documents = self.get_open_documents()
        if not documents:
            raise RuntimeError('No schematic editor is open')
        projects = {(doc.project.name, doc.project.path) for doc in documents}
        if len(projects) != 1:
            raise RuntimeError('Several schematic projects are open; select a document explicitly')
        return documents[0]

    def get_sheets(self, document=None):
        return self._get_items(document, 37, Sheet)

    def get_sheet_pins(self, document=None, sheet_id=None):
        return self._get_items(document, 36, SheetPin, sheet_id)

    def _get_items(self, document, item_type, item_class, container=None):
        request = _message('kiapi.common.commands.GetItems')()
        request.header.document.CopyFrom(document if document is not None else self.active_document())
        if container:
            request.header.container.value = container
        request.types.append(item_type)
        response = self._send(request, 'kiapi.common.commands.GetItemsResponse')
        if response.status != 1:
            raise RuntimeError(f'Schematic GetItems failed: status {response.status}')
        items = []
        for packed in response.items:
            item = item_class()
            if not packed.Unpack(item):
                raise RuntimeError(f'Unexpected schematic item type: {packed.type_url}')
            items.append(item)
        return items

    def _connectivity_query(self, command, *, document=None, response='NetQueryResponse', **fields):
        request = _message('kiapi.schematic.commands.' + command)(**fields)
        request.document.CopyFrom(document if document is not None else self.active_document())
        return self._send(request, 'kiapi.schematic.commands.' + response)

    def get_schematic_netlist(self, document=None):
        """Full live hierarchy. Net IDs are scoped to snapshot.session_id/revision."""
        return self._connectivity_query('GetSchematicNetlist', document=document).snapshot

    def create_sheet(self, name, filename, *, document=None,
                     position_nm=(0, 0), size_nm=(20000000, 10000000), pins=(),
                     link_existing=False):
        """Create a child file, or attach an existing one without modifying its contents.

        pins contains SheetPin messages; their positions are absolute on the parent
        page. side: left=1/right=2/top=3/bottom=4; shape: input=1/output=2/
        bidirectional=3/tristate=4/passive=5. Matching child labels and parent
        wires remain separate schematic objects, just as in the GUI.
        """
        request = _message('kiapi.schematic.commands.CreateSheet')()
        request.header.document.CopyFrom(document if document is not None else self.active_document())
        request.link_existing = link_existing
        request.sheet.name = name
        request.sheet.filename = filename
        request.sheet.position.x_nm, request.sheet.position.y_nm = position_nm
        request.sheet.size.x_nm, request.sheet.size.y_nm = size_nm
        request.sheet.pins.extend(pins)
        response = self._send(request, 'kiapi.schematic.commands.CreateSheetResponse')
        return response.sheet

    @contextmanager
    def commit(self, message='Create schematic sheets via Python', document=None):
        """Group changes in one Undo step; on error drop changes, retaining child files."""
        if self._in_commit:
            raise RuntimeError('Nested schematic commits are not supported')
        document = document if document is not None else self.active_document()
        request = _message('kiapi.common.commands.BeginCommit')()
        request.header.document.CopyFrom(document)
        response = self._send(request, 'kiapi.common.commands.BeginCommitResponse')
        end = _message('kiapi.common.commands.EndCommit')()
        end.header.CopyFrom(request.header)
        end.id.CopyFrom(response.id)
        end.message = message
        self._in_commit = True
        try:
            yield self
        except BaseException:
            end.action = _pool.FindEnumTypeByName('kiapi.common.commands.CommitAction').values_by_name['CMA_DROP'].number
            self._send(end, 'kiapi.common.commands.EndCommitResponse')
            raise
        else:
            end.action = _pool.FindEnumTypeByName('kiapi.common.commands.CommitAction').values_by_name['CMA_COMMIT'].number
            self._send(end, 'kiapi.common.commands.EndCommitResponse')
        finally:
            self._in_commit = False
