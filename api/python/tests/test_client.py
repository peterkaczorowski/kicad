"""Run with the built client on PYTHONPATH; no running KiCad is required."""
import unittest
from kipy_schematic import Document, SheetPin, SchematicClient


class Transport:
    def __init__(self):
        self.commands = []

    def send(self, request, response_type):
        self.commands.append(request)
        response = response_type()
        name = request.DESCRIPTOR.name
        if name == 'GetOpenDocuments':
            document = response.documents.add(type=1)
            document.sheet_path.path.add(value='root-uuid')
            document.project.name = 'test'
        elif name == 'GetItems':
            response.status = 1
        elif name == 'CreateSheet':
            response.sheet.CopyFrom(request.sheet)
            response.sheet.id.value = 'created-uuid'
        elif name == 'BeginCommit':
            response.id.value = 'commit-uuid'
        return response


class ClientTest(unittest.TestCase):
    def setUp(self):
        self.transport = Transport()
        self.client = SchematicClient(transport=self.transport)
        self.doc = Document(type=1)
        self.doc.sheet_path.path.add(value='root-uuid')

    def test_empty_unsaved_page_and_sheet_pin_filter(self):
        self.assertEqual(self.client.get_sheets(self.doc), [])
        self.assertEqual(list(self.transport.commands[-1].types), [37])
        self.client.get_sheet_pins(self.doc, 'sheet-uuid')
        self.assertEqual(self.transport.commands[-1].header.container.value, 'sheet-uuid')
        self.assertEqual(list(self.transport.commands[-1].types), [36])

    def test_create_preserves_units_and_pins(self):
        pin = SheetPin(name='DATA', side=1, shape=3)
        pin.position.x_nm = -1000000
        sheet = self.client.create_sheet('power', 'power.kicad_sch', document=self.doc,
                                        position_nm=(-1000000, 2000000), pins=[pin])
        self.assertEqual(sheet.position.x_nm, -1000000)
        self.assertEqual(sheet.pins[0].name, 'DATA')
        self.assertEqual(sheet.id.value, 'created-uuid')
        self.assertFalse(self.transport.commands[-1].link_existing)
        self.client.create_sheet('copy', 'existing.kicad_sch', document=self.doc,
                                 link_existing=True)
        self.assertTrue(self.transport.commands[-1].link_existing)

    def test_commit_success_and_rollback(self):
        with self.client.commit(document=self.doc):
            pass
        self.assertEqual(self.transport.commands[-1].action, 1)
        with self.assertRaisesRegex(ValueError, 'failure'):
            with self.client.commit(document=self.doc):
                raise ValueError('failure')
        self.assertEqual(self.transport.commands[-1].action, 2)
        self.assertEqual(self.transport.commands[-1].header.document, self.doc)
        self.assertFalse(self.client._in_commit)

    def test_native_kipy_can_coexist(self):
        from kipy.proto.common.types.base_types_pb2 import DocumentSpecifier
        self.assertEqual(DocumentSpecifier(type=1).type, self.doc.type)
        self.assertIsNot(DocumentSpecifier.DESCRIPTOR.file.pool, self.doc.DESCRIPTOR.file.pool)


if __name__ == '__main__':
    unittest.main()
