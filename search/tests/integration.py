"""Real Nori, E5, kNN and reconciliation tests against an isolated TLS fixture."""
import json
import os
import sys
sys.path.insert(0, "/app")
import time
import unittest
from embedding import E5
from projection import Projection

class SearchIntegration(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.model = E5(); cls.p = Projection(cls.model)
        cls.p.request('PUT', '/canvas', {'mappings': {'properties': {'canvas-name': {'type': 'text'}, 'canvas-id': {'type': 'long'}, 'description': {'type': 'text'}}}})
        with open('/fixture/index.json') as source: definition = json.load(source)
        cls.p.request('PUT', '/canvas-search-v1', definition)
        cls.p.request('POST', '/_aliases', {'actions': [{'add': {'index': 'canvas-search-v1', 'alias': 'canvas-search', 'is_write_index': True}}]})
        cls.admin = cls.p
        cls.admin.request('PUT', '/_security/role/search_fixture', {'cluster': [], 'indices': [{
            'names': ['canvas', 'canvas-search', 'canvas-search-*'], 'privileges': ['read', 'write', 'view_index_metadata']}]})
        cls.admin.request('PUT', '/_security/user/search_fixture', {'password': os.environ['SEARCH_TEST_APP_PASSWORD'], 'roles': ['search_fixture']})
        cls.p = Projection(cls.model)
        cls.p.session.auth = ('search_fixture', os.environ['SEARCH_TEST_APP_PASSWORD'])
    def refresh(self):
        self.admin.request('POST', '/canvas/_refresh'); self.admin.request('POST', '/canvas-search/_refresh')
    def put(self, id, name, description=''):
        self.p.request('PUT', '/canvas/_doc/' + id, {'canvas-id': int(id), 'canvas-name': name, 'description': description,
            'items': {'secret': 'DO_NOT_INDEX'}, 'canvas-password-hash': 'DO_NOT_INDEX', 'people': [1]})
    def hits(self, query):
        return [hit['_id'] for hit in self.p.request('POST', '/canvas-search/_search', {'query': query})['hits']['hits']]
    def test_search_and_reconciliation(self):
        self.put('1', '인공지능 프로젝트', '머신러닝 모델과 AI를 연구하는 개발 팀')
        self.put('2', '자전거 정비', '바퀴와 브레이크 수리 안내')
        self.put('3', '공동작업 회의', '여러 사람이 함께 아이디어를 공유합니다')
        self.refresh(); self.assertEqual(self.p.synchronize(), 3); self.refresh()
        doc = self.p.request('GET', '/canvas-search/_doc/1')['_source']
        self.assertEqual(len(doc['embedding']), 384)
        self.assertNotIn('items', doc); self.assertNotIn('canvas-password-hash', doc); self.assertNotIn('people', doc)
        self.assertIn('1', self.hits({'match': {'canvas-name': 'AI'}}))
        self.assertIn('3', self.hits({'match': {'canvas-name': '협업'}}))
        self.assertIn('1', self.hits({'match': {'canvas-name.compact': '인공 지능 프로젝트'}}))
        self.assertIn('1', self.hits({'match': {'canvas-name.typo': {'query': '프로잭트', 'fuzziness': 'AUTO:3,6', 'prefix_length': 0}}}))
        tokens = self.admin.request('POST', '/canvas-search/_analyze', {'analyzer': 'ko_index', 'text': '인공지능'})['tokens']
        self.assertTrue(len(tokens) >= 2, tokens)
        vector = self.model.encode(['기계가 학습하는 기술을 연구하고 싶어요'], 'query')[0]
        result = self.p.request('POST', '/canvas-search/_search', {'size': 2, '_source': False, 'knn': {
            'field': 'embedding', 'query_vector': vector, 'k': 2, 'num_candidates': 10, 'similarity': .8,
            'filter': {'term': {'model-revision': self.model.revision}}}})['hits']['hits']
        self.assertEqual(result[0]['_id'], '1')
        self.assertEqual(self.p.synchronize(), 0) # unchanged documents do not re-embed
        self.assertEqual(self.p.request('GET', '/canvas-search/_doc/1')['_source']['updated-at'], doc['updated-at'])
        self.put('1', '한국어 검색', '형태소 분석 문서'); self.refresh()
        self.assertEqual(self.p.synchronize(), 1); self.refresh()
        self.assertEqual(self.p.request('GET', '/canvas-search/_doc/1')['_source']['canvas-name'], '한국어 검색')
        self.p.request('DELETE', '/canvas/_doc/2'); self.refresh(); self.p.synchronize(); self.refresh()
        self.assertNotIn('2', self.hits({'match_all': {}}))
        self.assertEqual(self.p.request('GET', '/canvas/_doc/1')['_source']['items']['secret'], 'DO_NOT_INDEX')
        for invalid in ['string', [], [None], [''], ['x' * 8193]]:
            with self.assertRaises(ValueError): self.model.encode(invalid)

if __name__ == '__main__': unittest.main(verbosity=2)
