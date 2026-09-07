"""Physical A--B--C encrypted Relay custody. USB owns the test protocol.
Downstream ACKs are test-owner claims after a durable host observation, not
MCU Core/application receipts. No original device data is backed up or erased.
"""
import hashlib,json,os,re,sys
from datetime import datetime,timezone
import campaign as c
import hardware as hw
hw.IDENTITIES['c']=('COM7','E0:72:A1:D7:77:28')
label=sys.argv[1]
assert re.fullmatch(r'relay-20260908-[a-z0-9-]+',label)
boards={}; records={}; traces=[]; checks=[]; log_names={name:[] for name in ('a','b','c')}
audit=(hw.EVIDENCE/f'{label}-ownership.jsonl').open('x')

def record(kind,**values):
    audit.write(json.dumps({'utc':datetime.now(timezone.utc).isoformat(),'kind':kind,**values})+'\n')
    audit.flush();os.fsync(audit.fileno())

def passed(name):
    checks.append(name);print('PASS',name,flush=True)

original_air=c.air

def air(sender,receiver,frame):
    # The third participant can overhear the other pair; retire that bounded
    # test-only RX before asking its transmitter to run. Driver preserves
    # pending RX rather than silently overwriting it during TX.
    sender.call('t')
    receiver.call('t')
    received=original_air(sender,receiver,frame)
    traces.append({'from':sender.name,'to':receiver.name,'hex':frame.hex()})
    return received
c.air=air

def boot(name,suffix=''):
    board=c.Board(name,label+suffix);board.id={'a':1,'b':2,'c':3}[name]
    board.key=board.call('I');boards[name]=board
    log_names[name].append(f'{label+suffix}-{name}.jsonl')
    return board

def select(board,peer,hop):
    board.call('Y',peer.id.to_bytes(2,'big')+bytes([hop]))

def handshake(a,b):
    assert a.id<b.id
    a.call('P',b.id.to_bytes(2,'big')+b.key)
    b.call('P',a.id.to_bytes(2,'big')+a.key)
    message=a.call('E')
    for kind in range(1,5):
        s,r=(a,b) if kind%2 else (b,a)
        message=r.call('E',c.fragment(s,r,message,kind))
    assert not message
    af,bf=a.call('O'),b.call('O');assert af==bf and len(af)==16
    return af

def wrap(sender,peer,data):
    select(sender,peer,1);return sender.call('S',data)

def unwrap(receiver,peer,frame):
    select(receiver,peer,1);return receiver.call('U',frame)

def new_packet(source,target,number):
    plain=number.to_bytes(4,'big')+bytes([source.id])*100
    select(source,target,0);cipher=source.call('S',plain)
    assert len(cipher)==144
    header=bytearray(48);header[:6]=b'NR\x01\x00\x03\x02'
    header[6:8]=len(cipher).to_bytes(2,'big')
    header[8:14]=source.id.to_bytes(2,'big')+bytes([0,2])+target.id.to_bytes(2,'big')
    header[18:26]=(1).to_bytes(8,'big');header[26:42]=hashlib.sha256(cipher).digest()[:16]
    packet=bytes(header)+cipher;records[packet[26:42]]=(source,target,plain,packet)
    record('source_owns_test_packet',id=packet[26:42].hex(),plain=plain.hex(),packet=packet.hex())
    return packet

def done(packet):
    return packet[:3]+bytes([1])+packet[4:]

def admit(source,b,packet,expected=0):
    frame=air(source,b,wrap(source,b,packet))
    reply=b.call('j',frame,expected=expected)
    if expected==0:
        assert unwrap(source,b,air(b,source,reply))==done(packet)
        record('relay_committed_reply',id=packet[26:42].hex())

def ack(target,b,packet,expected=0):
    b.call('k',air(target,b,wrap(target,b,done(packet))),expected=expected)

