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
    if value <= 2:
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

    def place_footprint(self, lib_id, position=None, document=None, *, rotation_degrees=None,
                        side=None, reference=None, value=None, replace_id=None, override_locks=False):
        request = _message('kiapi.board.commands.PlaceFootprint')()
        request.header.document.CopyFrom(document if document is not None else self.active_document())
        self._set_lib_id(request.lib_id, lib_id)
        if position is not None:
            request.position.x_nm, request.position.y_nm = position
        if rotation_degrees is not None:
            request.rotation_degrees = rotation_degrees
        if side is not None:
            request.side = layer_id(side)
        if reference is not None:
            request.reference = reference
        if value is not None:
            request.value = value
        if replace_id is not None:
            request.replace_id.value = str(replace_id)
        request.override_locks = override_locks
        return self._send(request, 'kiapi.board.commands.FootprintPlacementResponse')

    def get_footprint_pads(self, reference=None, document=None, *, footprint_id=None):
        if (reference is None) == (footprint_id is None):
            raise ValueError('Choose exactly one footprint reference or UUID')
        request = _message('kiapi.board.commands.GetFootprintPads')()
        request.document.CopyFrom(document if document is not None else self.active_document())
        if footprint_id is not None:
            request.footprint_id.value = str(footprint_id)
        else:
            request.reference = str(reference)
        return list(self._send(request, 'kiapi.board.commands.FootprintPlacementResponse').pads)

    def get_pad(self, reference, number, document=None):
        pads = [p for p in self.get_footprint_pads(reference, document) if p.number == str(number)]
        if len(pads) != 1:
            raise ValueError(f'Pad {reference}.{number} is missing or ambiguous; select an explicit pad UUID')
        return pads[0]

    def get_ratsnest(self, net_name='', document=None, *, pad_id=None):
        request = _message('kiapi.board.commands.GetRatsnest')(net_name=net_name)
        request.document.CopyFrom(document if document is not None else self.active_document())
        if pad_id is not None:
            request.pad_id.value = str(pad_id)
        return list(self._send(request, 'kiapi.board.commands.RatsnestResponse').edges)

    def get_nets(self, document=None):
        request = _message('kiapi.board.commands.GetNets')()
        request.board.CopyFrom(document if document is not None else self.active_document())
        return list(self._send(request, 'kiapi.board.commands.NetsResponse').nets)

    def get_connected_items(self, ids, document=None, *, types=()):
        request = _message('kiapi.board.commands.GetConnectedItems')()
        request.header.document.CopyFrom(document if document is not None else self.active_document())
        for value in ids:
            request.items.add(value=str(value))
        request.types.extend(types)
        response = self._send(request, 'kiapi.common.commands.GetItemsResponse')
        if response.status != 1:
            raise RuntimeError('GetConnectedItems failed')
        return [self._unpack_item(item) for item in response.items]

    def add_track(self, start=None, end=None, width_nm=250000, layer='F.Cu', document=None, *,
                  net=None, from_pad=None, to_pad=None, create_net_if_missing=False):
        first = self.get_pad(*from_pad, document) if from_pad is not None else None
        last = self.get_pad(*to_pad, document) if to_pad is not None else None
        for pad in (first, last):
            if pad is not None:
                if not pad.net.name:
                    raise ValueError('Pad has no net; update PCB from schematic first')
                if net is not None and net != pad.net.name:
                    raise ValueError('Track endpoints must share a net')
                net = pad.net.name
                if layer_id(layer) not in pad.copper_layers:
                    raise ValueError('Track layer does not intersect the selected pad')
        if first is not None:
            start = (first.position.x_nm, first.position.y_nm)
        if last is not None:
            end = (last.position.x_nm, last.position.y_nm)
        if start is None or end is None or width_nm <= 0 or start == end:
            raise ValueError('A track needs positive width and distinct endpoints')
        item = Track(layer=layer_id(layer))
        item.id.value = str(uuid4())
        item.start.x_nm, item.start.y_nm = start
        item.end.x_nm, item.end.y_nm = end
        item.width.value_nm = width_nm
        item.net.name = net or ''
        return self.create_items([item], document,
                                 create_net_if_missing=create_net_if_missing)[0]

    def add_via(self, position, net, document=None, *, diameter_nm=800000, drill_nm=400000,
                create_net_if_missing=False):
        if not 0 < drill_nm < diameter_nm:
            raise ValueError('Via needs 0 < drill < copper diameter')
        item = Via(type=1)
        item.id.value = str(uuid4())
        item.position.x_nm, item.position.y_nm = position
        item.net.name = net
        stack = item.pad_stack
        stack.type = 1  # PST_NORMAL
        stack.layers.extend([layer_id('F.Cu'), layer_id('B.Cu')])
        stack.drill.start_layer, stack.drill.end_layer = layer_id('F.Cu'), layer_id('B.Cu')
        stack.drill.diameter.x_nm = stack.drill.diameter.y_nm = drill_nm
        stack.drill.shape = 1  # DS_CIRCLE
        copper = stack.copper_layers.add(layer=layer_id('F.Cu'), shape=1)  # PSS_CIRCLE
        copper.size.x_nm = copper.size.y_nm = diameter_nm
        copper.custom_anchor_shape = 2  # native default rectangular custom anchor (unused for circle)
        return self.create_items([item], document, create_net_if_missing=create_net_if_missing)[0]
