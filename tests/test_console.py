"""Late replies are retained as evidence without satisfying another command."""
import io
import json
import sys
import unittest
import tempfile
from pathlib import Path
from unittest.mock import Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/node_hil'))
from console import Board
import field_evidence
import extension_evidence


class DelayedReply(unittest.TestCase):
    def test_unmatched_stop_is_not_configuration_evidence(self):
        rows = [{'time': n, 'node': n, 'command': 'X', 'request': None,
                 'result': 0, 'response': ''} for n in (1, 2, 3)]
        rows.append(dict(rows[0], time=4, command='G', request='00000001'))
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            capture = folder / 'console.jsonl'
            capture.write_text('\n'.join(map(json.dumps, rows))+'\n')
            self.assertFalse(field_evidence.analyze(folder)['all_stopped'])
            self.assertFalse(extension_evidence.analyze(capture)['all_stopped'])

    def test_late_failure_does_not_replace_current_reply(self):
        board = Board.__new__(Board)
        board.node, board.log, board.port = 2, io.StringIO(), Mock()
        board.port.readline.side_effect = [b'NODE E -2 \n', b'NODE H 0 aa\n']
        self.assertEqual(board.call('H'), b'\xaa')
        board.port.write.assert_called_once_with(b'H \n')
        late, current = map(json.loads, board.log.getvalue().splitlines())
        self.assertEqual((late['command'], late['request'], late['result']), ('E', None, -2))
        self.assertEqual((current['command'], current['request']), ('H', ''))
