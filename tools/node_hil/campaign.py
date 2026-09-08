"""Application requests and observation only; MCU owners run every RF protocol."""
import argparse
import json
import struct
import time
from pathlib import Path
from console import Board
from hardware import require
from field_evidence import peer_status, flow_status


def status(board):
    b = board.call('H')
    require(len(b) in (40, 72, 96), 'Malformed node status')
    result = dict(zip(('running', 'joined', 'clock', 'fault_injection'), b[:4]))
    result.update(zip(('authenticated', 'members', 'relay_owned', 'application_records'),
                      struct.unpack('>4H', b[4:12])))
    result.update(zip(('tx_done', 'rx_frames', 'discarded_staging', 'heap_free',
                       'heap_minimum', 'stack_minimum', 'fault'),
                      struct.unpack('>7I', b[12:40])))
    if len(b) >= 72:
        result.update(zip(('handshakes', 'rejected_frames', 'handshake_peer',
                           'handshake_stage', 'initiator', 'channel_busy',
                           'crc_errors', 'header_errors', 'radio_timeouts',
                           'last_receive_result'), struct.unpack('>IIHBBIIIIi', b[40:72])))
    if len(b) == 96:
        result.update(zip(('pending_route_epoch', 'prepared_route_epoch', 'pending_phase',
                           'prepared_mask', 'proof_mask', 'authority_routes', 'local_routes',
                           'effective_routes', 'wanted_routes'), struct.unpack('>QQ6BH', b[72:])))
    return result


