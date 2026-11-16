"""Stage-3 full native schematic→PCB acceptance; only marked disposable projects."""
from pathlib import Path
import argparse,json,time,sys,shutil
from uuid import uuid4
from google.protobuf.json_format import MessageToDict
from kipy_schematic import SchematicClient,PCBClient,Document,_message,_pool
from kipy_schematic.pcb import BoardGraphic,layer_id
from kipy.errors import ApiError


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project',type=Path,required=True);parser.add_argument('--schematic-socket',required=True)
    parser.add_argument('--pcb-socket',required=True);parser.add_argument('--artifacts',type=Path,required=True)
    args=parser.parse_args();project=args.project.resolve();out=args.artifacts.resolve();out.mkdir(exist_ok=True,parents=True)
    assert (project/'.ipc-test-project').is_file() and project.name not in ('CodexTest2','Codex800XL')
    sch=SchematicClient(socket_path=args.schematic_socket,timeout_ms=60000);pcb=PCBClient(socket_path=args.pcb_socket,timeout_ms=60000)
    sdocs=[d for d in sch.get_open_documents() if Path(d.project.path).resolve()==project];assert len(sdocs)==1;sd=sdocs[0]
    bdocs=[d for d in pcb.get_open_documents() if Path(d.project.path).resolve()==project];assert len(bdocs)==1;bd=bdocs[0]
    sfile=project/'document-test.kicad_sch';bfile=project/bd.board_filename
    source_saved=sfile.read_bytes();report=[]
    def ok(text):
        report.append(text);(out/'integration-report.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS',text,flush=True)
    def snapshot():
        records=[]
        for value in pcb.get_items(document=bd):
            # BOARD UUID is session-only and regenerated on native file reload.
            if 'parent' in value.DESCRIPTOR.fields_by_name:value.ClearField('parent')
            if 'definition' in value.DESCRIPTOR.fields_by_name:
                # Native saving sorts footprint child records. Compare the full
                # records as a set; retain every child UUID, geometry and net.
                ordered=sorted(value.definition.items,key=lambda v:v.SerializeToString(deterministic=True))
                del value.definition.items[:];value.definition.items.extend(ordered)
            records.append(value.SerializeToString(deterministic=True).hex())
        return sorted(records),sorted(n.name for n in pcb.get_nets(bd))
    def sstate():return sorted(v.SerializeToString(deterministic=True).hex() for v in sch.get_items(document=sd))
    def refuses(fn):
        try:fn()
        except (ApiError,RuntimeError,ValueError):return
        raise AssertionError('Expected refusal')
    def wait(fn):
        for _ in range(200):
            if fn():return
            time.sleep(.05)
        raise AssertionError('State did not reach expected value')
    def undo(expected):pcb.run_action(bd,'common.Interactive.undo');wait(lambda:snapshot()==expected)
    def redo(expected):pcb.run_action(bd,'common.Interactive.redo');wait(lambda:snapshot()==expected)
    def update(dry=False,**options):return pcb.update_pcb_from_schematic(bd,schematic_document=sd,schematic_client=sch,dry_run=dry,**options)

    # Clear only this marked board, then remove test-only nets using the real updater
    # and a deliberately empty live schematic snapshot. Restore saved NE555 afterwards.
    items=pcb.get_items(document=bd)
    if items:pcb.delete_items([v.id.value for v in items],bd)
    saved_sstate=sstate();sch.delete_items([v.id.value for v in sch.get_items(document=sd)],sd)
    assert not sch.get_items(document=sd)
    empty=update(delete_unused_footprints=True);assert empty.applied and empty.component_count==0
    sch.revert_document(sd,discard_unsaved=True);assert sstate()==saved_sstate and sfile.read_bytes()==source_saved
    pcb.save_document(bd);pcb.revert_document(bd);initial=snapshot();assert not initial[0]
    initial_file=bfile.read_bytes()
    original_sstate=sstate()
    dry=update(True);(out/'update-dry-run.json').write_text(json.dumps(MessageToDict(dry,descriptor_pool=_pool),indent=2)+'\n')
    assert dry.can_apply and not dry.applied and dry.component_count==10 and dry.net_count==7
    assert sum(v.kind==1 for v in dry.footprints)==10
    assert snapshot()==initial and bfile.read_bytes()==initial_file and sfile.read_bytes()==source_saved
    # Dirty Revert would refuse; successful default Revert proves dry_run left it clean.
    pcb.revert_document(bd);assert snapshot()==initial
    ok('Native dry_run reports 10 footprints/7 nets, pad-net plan and provenance; no board/file/dirty change')

    r1=next(v for v in sch.get_items([35],sd) if v.reference=='R1')
    for field in r1.fields:
        if field.name=='Value':field.text.text='120k'
    sch.update_items([r1],sd,fields=['fields'])
    assert sfile.read_bytes()==source_saved
    try:
        with pcb.commit('Unsaved live snapshot Drop',bd):
            applied=update();assert applied.source.source==1 and applied.source.source_modified
            fp=next(v for v in pcb.get_items([1],bd) if v.reference_field.text.text.text=='R1')
            assert fp.value_field.text.text.text=='120k'
            assert len(pcb.get_footprint_pads('U1',bd))==8
            raise ValueError('drop')
    except ValueError as e:assert str(e)=='drop'
    assert snapshot()==initial
    try:
        with pcb.commit('Explicit saved snapshot Drop',bd):
            applied=pcb.update_pcb_from_schematic(bd,schematic_file=sfile)
            assert applied.source.source==2
            fp=next(v for v in pcb.get_items([1],bd) if v.reference_field.text.text.text=='R1')
            assert fp.value_field.text.text.text=='110k'
            raise ValueError('drop')
    except ValueError as e:assert str(e)=='drop'
    assert snapshot()==initial and sfile.read_bytes()==source_saved
    sch.revert_document(sd,discard_unsaved=True);assert sstate()==original_sstate
    ok('Live unsaved R1=120k imported through native snapshot; explicit saved source remains 110k; both complete Update+net creation Drop atomically')
    refuses(lambda:pcb.update_pcb_from_schematic(bd,schematic_file=project/'missing.kicad_sch'))
    wrong=Document();wrong.CopyFrom(bd);wrong.project.path='/wrong/project'
    refuses(lambda:pcb.update_pcb_from_schematic(wrong,schematic_document=sd,schematic_client=sch,dry_run=True))
    with sch.commit('Export during pending source transaction',sd):refuses(lambda:sch.export_schematic_for_pcb(sd))
    assert snapshot()==initial
    ok('Missing source, wrong project and pending schematic commit refuse without partial PCB changes')

    standalone=update();assert standalone.applied and len(pcb.get_items([1],bd))==10
    standalone_state=snapshot();undo(initial);redo(standalone_state);undo(initial)
    assert sfile.read_bytes()==source_saved and bfile.read_bytes()==initial_file
    ok('Standalone UpdatePcbFromSchematic has one complete Undo/Redo with stable UUIDs and no automatic save')

    positions={
       'U1':((127000000,101600000),0),'R1':((111760000,91440000),0),
       'R2':((111760000,106680000),0),'R3':((144780000,106680000),0),
       'D1':((104140000,101600000),0),'D2':((165100000,106680000),180),
       'C1':((104140000,121920000),0),'C2':((132080000,121920000),0),
       'C3':((144780000,88900000),0),'J1':((91440000,91440000),0)}
    expected=json.loads((out/'ne555-reference.json').read_text())
    expected_pads={(node['ref'],node['pin']):net['name'] for net in expected['nets'] for node in net['nodes']}
    with pcb.commit('NE555: schematic update, placement, outline and all copper',bd):
        applied=update();assert applied.applied and applied.component_count==10 and applied.net_count==7
        assert {n.name for n in pcb.get_nets(bd) if n.name}=={net['name'] for net in expected['nets']}
        for fp in pcb.get_items([1],bd):
            ref=fp.reference_field.text.text.text;xy,rotation=positions[ref]
            pad_ids={p.number:p.id.value for p in pcb.get_footprint_pads(ref,bd)}
            fp.position.x_nm,fp.position.y_nm=xy;fp.orientation.value_degrees=rotation
            pcb.update_items([fp],bd,fields=['position','orientation'])
            pads=pcb.get_footprint_pads(ref,bd)
            assert {p.number:p.id.value for p in pads}==pad_ids
            for pad in pads:assert pad.net.name==expected_pads[(ref,pad.number)]
        assert len(pcb.get_items([1],bd))==10
        outline=BoardGraphic(layer=layer_id('Edge.Cuts'));outline.id.value=str(uuid4())
        outline.shape.rectangle.top_left.x_nm=80000000;outline.shape.rectangle.top_left.y_nm=80000000
        outline.shape.rectangle.bottom_right.x_nm=180000000;outline.shape.rectangle.bottom_right.y_nm=140000000
        outline.shape.attributes.stroke.width.value_nm=50000
        pcb.create_items([outline],bd)
        # Explicit acceptance pair: an unobstructed horizontal F.Cu segment.
        track=pcb.add_track(from_pad=('R3','2'),to_pad=('D2','2'),width_nm=250000,layer='F.Cu',document=bd)
        assert track.net.name=='Net-(D2-A)'
        a=pcb.get_pad('R3','2',bd);z=pcb.get_pad('D2','2',bd)
        connected=pcb.get_connected_items([track.id.value],bd)
        assert {a.id.value,z.id.value}<={v.id.value for v in connected}
        assert not pcb.get_ratsnest('Net-(D2-A)',bd)
        sys.path.insert(0,str(out));from route_ne555 import route
        routed=route(pcb,bd,out)
        remaining=pcb.get_ratsnest(document=bd)
        (out/'ratsnest-final.json').write_text(json.dumps([MessageToDict(v,descriptor_pool=_pool) for v in remaining],indent=2)+'\n')
        assert not remaining,remaining
        assert bfile.read_bytes()==initial_file
    final=snapshot();(out/'update-applied.json').write_text(json.dumps(MessageToDict(applied,descriptor_pool=_pool),indent=2)+'\n')
    ok('One PCB commit: native update, 10 placements, stable pad UUIDs/nets, outline, pad-to-pad F.Cu track and complete two-layer copper; ratsnest empty')
    undo(initial);redo(final)
    ok('One Undo restores exact pre-update PCB and net set; one Redo restores all footprint/pad/track/via UUIDs and geometry')
    (out/'pcb-before-save.json').write_text(json.dumps([MessageToDict(v,descriptor_pool=_pool) for v in pcb.get_items(document=bd)],indent=2)+'\n')
    pcb.save_document(bd);saved_board=bfile.read_bytes();pcb.revert_document(bd)
    (out/'pcb-after-revert.json').write_text(json.dumps([MessageToDict(v,descriptor_pool=_pool) for v in pcb.get_items(document=bd)],indent=2)+'\n')
    assert snapshot()==final
    ok('SaveDocument and native Revert reopen routed PCB with stable state')

    # External-change Revert uses ONLY KiCad-generated SaveCopy files in this marked project.
    for client,doc,source,kind in ((sch,sd,sfile,'schematic'),(pcb,bd,bfile,'pcb')):
        saved=source.read_bytes()
        if kind=='schematic':
            def read():return next(v for v in client.get_items([35],doc) if v.reference=='R1')
            def set_value(text):
                item=read()
                for field in item.fields:
                    if field.name=='Value':field.text.text=text
                client.update_items([item],doc,fields=['fields'])
            def value():return next(f.text.text for f in read().fields if f.name=='Value')
        else:
            def read():return next(v for v in client.get_items([1],doc) if v.reference_field.text.text.text=='R1')
            def set_value(text):
                item=read();item.value_field.text.text.text=text;client.update_items([item],doc,fields=['value_field'])
            def value():return read().value_field.text.text.text
        original_value=value();set_value('REVERT_EXTERNAL')
        copy=project/('external-copy'+source.suffix);client.save_copy_of_document(doc,copy,overwrite=True)
        client.revert_document(doc,discard_unsaved=True);assert value()==original_value
        shutil.copyfile(copy,source);client.revert_document(doc);assert value()=='REVERT_EXTERNAL'
        set_value('REVERT_UNSAVED');refuses(lambda:client.revert_document(doc));assert value()=='REVERT_UNSAVED'
        client.revert_document(doc,discard_unsaved=True);assert value()=='REVERT_EXTERNAL'
        source.write_bytes(saved);client.revert_document(doc);assert value()==original_value
        ok(kind+' Revert imports external native SaveCopy without dialog; refuses unsaved changes by default; explicit discard works')
    assert bfile.read_bytes()==saved_board and sfile.read_bytes()==source_saved
    assert snapshot()==final and not pcb.get_ratsnest(document=bd)
    (out/'final-pcb-items.json').write_text(json.dumps([MessageToDict(v,descriptor_pool=_pool) for v in pcb.get_items(document=bd)],indent=2)+'\n')
    print('FULL SCHEMATIC→PCB IPC ACCEPTANCE COMPLETE',flush=True)

if __name__=='__main__':main()
