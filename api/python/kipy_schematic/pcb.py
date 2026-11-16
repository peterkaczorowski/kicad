"""PCB operations using the exact schema of this KiCad fork."""
from uuid import uuid4
from . import SchematicClient, _message, _pool

Track = _message('kiapi.board.types.Track')
Via = _message('kiapi.board.types.Via')
Footprint = _message('kiapi.board.types.FootprintInstance')
Pad = _message('kiapi.board.types.Pad')
BoardGraphic = _message('kiapi.board.types.BoardGraphicShape')


def layer_id(layer):
    if isinstance(layer, int):
        value = layer
    else:
        value = _pool.FindEnumTypeByName('kiapi.board.types.BoardLayer').values_by_name[
            'BL_' + layer.replace('.', '_')].number
    if value <= 0:
        raise ValueError('An explicit board layer is required')
    return value


class PCBClient(SchematicClient):
    """Live board commands; use explicit documents when several boards are open."""
    def get_open_documents(self, document_type=3):
        return super().get_open_documents(document_type)

    def active_document(self):
        docs = self.get_open_documents()
        if len(docs) != 1:
            raise RuntimeError('Select an explicit PCB document; expected exactly one open board')
        return docs[0]

    def get_items(self, types=None, document=None, *, ids=None):
        # PCB's native GetItems requires explicit types. Default to top-level
        # containers/drawings, so deleting a returned footprint never also
        # submits its child pads/fields for deletion in the same batch.
        if types is None:
            types = [1, 3, 4, 6, 7, 8, 9, 11, 12, 13, 14, 15, 16, 17, 52, 56]
        return super().get_items(types, document, ids=ids)

    def _edit_pcb(self, command, items, document=None, *, fields=None,
                  create_net_if_missing=False, allow_net_mismatch=False, container_id=None):
        request = _message('kiapi.common.commands.' + command)()
        request.header.document.CopyFrom(document if document is not None else self.active_document())
        request.header.create_net_if_missing = create_net_if_missing
        request.header.allow_net_mismatch = allow_net_mismatch
        if container_id:
            request.header.container.value = container_id
        if fields:
            request.header.field_mask.paths.extend(fields)
        for item in items:
            request.items.add().Pack(item)
        reply = self._send(request, 'kiapi.common.commands.' + command + 'Response')
        if reply.status != 1:
            raise RuntimeError(f'{command}: request status {reply.status}')
        self.last_affected_documents = list(reply.affected_documents)
        entries = reply.created_items if command == 'CreateItems' else reply.updated_items
        for entry in entries:
            if entry.status.code != 1:
                raise RuntimeError(entry.status.error_message)
        return [self._unpack_item(entry.item) for entry in entries]

    def create_items(self, items, document=None, **options):
        return self._edit_pcb('CreateItems', items, document, **options)

    def update_items(self, items, document=None, **options):
        return self._edit_pcb('UpdateItems', items, document, **options)

    def add_track(self, start, end, width_nm, layer='F.Cu', document=None, *,
                  net='', create_net_if_missing=False):
        if width_nm <= 0 or start == end:
            raise ValueError('A track needs positive width and distinct endpoints')
        item = Track(layer=layer_id(layer))
        item.id.value = str(uuid4())
        item.start.x_nm, item.start.y_nm = start
        item.end.x_nm, item.end.y_nm = end
        item.width.value_nm = width_nm
        item.net.name = net
        return self.create_items([item], document,
                                 create_net_if_missing=create_net_if_missing)[0]