def main():
    from manage import control_status
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('evidence', type=Path)
    p.add_argument('--sequence', type=int, default=1)
    p.add_argument('--count', type=int, default=1, help='One to three new messages without restarting owners')
    p.add_argument('--seconds', type=int, default=180)
    p.add_argument('--block-bootstrap', action='store_true',
                   help='Also discard direct 1<->3 bootstrap frames; relayed bootstrap remains available')
    p.add_argument('--diagnostics', action='store_true', help='Read public control reception counters')
    p.add_argument('--restart-relay', action='store_true',
                   help='Reset relay and receiver after new custody, then recover the same message')
    p.add_argument('--restart-source', action='store_true',
                   help='Reset source/authority after relay custody and verify durable pending recovery')
    p.add_argument('--drain', action='store_true',
                   help='Drain relay, wait for dependency removal and deliver with the direct path available')
    p.add_argument('--existing-message', type=bytes.fromhex,
                   help='Recover a pending source receipt for an already committed receiver record')
    args = p.parse_args()
    require(1 <= args.count <= 3 and args.sequence + args.count <= 2**32, 'Invalid bounded message count')
    require(args.count == 1 or not (args.existing_message or args.restart_relay or args.restart_source or args.drain), 'Burst requires new traffic')
    require(100 <= args.seconds <= 570 and 0 < args.sequence < 2**32,
            'Duration must be 100..570 seconds and sequence must be uint32 nonzero')
    require(args.existing_message is None or len(args.existing_message) == 16,
            'Existing message must be a 16-byte Core ID')
    require(sum(bool(x) for x in (args.existing_message, args.restart_relay, args.restart_source, args.drain)) <= 1,
            'Select one recovery/removal case')
    require(not (args.block_bootstrap and args.drain), 'Bootstrap isolation cannot test direct-path drain')
    def fault_mask(value, node):
        return bytes((value | (4 if args.block_bootstrap and node in (1, 3) else 0),))
    args.evidence.mkdir(parents=True, exist_ok=True)
    boards = []
    result = {'scope': 'actual three-MCU autonomous Core/application/relay integration',
              'software_rx_loss': 'none' if args.drain else
                  'direct 1<->3 NS and NB frames; relayed bootstrap available' if args.block_bootstrap else
                  'direct 1<->3 NS frames only; NB control stays available',
              'physical_range_claim': False, 'started': time.time(), 'snapshots': [],
              'message_count': args.count, 'deliveries': []}
    with (args.evidence / 'console.jsonl').open('x', encoding='utf-8') as log:
        try:
            for i in (1, 2, 3):
                boards.append(Board(i, log))
            for board in boards:
                board.call('X')
                board.call('P')
                board.call('F', fault_mask(0 if args.drain else
                           3 if (args.restart_relay or args.restart_source) and board.node == 3 else 1, board.node))
                board.call('G', ((args.seconds+30)*1000).to_bytes(4, 'big'))
            deadline = time.monotonic() + args.seconds
            message = None
            restarted = False
            while True:
                snapshots = [status(b) for b in boards]
                if not result['snapshots']:
                    result['initial_application_records'] = snapshots[2]['application_records']
                    if args.restart_relay or args.restart_source:
                        require(snapshots[1]['relay_owned'] == 0,
                                'Restart case requires quiescent relay custody before submission')
                    if args.drain:
                        boards[1].call('D', b'\x01')
                    if args.existing_message is not None:
                        prior = boards[0].call('Q', args.existing_message)
                        require(len(prior) == 2 and prior[0] == 0,
                                'Receipt recovery requires a still-active source message')
                        require(snapshots[2]['application_records'] > 0,
                                'Receipt recovery requires prior receiver application evidence')
                        result['initial_source_evidence'] = prior.hex()
                if len(result['snapshots']) % 5 == 0:
                    for board in boards:
                        if args.diagnostics:
                            snapshots[board.node-1]['controls'] = control_status(board)
                        for peer in (1,2,3):
                            if board.node != peer:
                                snapshots[board.node-1][f'peer_{peer}'] = peer_status(board, peer)
                        for source, target in ((1, 3), (3, 1)):
                            snapshots[board.node-1][f'route_{source}_{target}'] = flow_status(board, source, target)
                result['snapshots'].append({'time': time.time(), 'nodes': snapshots})
                print(json.dumps(snapshots), flush=True)
                require(all(s['running'] and not s['fault'] for s in snapshots), snapshots)
                removable = not args.drain or boards[1].call('D') == b'\x01'
                if message is None and all(s['joined'] and s['clock'] and
                                           s['members'] == 3 for s in snapshots) and removable:
                    message = boards[0].call('S', struct.pack('>HI', 3, args.sequence + len(result['deliveries'])))
                    require(len(message) == 16, 'Malformed application message ID')
                    require(args.existing_message is None or message == args.existing_message,
                            'Existing Core ID does not match this target, payload and sequence')
                    result['message_id'] = message.hex()
                if message:
                    evidence = boards[0].call('Q', message)
                    require(len(evidence) == 2, 'Malformed application evidence')
                    if (args.restart_relay or args.restart_source) and not restarted and snapshots[1]['relay_owned']:
                        require(evidence[0] == 0 and snapshots[2]['application_records'] ==
                                result['initial_application_records'], 'Require pending source and held receiver')
                        result['before_reset'] = {'source': evidence.hex(), 'nodes': snapshots}
                        result['reset_nodes'] = [1] if args.restart_source else [2, 3]
                        for index in (node-1 for node in result['reset_nodes']):
                            boards[index].call('R')
                            boards[index].close()
                            time.sleep(2)
                            limit = time.monotonic() + 20
                            while True:
                                try:
                                    boards[index] = Board(index+1, log)
                                    require(not status(boards[index])['running'], 'Reset must boot stopped')
                                    break
                                except (OSError, RuntimeError, TimeoutError):
                                    if time.monotonic() >= limit:
                                        raise
                                    boards[index].close()
                                    time.sleep(0.5)
                            boards[index].call('F', fault_mask(3 if index == 2 else 1, index + 1))
                            boards[index].call('G', ((args.seconds+30)*1000).to_bytes(4, 'big'))
                        after = [status(b) for b in boards]
                        require(after[1]['relay_owned'] >= snapshots[1]['relay_owned'],
                                'Reset lost durable relay custody')
                        require(after[2]['application_records'] == result['initial_application_records'],
                                'Held receiver changed its application ledger')
                        result['after_reset'] = after
                        result['source_after_reset'] = boards[0].call('Q', message).hex()
                        require(result['source_after_reset'] == '0000', 'Pending Core message changed across reset')
                        boards[2].call('F', fault_mask(1, 3))
                        restarted = True
                    if evidence == b'\x01\x05':
                        require(not (args.restart_relay or args.restart_source) or restarted, 'Required reset did not occur')
                        if args.drain:
                            require(removable and snapshots[1]['relay_owned'] == 0,
                                    'Application completion did not preserve removal readiness')
                            result['relay_removal_ready'] = True
                        growth = 0 if args.existing_message is not None else len(result['deliveries']) + 1
                        require(snapshots[2]['application_records'] == result['initial_application_records'] + growth,
                            'Use a fresh sequence; an old completed message is not new delivery evidence')
                        result['application_record_growth'] = growth
                        result['deliveries'].append({'id': message.hex(), 'accepted_at': time.time()})
                        if len(result['deliveries']) < args.count:
                            message = None
                            continue
                        result['source_outcome'] = 'SATISFIED/APPLICATION_ACCEPTED'
                        result['result'] = 'PASS'
                        break
                # Observe once at the boundary; a final polling sleep is not
                # evidence that the device remained pending during that gap.
                require(time.monotonic() < deadline, 'Autonomous delivery deadline')
                time.sleep(max(0, min(4, deadline-time.monotonic())))
            require(result.get('result') == 'PASS', 'Autonomous delivery deadline')
        except BaseException as error:
            result['result'], result['error'] = 'FAIL', repr(error)
            raise
        finally:
            result['finished'] = time.time()
            if args.drain and len(boards) >= 2:
                try:
                    boards[1].call('D', b'\x00')
                    require(boards[1].call('D') == b'\x00', 'Resume left removal readiness set')
                    result['relay_resumed'] = True
                except Exception as error:
                    result.setdefault('stop_errors', []).append({'node': 2, 'resume': repr(error)})
            for board in boards:
                try:
                    board.call('X', timeout=3)
                except Exception as error:
                    result.setdefault('stop_errors', []).append({'node': board.node, 'error': repr(error)})
                finally:
                    try:
                        board.close()
                    except Exception as error:
                        result.setdefault('stop_errors', []).append({'node': board.node, 'error': repr(error)})
            if result.get('stop_errors'):
                result['result'] = 'FAIL'
            (args.evidence / 'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
            require(not result.get('stop_errors'), 'One or more boards could not be stopped/closed')


if __name__ == '__main__':
    main()