def forward(b):
    rc,frame=b.call('n',expected=None)
    assert rc==0,rc
    target=next(x for x in boards.values() if x.id==int.from_bytes(frame[6:8],'big'))
    packet=unwrap(target,b,air(b,target,frame))
    source,expected_target,plain,original=records[packet[26:42]]
    assert target is expected_target and packet==original
    select(target,source,0);assert target.call('U',packet[48:])==plain
    record('destination_test_payload_committed_on_host',id=packet[26:42].hex(),plain=plain.hex())
    ack(target,b,packet)
    record('relay_hop_retired',id=packet[26:42].hex())
    return packet

result={'label':label,'started':datetime.now(timezone.utc).isoformat()}
try:
    a,b,d=[boot(name) for name in ('a','b','c')]
    fingerprints=[handshake(a,d),handshake(a,b),handshake(b,d)]
    assert len(set(fingerprints))==3
    b.call('q',expected=-7);b.call('n',expected=-7)
    passed('three real EDHOC pairs with independent end-to-end and hop contexts')
    for number in range(20):
        source,target=(a,d) if number%2==0 else (d,a)
        packet=new_packet(source,target,number)
        admit(source,b,packet);assert forward(b)==packet;b.call('q',expected=-7)
    passed('10 encrypted opaque packets each direction through physical Relay; maximum 232-byte hop frame')
    packet=new_packet(a,d,20)
    bad=bytearray(wrap(a,b,packet));bad[-1]^=1
    rc,_=b.call('j',air(a,b,bytes(bad)),expected=None);assert rc!=0
    wrong_epoch=packet[:18]+(2).to_bytes(8,'big')+packet[26:]
    admit(a,b,wrong_epoch,expected=-12)
    admit(d,b,packet,expected=-12)
    b.call('q',expected=-7)
    admit(a,b,packet);admit(a,b,packet)
    ack(d,b,packet,expected=-12)  # No forwarding attempt yet.
    assert b.call('q')==packet
    select(b,a,0);rc,_=b.call('U',packet[48:],expected=None);assert rc!=0
    staged=b.call('n')  # Withhold the actual transmission; ownership must remain.
    assert staged and b.call('q')==packet
    ack(a,b,packet,expected=-12)
    ack(d,b,wrong_epoch,expected=-12)
    b.call('d',bytes([1]));b.call('r',expected=-8)
    passed('tamper/wrong source/stale epoch/premature and wrong ACK rejected; duplicate custody and withheld TX retained')
    b.close();old_key=b.key;b=boot('b','-reboot');assert b.key!=old_key
    assert b.call('q')==packet;b.call('n',expected=-7);b.call('r',expected=-8)
    handshake(a,b);handshake(b,d)
    assert forward(b)==packet
    b.call('q',expected=-7);b.call('r')
    ack(d,b,packet,expected=-6)
    passed('actual Relay MCU reset: opaque custody and drain restored; fresh hops resume unchanged end-to-end ciphertext')
    blocked=new_packet(a,d,21);admit(a,b,blocked,expected=-8)
    b.call('d',bytes([0]));admit(a,b,blocked);forward(b)
    pending=[new_packet(a,d,i) for i in range(22,31)]
    for packet in pending[:8]:admit(a,b,packet)
    admit(a,b,pending[8],expected=-4)
    b.call('d',bytes([1]));b.call('r',expected=-8)
    delivered={forward(b)[26:42] for _ in range(8)}
    assert delivered=={p[26:42] for p in pending[:8]}
    b.call('r');b.call('d',bytes([0]))
    admit(a,b,pending[8]);assert forward(b)==pending[8]
    b.call('d',bytes([1]));b.call('Z');b.call('q',expected=-7);b.call('r')
    passed('8-slot physical Flash custody saturation backpressures without loss; drain survives reopen and removal is ready only when empty')
    for board in boards.values():
        health=board.call('H');assert int.from_bytes(health[:4],'big')>=4096
    result['result']='PASS'
except BaseException as error:
    result.update(result='FAIL',error=repr(error));raise
finally:
    for board in boards.values():board.close()
    audit.close();result.update(checks=checks,rf_frames=c.rf_frames,traces=traces,logs=log_names,finished=datetime.now(timezone.utc).isoformat())
    with (hw.EVIDENCE/f'{label}-result.json').open('x') as output:json.dump(result,output,indent=2)
