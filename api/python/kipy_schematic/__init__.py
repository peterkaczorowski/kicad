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
Graphic = _message('kiapi.schematic.types.Graphic')
GraphicShape = _message('kiapi.common.types.GraphicShape')
Line = _message('kiapi.schematic.types.Line')
Symbol = _message('kiapi.schematic.types.Symbol')
SymbolField = _message('kiapi.schematic.types.SymbolField')
LibraryPin = _message('kiapi.schematic.types.LibraryPin')
LibraryGraphic = _message('kiapi.schematic.types.LibraryGraphic')
LibrarySymbolDefinition = _message('kiapi.schematic.types.LibrarySymbolDefinition')
Junction = _message('kiapi.schematic.types.Junction')
NoConnect = _message('kiapi.schematic.types.NoConnect')
BusEntry = _message('kiapi.schematic.types.BusEntry')
LocalLabel = _message('kiapi.schematic.types.LocalLabel')
GlobalLabel = _message('kiapi.schematic.types.GlobalLabel')
HierarchicalLabel = _message('kiapi.schematic.types.HierarchicalLabel')
DirectiveLabel = _message('kiapi.schematic.types.DirectiveLabel')
Text = _message('kiapi.schematic.types.Text')


class SchematicClient:
    """Live schematic, connectivity and library commands; dimensions are nanometers."""
    def __init__(self, socket_path=None, token=None, timeout_ms=5000, *, transport=None):
        self._transport = transport or KiCadClient(
            socket_path or os.environ.get('KICAD_API_SOCKET', 'ipc:///tmp/kicad/api.sock'),
            'schematic-' + uuid4().hex,
            token if token is not None else os.environ.get('KICAD_API_TOKEN', ''), timeout_ms)
        self._in_commit = False
        self.last_affected_documents = []

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

    def _document_command(self, name, document, **fields):
        if self._in_commit:
            raise RuntimeError('Finish commit() before running document commands')
        request = _message('kiapi.common.commands.' + name)(**fields)
        request.document.CopyFrom(document)
        return request

    def revert_document(self, document, *, discard_unsaved=False):
        """Reload from disk; refuse unsaved changes unless explicitly discarded.

        Schematics reload the entire hierarchy and restore the active sheet UUID
        path. Other editors restore zoom/center. Library editors reload the open
        library item, preserving modifications to other buffered symbols.
        """
        request = self._document_command('RevertDocument', document,
                                         discard_unsaved=discard_unsaved)
        self._send(request, 'google.protobuf.Empty')

    def save_document(self, document):
        """Save the live document. Schematics save all loaded hierarchy screens."""
        request = self._document_command('SaveDocument', document)
        self._send(request, 'google.protobuf.Empty')

    def save_copy_of_document(self, document, path, *, overwrite=False, include_project=False):
        """Export without changing the active path or clearing unsaved changes.

        Schematic copies contain the selected sheet only (child file links stay
        unchanged). Symbol copies are self-contained single-symbol libraries;
        footprints and drawing sheets are single files. include_project is
        supported for PCB copies only. Overwriting needs explicit permission.
        """
        request = self._document_command('SaveCopyOfDocument', document, path=os.fspath(path))
        request.options.overwrite = overwrite
        request.options.include_project = include_project
        self._send(request, 'google.protobuf.Empty')

    def run_action(self, document, action):
        """Run an editor action by name; return RunActionStatus (OK=1, invalid=2).

        Action names are unstable. Revert/save actions use the safe document
        handlers, so RunAction cannot bypass the dirty-revert policy.
        """
        request = self._document_command('RunAction', document, action=action)
        return self._send(request, 'kiapi.common.commands.RunActionResponse').status

    @staticmethod
    def _unpack_item(packed):
        item = _message(packed.type_url.rsplit('/', 1)[-1])()
        if not packed.Unpack(item):
            raise RuntimeError('Malformed schematic item: ' + packed.type_url)
        return item

    def get_items(self, types=None, document=None, *, ids=None):
        """Read supported live items; types are KiCadObjectType numbers, ids optional."""
        name = 'GetItemsById' if ids is not None else 'GetItems'
        request = _message('kiapi.common.commands.' + name)()
        request.header.document.CopyFrom(document if document is not None else self.active_document())
        if ids is not None:
            for value in ids:
                request.items.add(value=str(value))
        elif types is not None:
            request.types.extend(types)
        result = self._send(request, 'kiapi.common.commands.GetItemsResponse')
        if result.status != 1:
            raise RuntimeError(f'{name} failed: status {result.status}')
        return [self._unpack_item(item) for item in result.items]

    def _edit_items(self, command, items, document, fields=None):
        request = _message('kiapi.common.commands.' + command)()
        request.header.document.CopyFrom(document if document is not None else self.active_document())
        if fields:
            request.header.field_mask.paths.extend(fields)
        for item in items:
            request.items.add().Pack(item)
        result = self._send(request, 'kiapi.common.commands.' + command + 'Response')
        if result.status != 1:
            raise RuntimeError(f'{command} failed: status {result.status}')
        self.last_affected_documents = list(result.affected_documents)
        entries = result.created_items if command == 'CreateItems' else result.updated_items
        for entry in entries:
            if entry.status.code != 1:
                raise RuntimeError(f'{command}: {entry.status.error_message} (status {entry.status.code})')
        return [self._unpack_item(entry.item) for entry in entries]

    def create_items(self, items, document=None):
        """Create a batch in the current commit, or one independent Undo step."""
        return self._edit_items('CreateItems', items, document)

    def update_items(self, items, document=None, *, fields=None):
        """Patch only named protobuf fields; omit fields for a full supported update."""
        return self._edit_items('UpdateItems', items, document, fields)

    def delete_items(self, ids, document=None):
        request = _message('kiapi.common.commands.DeleteItems')()
        request.header.document.CopyFrom(document if document is not None else self.active_document())
        for value in ids:
            request.item_ids.add(value=str(value))
        result = self._send(request, 'kiapi.common.commands.DeleteItemsResponse')
        if result.status != 1 or any(item.status != 1 for item in result.deleted_items):
            raise RuntimeError('DeleteItems rejected one or more IDs')
        self.last_affected_documents = list(result.affected_documents)
        return result

    @staticmethod
    def _set_lib_id(target, value):
        nickname, separator, name = value.partition(':')
        if not separator or not nickname or not name:
            raise ValueError('lib_id must be library:entry')
        target.library_nickname, target.entry_name = nickname, name

    def _library_request(self, name, document):
        request = _message('kiapi.schematic.commands.' + name)()
        request.document.CopyFrom(document if document is not None else self.active_document())
        return request

    def search_symbol_libraries(self, query='', document=None, *, library='', limit=100, offset=0):
        """Return matches, total_matches and explicit per-library errors (never hide errors)."""
        request = self._library_request('SearchSymbolLibraries', document)
        request.query, request.library_nickname = query, library
        request.limit, request.offset = limit, offset
        return self._send(request, 'kiapi.schematic.commands.SearchSymbolLibrariesResponse')

    def get_symbol_definition(self, lib_id=None, document=None, *, symbol_id=None):
        """Read a library definition or exactly the cached definition of a live instance."""
        if (lib_id is None) == (symbol_id is None):
            raise ValueError('Select exactly one of lib_id and symbol_id')
        request = self._library_request('GetSymbolDefinition', document)
        if symbol_id is not None:
            request.symbol_id.value = str(symbol_id)
        else:
            self._set_lib_id(request.lib_id, lib_id)
        return self._send(request, 'kiapi.schematic.commands.SymbolDefinitionResponse')

    def create_symbol_library(self, nickname, path, document=None, *, link_existing=False):
        """Create/register a project library; global libraries are never implicitly modified."""
        request = self._library_request('CreateSymbolLibrary', document)
        request.nickname, request.path = nickname, os.fspath(path)
        request.link_existing = link_existing
        return self._send(request, 'kiapi.schematic.commands.CreateSymbolLibraryResponse').library

    def save_symbol_definition(self, definition, document=None, *, overwrite=False):
        request = self._library_request('SaveSymbolDefinition', document)
        request.definition.CopyFrom(definition)
        request.overwrite = overwrite
        return self._send(request, 'kiapi.schematic.commands.SymbolDefinitionResponse')

    def recover_schematic_symbol(self, symbol_id, target_lib_id, document=None, *, open_in_editor=False):
        """Persist a detached embedded definition; never relink or modify the source instance."""
        request = self._library_request('RecoverSchematicSymbol', document)
        request.symbol_id.value = str(symbol_id)
        self._set_lib_id(request.target_lib_id, target_lib_id)
        request.open_in_editor = open_in_editor
        return self._send(request, 'kiapi.schematic.commands.SymbolDefinitionResponse')

    def open_library_symbol(self, lib_id, document=None):
        request = self._library_request('OpenLibrarySymbol', document)
        self._set_lib_id(request.lib_id, lib_id)
        return self._send(request, 'kiapi.schematic.commands.OpenLibrarySymbolResponse').document

    def add_symbol(self, lib_id, position, document=None, *, reference='', unit=1,
                   body_style=1, rotation_degrees=0, mirror_x=False, mirror_y=False,
                   fields=None, dnp=None, in_bom=None, on_board=None, id=None):
        item = Symbol(reference=reference or (fields or {}).get("Reference", ""), unit=unit, body_style=body_style,
                      rotation_degrees=rotation_degrees, mirror_x=mirror_x, mirror_y=mirror_y)
        nickname, separator, name = lib_id.partition(':')
        if not separator or not nickname or not name:
            raise ValueError('lib_id must be library:entry')
        item.lib_id.library_nickname, item.lib_id.entry_name = nickname, name
        item.id.value = str(id or uuid4())
        item.position.x_nm, item.position.y_nm = position
        for name, value in (fields or {}).items():
            field = item.fields.add(name=name)
            field.text.text = str(value)
            field.text.position.CopyFrom(item.position)
            field.visible = name in ('Reference', 'Value')
        for name, value in (('dnp', dnp), ('in_bom', in_bom), ('on_board', on_board)):
            if value is not None:
                setattr(item, name, value)
        return self.create_items([item], document)[0]

    def pin_position(self, reference, number, document=None):
        matches = [pin for symbol in self.get_items([35], document)
                   if symbol.reference == reference for pin in symbol.pins if pin.number == str(number)]
        positions = {(pin.position.x_nm, pin.position.y_nm) for pin in matches}
        if len(positions) != 1:
            raise ValueError(f'Pin {reference}.{number} is missing or ambiguous on the selected sheet')
        return positions.pop()

    def _wire_endpoint(self, endpoint, document):
        if len(endpoint) == 2 and isinstance(endpoint[0], str):
            return self.pin_position(endpoint[0], endpoint[1], document)
        return endpoint

    def add_junction(self, position, document=None, *, diameter_nm=0, id=None):
        item = Junction(diameter_nm=diameter_nm)
        item.id.value = str(id or uuid4())
        item.position.x_nm, item.position.y_nm = position
        return self.create_items([item], document)[0]

    def add_no_connect(self, position, document=None, *, id=None):
        item = NoConnect()
        item.id.value = str(id or uuid4())
        item.position.x_nm, item.position.y_nm = position
        return self.create_items([item], document)[0]

    def add_bus_entry(self, position, size, document=None, *, bus_to_bus=False, id=None):
        item = BusEntry(bus_to_bus=bus_to_bus)
        item.id.value = str(id or uuid4())
        item.position.x_nm, item.position.y_nm = position
        item.size.x_nm, item.size.y_nm = size
        return self.create_items([item], document)[0]

    def add_label(self, text, position, document=None, *, kind='local',
                  rotation_degrees=0, shape=0, properties=None, id=None):
        """Create local/global/hierarchical/directive label at an electrical anchor in nm.

        Spin rotations 0/90/180/270 correspond to native left/up/right/bottom.
        Shapes: input/output/bidirectional/tristate/passive = 0..4; directive
        dot/circle/diamond/rectangle = 0..3. Properties include net-class fields.
        """
        classes = {'local': LocalLabel, 'global': GlobalLabel,
                   'hierarchical': HierarchicalLabel, 'directive': DirectiveLabel}
        if kind not in classes or rotation_degrees % 90:
            raise ValueError('Unknown label kind or non-quarter-turn rotation')
        label = classes[kind](spin=(rotation_degrees // 90) % 4, shape=shape)
        label.id.value = str(id or uuid4())
        label.position.x_nm, label.position.y_nm = position
        label.text.text.text = text
        if properties:
            label.properties.update(properties)
        return self.create_items([label], document)[0]

    def add_graphic(self, drawing, document=None, *, id=None):
        item = Graphic()
        item.id.value = str(id or uuid4())
        item.drawing.CopyFrom(drawing)
        return self.create_items([item], document)[0]

    def add_shape(self, shape, document=None, *, fill_mode=0, id=None):
        drawing = LibraryGraphic(fill_mode=fill_mode)
        drawing.shape.CopyFrom(shape)
        return self.add_graphic(drawing, document, id=id)

    def add_text_box(self, text, top_left, bottom_right, document=None, *,
                     size_nm=1270000, margin_nm=0, border=True, id=None):
        drawing = LibraryGraphic()
        box = drawing.text_box
        box.text, box.border_enabled = text, border
        box.top_left.x_nm, box.top_left.y_nm = top_left
        box.bottom_right.x_nm, box.bottom_right.y_nm = bottom_right
        box.attributes.size.x_nm = box.attributes.size.y_nm = size_nm
        for edge in ('left', 'top', 'right', 'bottom'):
            getattr(box, 'margin_' + edge).value_nm = margin_nm
        return self.add_graphic(drawing, document, id=id)

    def add_text(self, text, position, document=None, *, size_nm=1270000, id=None):
        item = Text()
        item.id.value = str(id or uuid4())
        item.text.text = text
        item.text.position.x_nm, item.text.position.y_nm = position
        item.text.attributes.size.x_nm = item.text.attributes.size.y_nm = size_nm
        return self.create_items([item], document)[0]

    def add_wire(self, start, end, document=None, *, bus=False, graphic=False, id=None):
        """Wire/bus endpoints in nm; zero-length segments and invalid layers are rejected."""
        line = Line(layer=3 if graphic else 2 if bus else 1)
        line.id.value = str(id or uuid4())
        line.start.x_nm, line.start.y_nm = self._wire_endpoint(start, document)
        line.end.x_nm, line.end.y_nm = self._wire_endpoint(end, document)
        return self.create_items([line], document)[0]

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

    def get_pin_net(self, reference='', pin_number='', *, pin_id=None, document=None):
        fields = dict(reference=reference, pin_number=str(pin_number))
        if pin_id is not None:
            fields['pin_id'] = {'value': pin_id}
        return self._connectivity_query('GetPinNet', document=document, **fields).snapshot

    def get_net_members(self, net_id='', *, name='', snapshot=None, document=None):
        """Pass the originating snapshot to reject stale results explicitly."""
        fields = dict(net_id=net_id, name=name)
        if snapshot is not None:
            fields.update(session_id=snapshot.session_id, revision=snapshot.revision)
            if document is None:
                document = snapshot.document
        return self._connectivity_query('GetNetMembers', document=document, **fields).snapshot

    def get_item_net(self, item_ids=(), *, use_selection=False, document=None):
        """A symbol or multi-selection returns all its nets; buses include leaf nets."""
        if isinstance(item_ids, str):
            item_ids = [item_ids]
        return self._connectivity_query('GetItemNet', document=document,
            items=[{'value': item_id} for item_id in item_ids], use_selection=use_selection).snapshot

    def get_symbol_connections(self, reference, document=None):
        """Pin table for all placed units, including intentional NC pins."""
        return self._connectivity_query('GetSymbolConnections', document=document,
            response='GetSymbolConnectionsResponse', reference=reference)

    def highlight_net(self, net_id='', *, name='', clear=False, snapshot=None, document=None):
        fields = dict(net_id=net_id, name=name, clear=clear)
        if snapshot is not None:
            fields.update(session_id=snapshot.session_id, revision=snapshot.revision)
            if document is None:
                document = snapshot.document
        return self._connectivity_query('HighlightNet', document=document,
            response='HighlightNetResponse', **fields).snapshot

    def get_connectivity_issues(self, document=None):
        """Read-only connectivity diagnostics, without adding ERC marker objects."""
        return self._connectivity_query('GetConnectivityIssues', document=document,
            response='GetConnectivityIssuesResponse')

    def plan_net_rename(self, net_id, new_name, *, snapshot, document=None):
        """Preview literal labels/ports throughout the hierarchy; never merge nets.

        Inspect can_apply, blockers, collisions and changes before rename_net().
        All naming labels/ports must have one identical literal text. Buses,
        bus members, power sources, variables and heterogeneous aliases are refused.
        """
        if document is None:
            document = snapshot.document
        return self._connectivity_query('PlanNetRename', document=document,
            response='PlanNetRenameResponse', net_id=net_id, new_name=new_name,
            session_id=snapshot.session_id, revision=snapshot.revision).plan

    def rename_net(self, plan):
        """Apply this client's current server-issued plan as one Undo/Redo step.

        Rejects any existing target name. No merge override is supported.
        Schematics remain unsaved; this operation cannot be nested in commit().
        """
        if self._in_commit:
            raise RuntimeError('RenameNet creates an independent Undo step; finish commit() first')
        if not plan.can_apply or not plan.id:
            raise ValueError('Rename refused: ' + '; '.join(plan.blockers))
        return self._connectivity_query('RenameNet', document=plan.document,
            response='RenameNetResponse', plan_id=plan.id)

    def get_page_settings(self, document):
        """Read paper format, orientation and dimensions of an explicit sheet."""
        request = _message('kiapi.schematic.commands.GetPageSettings')()
        request.document.CopyFrom(document)
        return self._send(request, 'kiapi.schematic.commands.GetPageSettingsResponse').settings

    def set_page_settings(self, paper_format, orientation, *, documents):
        """Set standard paper on a list of sheets in one independent Undo step.

        Orientation is 'landscape' or 'portrait'. Include every loaded instance
        of a changed shared file. This changes the live editor without saving.
        """
        if self._in_commit:
            raise RuntimeError('Page changes create an independent Undo step; finish commit() first')
        orientations = {'landscape': 1, 'portrait': 2}
        if not isinstance(orientation, str) or orientation.lower() not in orientations:
            raise ValueError("orientation must be 'landscape' or 'portrait'")
        request = _message('kiapi.schematic.commands.SetPageSettings')(
            paper_format=paper_format, orientation=orientations[orientation.lower()])
        request.documents.extend(documents)
        if not request.documents:
            raise ValueError('At least one explicit sheet document is required')
        return self._send(request, 'kiapi.schematic.commands.SetPageSettingsResponse')

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


from .pcb import PCBClient  # matched PCB protocol, isolated from upstream descriptors
