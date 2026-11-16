"""Pure transport tests for the matching PCB adapter; no editor required."""
import unittest
from kipy_schematic import PCBClient, SchematicClient, Document
from kipy_schematic import _message
from kipy_schematic.pcb import Track, layer_id
from test_client import Transport


class PCBTransport(Transport):
    def send(self, request, response_type):
        result = super().send(request, response_type)
        if request.DESCRIPTOR.name == 'GetOpenDocuments':
            result.documents[0].type = 3
            result.documents[0].board_filename = 'board.kicad_pcb'
        name = request.DESCRIPTOR.name
        if name == 'ExportSchematicForPcb':
            result.source = 1
            result.source_document.CopyFrom(request.document)
            result.native_netlist = '(export (version "E"))'
            result.session_id, result.revision = 'test-session', 3
        elif name == 'UpdatePcbFromSchematic':
            result.can_apply, result.applied = True, not request.dry_run
        elif name == 'GetFootprintPads':
            for number, x in [('1', 0), ('2', 10000000)]:
                pad = result.pads.add(reference=request.reference, number=number)
                pad.position.x_nm = x
                pad.net.name = 'GND'
                pad.copper_layers.extend([layer_id('F.Cu'), layer_id('B.Cu')])
        return result


class PCBClientTests(unittest.TestCase):
    def setUp(self):
        self.transport = PCBTransport()
        self.client = PCBClient(transport=self.transport)
        self.doc = Document(type=3, board_filename='board.kicad_pcb')

    def test_footprint_library_sources_and_project_persistence(self):
        from pathlib import Path
        from kipy_schematic.pcb import Footprint
        self.client.search_footprint_libraries('qfn', self.doc, library='Local', limit=3, offset=4)
        request = self.transport.commands[-1]
        self.assertEqual((request.query, request.library_nickname, request.limit, request.offset),
                         ('qfn', 'Local', 3, 4))
        self.client.create_footprint_library('Local', Path('Local.pretty'), self.doc)
        request = self.transport.commands[-1]
        self.assertEqual(request.path, 'Local.pretty')
        self.assertFalse(request.link_existing)
        self.client.create_footprint_library('Local', 'Local.pretty', self.doc, link_existing=True)
        self.assertTrue(self.transport.commands[-1].link_existing)
        self.client.get_footprint_definition('Local:Part', self.doc)
        self.assertEqual(self.transport.commands[-1].WhichOneof('source'), 'lib_id')
        self.client.get_footprint_definition(document=self.doc, footprint_id='live-uuid')
        self.assertEqual(self.transport.commands[-1].WhichOneof('source'), 'footprint_id')
        count = len(self.transport.commands)
        for args in ({}, {'lib_id': 'Local:Part', 'footprint_id': 'live-uuid'}):
            with self.assertRaises(ValueError):
                self.client.get_footprint_definition(document=self.doc, **args)
        self.assertEqual(len(self.transport.commands), count)
        definition = Footprint()
        definition.definition.id.library_nickname, definition.definition.id.entry_name = 'Local', 'Part'
        self.client.save_footprint_definition(definition, self.doc)
        self.assertEqual(self.transport.commands[-1].definition, definition)
        self.assertFalse(self.transport.commands[-1].overwrite)
        self.client.save_footprint_definition(definition, self.doc, overwrite=True)
        self.assertTrue(self.transport.commands[-1].overwrite)
        self.client.recover_board_footprint('live-uuid', 'Local:Recovered', self.doc)
        request = self.transport.commands[-1]
        self.assertEqual(request.footprint_id.value, 'live-uuid')
        self.assertEqual(request.target_lib_id.entry_name, 'Recovered')
        self.assertFalse(request.overwrite)
        self.assertTrue(all(r.document == self.doc for r in self.transport.commands))

    def test_item_defaults_are_top_level_and_explicit(self):
        self.client.get_items(document=self.doc)
        request = self.transport.commands[-1]
        self.assertIn(1, request.types)  # footprint
        self.assertNotIn(2, request.types)  # pad children are queried explicitly
        self.assertNotIn(5, request.types)  # field children
        self.assertEqual(request.header.document.type, 3)

    def test_net_creation_is_explicit(self):
        self.client.add_track((0, 0), (2540000, 0), 250000, document=self.doc, net='GND')
        request = self.transport.commands[-1]
        self.assertFalse(request.header.create_net_if_missing)
        self.assertFalse(request.header.allow_net_mismatch)
        self.client.add_track((0, 0), (2540000, 0), 250000, document=self.doc,
                              net='NEW', create_net_if_missing=True)
        self.assertTrue(self.transport.commands[-1].header.create_net_if_missing)

    def test_update_mask_and_pad_override_explicit(self):
        track = Track(); track.id.value = 'uuid'
        self.client.update_items([track], self.doc, fields=['end'], allow_net_mismatch=True)
        request = self.transport.commands[-1]
        self.assertEqual(list(request.header.field_mask.paths), ['end'])
        self.assertTrue(request.header.allow_net_mismatch)
        self.assertFalse(request.header.create_net_if_missing)

    def test_default_document_and_transaction_type(self):
        doc = self.client.active_document()
        self.assertEqual(doc.type, 3)
        with self.client.commit('PCB transaction', doc):
            self.client.add_track((0, 0), (1000000, 0), 250000, document=doc)
        begin = next(r for r in self.transport.commands if r.DESCRIPTOR.name == 'BeginCommit')
        self.assertEqual(begin.header.document.type, 3)

    def test_bad_geometry_fails_before_transport(self):
        for width, end in [(0, (1, 2)), (250000, (0, 0))]:
            with self.assertRaises(ValueError):
                self.client.add_track((0, 0), end, width, document=self.doc)
        self.assertEqual(len(self.transport.commands), 0)
        self.assertNotEqual(layer_id('F.Cu'), layer_id('B.Cu'))

    def test_library_footprint_and_replacement_are_explicit(self):
        self.client.place_footprint('LED_THT:LED_D5.0mm', (10000000, 20000000), self.doc,
                                    side='B.Cu', rotation_degrees=90, reference='D2')
        request = self.transport.commands[-1]
        self.assertEqual(request.lib_id.entry_name, 'LED_D5.0mm')
        self.assertEqual(request.side, layer_id('B.Cu'))
        self.assertEqual(request.reference, 'D2')
        self.client.place_footprint('LED_THT:LED_D5.0mm', document=self.doc, replace_id='existing')
        request = self.transport.commands[-1]
        self.assertEqual(request.replace_id.value, 'existing')
        self.assertFalse(request.HasField('position'))
        self.assertFalse(request.HasField('rotation_degrees'))
        self.assertFalse(request.override_locks)

    def test_pads_tracks_and_ratsnest_native_requests(self):
        track = self.client.add_track(from_pad=('R3','2'), to_pad=('D2','1'),
                                      document=self.doc, width_nm=250000)
        self.assertEqual(track.net.name, 'GND')
        self.assertEqual(track.start.x_nm, 10000000)
        self.assertEqual(track.end.x_nm, 0)
        self.client.get_ratsnest('GND', self.doc, pad_id='pad-id')
        request = self.transport.commands[-1]
        self.assertEqual(request.net_name, 'GND')
        self.assertEqual(request.pad_id.value, 'pad-id')
        with self.assertRaises(ValueError):
            self.client.add_track(from_pad=('R3','2'), to_pad=('D2','1'), document=self.doc, net='WRONG')

    def test_live_export_and_dry_run_pipeline(self):
        schdoc = Document(type=1)
        schdoc.project.path = '/project'
        schdoc.sheet_path.path.add(value='root')
        sch = SchematicClient(transport=self.transport)
        reply = self.client.update_pcb_from_schematic(self.doc, dry_run=True,
                        schematic_document=schdoc, schematic_client=sch)
        self.assertTrue(reply.can_apply)
        self.assertFalse(reply.applied)
        request = self.transport.commands[-1]
        self.assertTrue(request.dry_run)
        self.assertEqual(request.live_snapshot.source_document, schdoc)
        self.assertEqual(request.live_snapshot.session_id, 'test-session')
        self.assertTrue(request.options.lookup_by_uuid)
        self.assertFalse(request.options.delete_unused_footprints)

    def test_saved_source_does_not_query_live_editor(self):
        self.client.update_pcb_from_schematic(self.doc, schematic_file='/project/design.kicad_sch')
        self.assertEqual(len(self.transport.commands), 1)
        self.assertEqual(self.transport.commands[-1].schematic_file, '/project/design.kicad_sch')
        with self.assertRaises(ValueError):
            self.client.update_pcb_from_schematic(self.doc, schematic_file='/p/s.kicad_sch',
                                                  schematic_document=Document(type=1))

    def test_via_preserves_explicit_geometry_and_net(self):
        via = self.client.add_via((10000000,20000000), 'GND', self.doc)
        self.assertEqual(via.net.name, 'GND')
        self.assertEqual(via.pad_stack.drill.diameter.x_nm, 400000)
        self.assertEqual(via.pad_stack.copper_layers[0].size.x_nm, 800000)


if __name__ == '__main__':
    unittest.main()
