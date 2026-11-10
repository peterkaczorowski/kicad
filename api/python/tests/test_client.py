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

    def test_connectivity_queries_keep_explicit_document_and_snapshot(self):
        from kipy_schematic import _message
        snapshot = _message('kiapi.schematic.types.ConnectivitySnapshot')(
            session_id='test-session', revision=42)
        snapshot.document.CopyFrom(self.doc)
        self.client.get_pin_net('U8', 39, pin_id='pin-uuid', document=self.doc)
        request = self.transport.commands[-1]
        self.assertEqual(request.reference, 'U8')
        self.assertEqual(request.pin_number, '39')
        self.assertEqual(request.pin_id.value, 'pin-uuid')
        self.assertEqual(request.document, self.doc)
        self.client.get_net_members('net-id', snapshot=snapshot)
        request = self.transport.commands[-1]
        self.assertEqual((request.session_id, request.revision), ('test-session', 42))
        self.assertEqual(request.document, self.doc)
        self.client.get_item_net(['wire-uuid', 'pin-uuid'], document=self.doc)
        self.assertEqual([i.value for i in self.transport.commands[-1].items], ['wire-uuid', 'pin-uuid'])
        self.client.get_item_net(use_selection=True, document=self.doc)
        self.assertTrue(self.transport.commands[-1].use_selection)
        self.client.get_symbol_connections('U8', document=self.doc)
        self.assertEqual(self.transport.commands[-1].reference, 'U8')
        self.client.highlight_net('net-id', snapshot=snapshot)
        self.assertEqual(self.transport.commands[-1].revision, 42)
        self.client.highlight_net(clear=True, document=self.doc)
        self.assertTrue(self.transport.commands[-1].clear)
        self.client.get_connectivity_issues(self.doc)
        self.assertEqual(self.transport.commands[-1].document, self.doc)
        self.client.get_schematic_netlist(self.doc)
        self.assertEqual(self.transport.commands[-1].DESCRIPTOR.name, 'GetSchematicNetlist')

    def test_net_rename_preview_and_refusal(self):
        from kipy_schematic import _message
        snapshot = _message('kiapi.schematic.types.ConnectivitySnapshot')(session_id='session', revision=3)
        snapshot.document.CopyFrom(self.doc)
        self.client.plan_net_rename('net-id', 'NEW', snapshot=snapshot)
        request = self.transport.commands[-1]
        self.assertEqual(request.DESCRIPTOR.name, 'PlanNetRename')
        self.assertEqual((request.net_id, request.new_name, request.session_id, request.revision),
                         ('net-id', 'NEW', 'session', 3))
        self.assertEqual(request.document, self.doc)
        plan = _message('kiapi.schematic.commands.NetRenamePlan')(can_apply=False, blockers=['Target exists'])
        with self.assertRaisesRegex(ValueError, 'Target exists'):
            self.client.rename_net(plan)
        plan.can_apply = True
        plan.id = 'issued-plan'
        plan.document.CopyFrom(self.doc)
        self.client.rename_net(plan)
        request = self.transport.commands[-1]
        self.assertEqual(request.plan_id, 'issued-plan')
        self.assertEqual(request.document, self.doc)
        self.assertNotIn('allow_merge', request.DESCRIPTOR.fields_by_name)
        self.client._in_commit = True
        with self.assertRaisesRegex(RuntimeError, 'independent Undo'):
            self.client.rename_net(plan)

    def test_page_settings_target_documents_and_orientation(self):
        self.client.get_page_settings(self.doc)
        request = self.transport.commands[-1]
        self.assertEqual(request.DESCRIPTOR.name, 'GetPageSettings')
        self.assertEqual(request.document, self.doc)
        other = Document(type=1)
        other.sheet_path.path.add(value='child-uuid')
        self.client.set_page_settings('A3', 'portrait', documents=[self.doc, other])
        request = self.transport.commands[-1]
        self.assertEqual(request.DESCRIPTOR.name, 'SetPageSettings')
        self.assertEqual(list(request.documents), [self.doc, other])
        self.assertEqual((request.paper_format, request.orientation), ('A3', 2))
        with self.assertRaises(ValueError):
            self.client.set_page_settings('A3', 'diagonal', documents=[self.doc])
        with self.assertRaises(ValueError):
            self.client.set_page_settings('A3', 'landscape', documents=[])

    def test_native_kipy_can_coexist(self):
        from kipy.proto.common.types.base_types_pb2 import DocumentSpecifier
        self.assertEqual(DocumentSpecifier(type=1).type, self.doc.type)
        self.assertIsNot(DocumentSpecifier.DESCRIPTOR.file.pool, self.doc.DESCRIPTOR.file.pool)


if __name__ == '__main__':
    unittest.main()
