"""SiI8784 native-library → live schematic → routed NE555 PCB acceptance, marked fixture only."""
from pathlib import Path
import argparse
import json
from google.protobuf.json_format import MessageToDict
from kipy_schematic import SchematicClient, PCBClient, LibrarySymbolDefinition, _pool
from kipy_schematic.pcb import Footprint, layer_id


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--project', type=Path, required=True)
    p.add_argument('--socket', required=True)
    p.add_argument('--schematic-socket', required=True)
    p.add_argument('--artifacts', type=Path, required=True)
    p.add_argument('--footprint-definition', type=Path, required=True)
    p.add_argument('--symbol-definition', type=Path, required=True)
    p.add_argument('--footprint-library', default='SiiFP')
    p.add_argument('--symbol-library', default='SiiSymbols')
    a = p.parse_args()
    project = a.project.resolve(); out = a.artifacts.resolve()
    assert (project / '.ipc-test-project').is_file()
    assert project.name not in ('CodexTest2', 'Codex800XL')
    out.mkdir(parents=True, exist_ok=True)
    pcb = PCBClient(socket_path=a.socket, timeout_ms=60000)
    sch = SchematicClient(socket_path=a.schematic_socket, timeout_ms=60000)
    bd, sd = pcb.active_document(), sch.active_document()
    assert Path(bd.project.path).resolve() == Path(sd.project.path).resolve() == project
    def kind(name):
        return _pool.FindEnumTypeByName('kiapi.common.types.KiCadObjectType').values_by_name['KOT_PCB_'+name].number
    # Link native fixture libraries through IPC, preserving the extension's prior entries.
    for library in sorted((project / 'footprints').glob('*.pretty')):
        pcb.create_footprint_library(library.stem, library, bd, link_existing=True)
    footprint = Footprint.FromString(a.footprint_definition.read_bytes())
    pcb.create_footprint_library(a.footprint_library, a.footprint_library+'.pretty', bd)
    footprint.definition.id.library_nickname = a.footprint_library
    footprint_id = a.footprint_library + ':SII8784CNUC'
    pads = [pcb._unpack_item(v) for v in footprint.definition.items if v.type_url.endswith('.Pad')]
    assert {p.number for p in pads} == set(map(str, range(1, 90)))
    ep = next(p for p in pads if p.number == '89')
    assert ep.pad_stack.copper_layers[0].size.x_nm == ep.pad_stack.copper_layers[0].size.y_nm == 5600000
    assert layer_id('F.Paste') not in ep.pad_stack.layers
    perimeter = [p for p in pads if p.number != '89']
    assert len(perimeter) == 88
    saved = pcb.save_footprint_definition(footprint, bd).definition
    (out / 'sii-footprint-native-readback.pb').write_bytes(saved.SerializeToString())
    after_pads = [pcb._unpack_item(v) for v in saved.definition.items if v.type_url.endswith('.Pad')]
    assert len(after_pads) == 89
    # Native save may canonicalize padstack defaults and UUID provenance; compare physical data.
    for source, native in zip(sorted(pads, key=lambda p: int(p.number)),
                              sorted(after_pads, key=lambda p: int(p.number))):
        assert source.number == native.number and source.position == native.position
        assert source.pad_stack.layers == native.pad_stack.layers
        assert source.pad_stack.copper_layers[0].size == native.pad_stack.copper_layers[0].size
        assert source.pad_stack.copper_layers[0].shape == native.pad_stack.copper_layers[0].shape
        assert source.pad_stack.angle == native.pad_stack.angle
    original_graphics = [pcb._unpack_item(v) for v in footprint.definition.items
                        if v.type_url.endswith('.BoardGraphicShape')]
    native_graphics = [pcb._unpack_item(v) for v in saved.definition.items
                      if v.type_url.endswith('.BoardGraphicShape')]
    assert len(native_graphics) == len(original_graphics)
    # Native library enumeration regenerates definition UUIDs and sorts children.
    # Placement UUID preservation is tested on the source PCB, not on library copies.
    def graphic_key(graphic):
        geometry = graphic.shape.WhichOneof('geometry')
        coordinates = {'rectangle': ('top_left', 'bottom_right'),
                       'circle': ('center', 'radius_point')}[geometry]
        points = [getattr(getattr(graphic.shape, geometry), field) for field in coordinates]
        return (graphic.layer, geometry, tuple((p.x_nm, p.y_nm) for p in points),
                graphic.shape.attributes.stroke.width.value_nm, graphic.shape.attributes.fill.fill_type)
    assert sorted(map(graphic_key, original_graphics)) == sorted(map(graphic_key, native_graphics))
    assert len(saved.definition.items) == len(footprint.definition.items)
    assert saved.overrides == footprint.overrides
    # The new library must be usable immediately, with no editor/restart.
    sch.create_symbol_library(a.symbol_library, a.symbol_library+'.kicad_sym', sd)
    symbol = LibrarySymbolDefinition.FromString(a.symbol_definition.read_bytes())
    symbol.lib_id.library_nickname = a.symbol_library
    next(field for field in symbol.fields if field.name == 'Footprint').text.text = footprint_id
    sch.save_symbol_definition(symbol, sd)
    def u1_nets():
        connection = sch.get_symbol_connections('U1', sd)
        names = {n.id: n.name for n in connection.snapshot.nets}
        return {p.pin.pin_number: [names[n] for n in p.net_ids] for p in connection.pins}
    native_before = u1_nets()
    assert not sch.get_connectivity_issues(sd).issues
    u2 = sch.add_symbol(a.symbol_library+':SII8784CNUC', (228600000, 101600000), sd,
                        reference='U2', fields={'Footprint': footprint_id})
    assert len(u2.pins) == 89
    sch.save_document(sd)
    assert u1_nets() == native_before
    issues = sch.get_connectivity_issues(sd)
    (out / 'sii-connectivity.json').write_text(json.dumps(MessageToDict(issues, descriptor_pool=_pool), indent=2)+'\n')
    dry = pcb.update_pcb_from_schematic(bd, schematic_document=sd, schematic_client=sch, dry_run=True)
    assert dry.can_apply and dry.component_count == 11
    with pcb.commit('Add unconnected SiI8784 without disturbing routed NE555', bd):
        applied = pcb.update_pcb_from_schematic(bd, schematic_document=sd, schematic_client=sch)
        fp = next(v for v in pcb.get_items([1], bd) if v.reference_field.text.text.text == 'U2')
        fp.position.x_nm, fp.position.y_nm = 210000000, 110000000
        pcb.update_items([fp], bd, fields=['position'])
        outlines = [v for v in pcb.get_items([kind('SHAPE')], bd) if v.layer == layer_id('Edge.Cuts')]
        assert len(outlines) == 1 and outlines[0].shape.HasField('rectangle')
        outlines[0].shape.rectangle.bottom_right.x_nm = 225000000
        pcb.update_items(outlines, bd, fields=['shape'])
    placed = pcb.get_footprint_pads('U2', bd)
    assert {p.number for p in placed} == set(map(str, range(1, 90)))
    assert not pcb.get_ratsnest(document=bd)
    # The whole source remains unconnected by design; all prior NE555 copper is retained.
    assert len(pcb.get_items([kind('TRACE')], bd)) == 126 and len(pcb.get_items([kind('VIA')], bd)) == 2
    pcb.save_document(bd)
    pcb.revert_document(bd)
    assert len(pcb.get_footprint_pads('U2', bd)) == 89 and not pcb.get_ratsnest(document=bd)
    report = {'pads': 89, 'perimeter_pads': 88, 'ep': '89', 'footprints': 11,
              'tracks': 126, 'vias': 2, 'ratsnest': 0, 'u1_nets': native_before,
              'expected_floating_u2': True, 'source_saved_via_ipc': True,
              'footprint_lib_id': footprint_id,
              'update': MessageToDict(applied, descriptor_pool=_pool)}
    (out / 'sii-integration-report.json').write_text(json.dumps(report, indent=2)+'\n')
    print('PASS native structured SiI89 library, immediate update, original U1 nets/copper and zero ratsnest', flush=True)


if __name__ == '__main__':
    main()
