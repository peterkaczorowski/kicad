"""Live stage-3 item/library/net tests. Never targets an unmarked user project."""
from pathlib import Path
import argparse,json,time
from kipy_schematic import PCBClient, Document
from kipy_schematic.pcb import Track,Footprint,layer_id
from uuid import uuid4
from kipy.errors import ApiError


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project',type=Path,required=True);parser.add_argument('--socket',required=True)
    parser.add_argument('--artifacts',type=Path,required=True);args=parser.parse_args()
    project=args.project.resolve();out=args.artifacts.resolve();out.mkdir(exist_ok=True,parents=True)
    assert (project/'.ipc-test-project').is_file() and project.name not in ('CodexTest2','Codex800XL')
    c=PCBClient(socket_path=args.socket,timeout_ms=60000)
    docs=[d for d in c.get_open_documents() if Path(d.project.path).resolve()==project];assert len(docs)==1;doc=docs[0]
    source=project/doc.board_filename;content=source.read_bytes();report=[]
    def state():return (sorted(x.SerializeToString(deterministic=True).hex() for x in c.get_items(document=doc)),sorted(n.name for n in c.get_nets(doc)))
    def ok(text):
        report.append(text);(out/'pcb-items-report.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS',text,flush=True)
    def refuses(fn):
        try:fn()
        except (ApiError,RuntimeError,ValueError):return
        raise AssertionError('Expected explicit refusal')
    def wait_state(expected):
        for _ in range(150):
            if state()==expected:return
            time.sleep(.05)
        raise AssertionError('Undo/Redo state mismatch')
    def undo(expected):c.run_action(doc,'common.Interactive.undo');wait_state(expected)
    def redo(expected):c.run_action(doc,'common.Interactive.redo');wait_state(expected)
    original=state()
    resistor='Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal'
    led='LED_THT:LED_D5.0mm'
    try:
        with c.commit('Library footprints and native transforms Drop',doc):
            r=c.place_footprint(resistor,(100000000,100000000),doc,reference='R90',value='1k')
            d=c.place_footprint(led,(130000000,100000000),doc,reference='D90',side='B.Cu',rotation_degrees=90)
            assert len(r.pads)==len(d.pads)==2 and d.footprint.layer==layer_id('B.Cu')
            assert c.get_pad('R90','2',doc).position.x_nm-c.get_pad('R90','1',doc).position.x_nm==10160000
            fp=c.get_items(document=doc,ids=[r.footprint.id.value])[0];ids={p.number:p.id.value for p in c.get_footprint_pads('R90',doc)}
            fp.position.x_nm=110000000;fp.position.y_nm=120000000;fp.orientation.value_degrees=90
            c.update_items([fp],doc,fields=['position','orientation'])
            pads={p.number:p for p in c.get_footprint_pads('R90',doc)}
            assert {n:p.id.value for n,p in pads.items()}==ids
            assert (pads['1'].position.x_nm,pads['1'].position.y_nm)==(110000000,120000000)
            assert abs(pads['2'].position.y_nm-pads['1'].position.y_nm)==10160000
            assert pads['2'].position.x_nm==pads['1'].position.x_nm
            refuses(lambda:c.place_footprint('Missing:NoFootprint',(0,0),doc,reference='D91'))
            refuses(lambda:c.place_footprint(resistor,(0,0),doc,reference='R90'))
            refuses(lambda:c.place_footprint(resistor,(0,0),doc,side='F.SilkS',reference='R91'))
            other=PCBClient(socket_path=args.socket,timeout_ms=10000)
            refuses(lambda:other.place_footprint(resistor,(0,0),doc,reference='R91'))
            # Newest item is visible by reference while the API commit is still open.
            assert other.get_pad('R90','1',doc).id.value==ids['1']
            raise ValueError('drop')
    except ValueError as e:assert str(e)=='drop'
    assert state()==original and source.read_bytes()==content
    ok('Native library R/LED placement, B-side/rotation, live absolute pads, transform masks preserve child UUIDs; invalid lib/ref/side and foreign writer refuse; Drop complete')

    old=state();r=c.place_footprint(resistor,(100000000,100000000),doc,reference='R90',value='1k');created=state();undo(old);redo(created)
    pads={p.number:p for p in c.get_footprint_pads('R90',doc)}
    raw=c.get_items(document=doc,ids=[pads['1'].id.value])[0];raw.net.name='GND'
    c.update_items([raw],doc,container_id=r.footprint.id.value,create_net_if_missing=True,allow_net_mismatch=True)
    assert c.get_pad('R90','1',doc).net.name=='GND'
    raw=c.get_items(document=doc,ids=[pads['2'].id.value])[0];raw.net.name='+5V'
    c.update_items([raw],doc,container_id=r.footprint.id.value,create_net_if_missing=True,allow_net_mismatch=True)
    assert c.get_pad('R90','2',doc).net.name=='+5V'
    protected=state();raw=c.get_items(document=doc,ids=[pads['1'].id.value])[0];raw.net.name='+5V'
    refuses(lambda:c.update_items([raw],doc,container_id=r.footprint.id.value))
    assert state()==protected
    # Replacing from a different library preserves board identity, reference and pad nets.
    replacement=c.place_footprint(led,document=doc,replace_id=r.footprint.id.value)
    assert replacement.footprint.id.value==r.footprint.id.value
    assert {p.number:p.net.name for p in replacement.pads}=={'1':'GND','2':'+5V'}
    assert all(p.reference=='R90' for p in replacement.pads)
    replaced=state();undo(protected);redo(replaced)
    reloaded=c.place_footprint(led,document=doc,replace_id=r.footprint.id.value)
    assert {p.number:p.net.name for p in reloaded.pads}=={'1':'GND','2':'+5V'}
    assert reloaded.footprint.id.value==r.footprint.id.value
    c.delete_items([r.footprint.id.value],doc)
    ok('Footprint create Undo/Redo, guarded pad reassignment, explicit new nets, different-lib replacement preserves nets/reference/UUID and native Undo/Redo')

    baseline=state();names={n.name for n in c.get_nets(doc)};assert 'TYPO' not in names
    refuses(lambda:c.add_track((10000000,10000000),(20000000,10000000),250000,document=doc,net='TYPO'))
    assert state()==baseline
    try:
        with c.commit('Explicit new net Drop',doc):
            c.add_track((10000000,10000000),(20000000,10000000),250000,document=doc,net='TYPO',create_net_if_missing=True)
            assert 'TYPO' in {n.name for n in c.get_nets(doc)}
            raise ValueError('drop')
    except ValueError as e:assert str(e)=='drop'
    assert state()==baseline
    c.add_track((10000000,10000000),(20000000,10000000),250000,document=doc,net='TYPO',create_net_if_missing=True)
    newnet=state();undo(baseline);assert 'TYPO' not in {n.name for n in c.get_nets(doc)};redo(newnet);undo(baseline)
    ok('Unknown net refuses without side effects; explicit net+track share Drop and single Undo/Redo; Undo removes orphan net')

    # An early error in the second update must not strand the first modification.
    guarded=c.add_track((30000000,10000000),(40000000,10000000),250000,document=doc,net='GND')
    guard_state=state();guarded.end.x_nm=45000000
    nonexistent=Footprint();nonexistent.id.value=str(uuid4())
    refuses(lambda:c.update_items([guarded,nonexistent],doc,fields=['end']))
    assert state()==guard_state
    assert c.run_action(doc,'common.Control.zoomRedraw')==1
    c.delete_items([guarded.id.value],doc);assert state()==baseline
    bad=Track(layer=layer_id('F.Cu'));bad.id.value='bdcfb09e-1d56-40ba-93f9-4072dc0f9be6';bad.width.value_nm=250000;bad.end.x_nm=1000000
    bad.net.name='NOT_CREATED'
    refuses(lambda:c.create_items([bad],doc))
    assert state()==baseline
    wrong=Document();wrong.CopyFrom(doc);wrong.project.path='/definitely/not/the/open/project'
    refuses(lambda:c.get_ratsnest(document=wrong));refuses(lambda:c.get_footprint_pads('R90',wrong))
    refuses(lambda:c.get_ratsnest('not-an-existing-net',doc))
    assert source.read_bytes()==content
    ok('Invalid project/net selectors refuse; no source board file written during item tests')
    print('PCB ITEM/LIBRARY/NET ACCEPTANCE COMPLETE',flush=True)

if __name__=='__main__':main()
