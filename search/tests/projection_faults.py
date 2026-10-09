import sys
sys.path.insert(0, '/app')
import unittest
from unittest.mock import Mock
from projection import Projection, fingerprint

class ProjectionFailureTests(unittest.TestCase):
    def test_failed_existence_check_cannot_delete_even_confirmed_missing_neighbor(self):
        projection = Projection.__new__(Projection)
        projection.source = 'canvas'; projection.index = 'canvas-search'; projection.model = Mock()
        projection.scan = Mock(side_effect=[[], [[{'_id': '1'}, {'_id': '2'}]]])
        projection.request = Mock(return_value={'docs': [{'_id': '1', 'found': False}, {'_id': '2', 'error': {'type': 'unavailable_shards_exception'}}]})
        with self.assertRaises(RuntimeError): projection.synchronize()
        self.assertFalse(any(call.args[0] == 'DELETE' for call in projection.request.call_args_list))
    def test_partial_source_scan_never_reaches_deletion(self):
        projection = Projection.__new__(Projection)
        projection.source = 'canvas'; projection.index = 'canvas-search'; projection.model = Mock(revision='test')
        def partial(*args):
            if False: yield []
            raise RuntimeError('Shard unavailable')
        projection.scan = Mock(side_effect=partial); projection.request = Mock()
        with self.assertRaises(RuntimeError): projection.synchronize()
        projection.request.assert_not_called()
    def test_unknown_existence_is_not_treated_as_missing(self):
        projection = Projection.__new__(Projection)
        projection.source = 'canvas'; projection.index = 'canvas-search'; projection.model = Mock()
        projection.scan = Mock(side_effect=[[], [[{'_id': '1'}]]])
        projection.request = Mock(return_value={'docs': [{'_id': '1'}]})
        self.assertEqual(projection.synchronize(), 0)
        self.assertFalse(any(call.args[0] == 'DELETE' for call in projection.request.call_args_list))
    def test_private_content_never_changes_search_fingerprint(self):
        source = {'canvas-id': 1, 'canvas-name': '회의', 'description': '협업'}
        private = dict(source, items={'private': 'secret'}, people=[1,2], **{'canvas-password-hash': 'secret'})
        self.assertEqual(fingerprint(source, 'A'), fingerprint(private, 'A'))
        self.assertNotEqual(fingerprint(source, 'A'), fingerprint(source, 'B'))

if __name__ == '__main__': unittest.main(verbosity=2)
