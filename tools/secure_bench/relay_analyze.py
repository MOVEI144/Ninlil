"""Reconcile three-board captures independently of the campaign verdict."""
import hashlib,json,re,sys
from hardware import EVIDENCE
label=sys.argv[1]
assert re.fullmatch(r'relay-20260908-[a-z0-9-]+',label)
r=json.loads((EVIDENCE/f'{label}-result.json').read_text());assert r['result']=='PASS'
records={};hashes={};per_file={}
for board,paths in r['logs'].items():
    records[board]=[]
    for name in paths:
        path=EVIDENCE/name;hashes[name]=hashlib.sha256(path.read_bytes()).hexdigest();pending=None;events=[]
        for raw in path.read_text().splitlines():
            e=json.loads(raw)
            if 'tx' in e:
                assert pending is None
                cmd,_,data=e['tx'].partition(' ');pending=(cmd,bytes.fromhex(data))
            match=re.fullmatch(r'BENCH (.) (-?\d+)(?: ([0-9a-f]*))?',e.get('rx',''))
            if not match:continue
            cmd,rc,out=match[1],int(match[2]),bytes.fromhex(match[3] or '')
            if cmd=='!':assert pending is None and rc==0;continue
            assert pending and pending[0]==cmd
            if rc!=0:assert not out
            events.append((cmd,pending[1],rc,out));pending=None
        assert pending is None
        per_file[name]=events;records[board]+=events
count=0;envelopes=0
for board,events in records.items():
    sent=[data for cmd,data,rc,_ in events if cmd=='T' and rc==0]
    received=[out for cmd,_,rc,out in events if cmd=='R' and rc==0]
    assert sent==[bytes.fromhex(t['hex']) for t in r['traces'] if t['from']==board]
    assert received==[bytes.fromhex(t['hex']) for t in r['traces'] if t['to']==board]
    assert all(1<=len(f)<=240 for f in sent);count+=len(sent)
    last={}
    for cmd,data,rc,out in events:
        if rc!=0 or cmd not in ('S','C','j','n') or not out.startswith(b'NS\x01'):continue
        assert int.from_bytes(out[4:6],'big')=={'a':1,'b':2,'c':3}[board]
        key=out[3:4]+out[8:24];counter=int.from_bytes(out[24:29],'big')
        assert key not in last or counter>last[key];last[key]=counter;envelopes+=1
assert count==r['rf_frames']==len(r['traces'])
old,new=[per_file[name] for name in r['logs']['b']]
assert [out for cmd,_,rc,out in old if cmd=='q' and rc==0][-1]==[out for cmd,_,rc,out in new if cmd=='q' and rc==0][0]
owned={};received=set();retired=set()
for raw in (EVIDENCE/f'{label}-ownership.jsonl').read_text().splitlines():
    e=json.loads(raw);kind=e['kind']
    if kind=='source_owns_test_packet':
        packet=bytes.fromhex(e['packet']);assert packet[26:42].hex()==e['id']==hashlib.sha256(packet[48:]).digest()[:16].hex()
        assert e['id'] not in owned;owned[e['id']]=e['plain']
    if kind=='destination_test_payload_committed_on_host':
        assert e['id'] in owned and e['plain']==owned[e['id']];received.add(e['id'])
    if kind=='relay_hop_retired':assert e['id'] in received;retired.add(e['id'])
assert set(owned)==received==retired and len(owned)==31
summary={'result':'PASS','matched_physical_frames':count,'unique_counter_envelopes':envelopes,'source_owned_and_host_observed_packets':len(owned),'restored_opaque_packet_matches':True,'log_sha256':hashes}
with (EVIDENCE/f'{label}-analysis.json').open('x') as output:json.dump(summary,output,indent=2)
print(json.dumps(summary),flush=True)
