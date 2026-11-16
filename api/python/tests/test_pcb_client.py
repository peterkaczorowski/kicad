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







if __name__ == '__main__':
    unittest.main()
