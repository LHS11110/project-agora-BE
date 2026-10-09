"""Bounded metadata-only reconciliation. All ES requests pass through Wall's TLS TCP proxy."""
import hashlib
import json
import os
from urllib.parse import quote, urlsplit
import requests
from requests.adapters import HTTPAdapter
import ssl
import time

FIELDS = ['canvas-name', 'canvas-id', 'description']


class TLSAdapter(HTTPAdapter):
    def init_poolmanager(self, *args, **kwargs):
        context = ssl.create_default_context(cafile=os.environ['ES_CA_CERT'])
        context.minimum_version = ssl.TLSVersion.TLSv1_3
        kwargs['ssl_context'] = context
        return super().init_poolmanager(*args, **kwargs)


def fingerprint(source, revision):
    content = {'canvas-name': source.get('canvas-name') or '', 'description': source.get('description') or '',
               'canvas-id': source.get('canvas-id'), 'model-revision': revision}
    return hashlib.sha256(json.dumps(content, ensure_ascii=False, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


class Projection:
    def __init__(self, model):
        self.model = model
        self.source = os.environ.get('ES_INDEX', 'canvas')
        self.index = os.environ.get('ES_SEARCH_INDEX', 'canvas-search')
        if self.source == self.index: raise ValueError('Search projection must be separate')
        self.base = 'https://' + os.environ.get('ES_HOST', 'agora-elasticsearch') + ':' + os.environ.get('ES_PORT', '9200')
        self.session = requests.Session(); self.session.trust_env = False
        self.session.verify = os.environ['ES_CA_CERT']
        self.session.auth = (os.environ['ES_USER_NAME'], os.environ['ES_USER_PASSWORD'])
        self.session.mount('https://', TLSAdapter())
    def request(self, method, path, body=None):
        response = self.session.request(method, self.base + path, json=body, timeout=(3, 15), allow_redirects=False)
        if response.status_code >= 300: raise RuntimeError('ES metadata request failed: ' + str(response.status_code))
        return response.json() if response.content else {}
    def scan(self, index, fields):
        pit = self.request('POST', '/' + quote(index, safe='') + '/_pit?keep_alive=2m')['id']
        after = None
        try:
            while True:
                body = {'size': 64, '_source': fields, 'query': {'match_all': {}},
                        'sort': ['_shard_doc'], 'pit': {'id': pit, 'keep_alive': '2m'}, 'track_total_hits': False}
                if after is not None: body['search_after'] = after
                response = self.request('POST', '/_search', body)
                pit = response.get('pit_id', pit)
                if response.get('timed_out') or response.get('_shards', {}).get('failed', 0): raise RuntimeError('Incomplete metadata scan')
                hits = response['hits']['hits']
                if not hits: break
                yield hits
                after = hits[-1]['sort']
        finally:
            try: self.request('DELETE', '/_pit', {'id': pit})
            except Exception: pass
    def synchronize(self):
        updated = 0
        for hits in self.scan(self.source, FIELDS):
            ids = [hit['_id'] for hit in hits]
            existing = self.request('POST', '/' + quote(self.index, safe='') + '/_mget', {'docs': [{'_id': value, '_source': ['fingerprint', 'model-revision']} for value in ids]})['docs']
            if any('error' in doc for doc in existing): raise RuntimeError('Projection read incomplete')
            previous = {doc['_id']: doc.get('_source', {}) for doc in existing}
            changed = [hit for hit in hits if previous.get(hit['_id'], {}).get('fingerprint') != fingerprint(hit['_source'], self.model.revision)]
            for offset in range(0, len(changed), 4):
                batch = changed[offset:offset + 4]
                texts = [((hit['_source'].get('canvas-name') or '') + '\n' + (hit['_source'].get('description') or '')).strip()[:8192] or '빈 캔버스' for hit in batch]
                vectors = self.model.encode(texts, 'passage')
                for hit, vector in zip(batch, vectors):
                    source = {key: hit['_source'].get(key) for key in FIELDS}
                    source.update(fingerprint=fingerprint(hit['_source'], self.model.revision), embedding=vector)
                    source['model-revision'] = self.model.revision
                    source['updated-at'] = int(time.time() * 1000)
                    self.request('PUT', '/' + quote(self.index, safe='') + '/_doc/' + quote(hit['_id'], safe=''), source)
                    updated += 1
        # Delete only confirmed missing source documents; never use a partial source scan as a deletion list.
        for hits in self.scan(self.index, ['fingerprint']):
            originals = self.request('POST', '/' + quote(self.source, safe='') + '/_mget', {'docs': [{'_id': hit['_id'], '_source': False} for hit in hits]})['docs']
            if any('error' in doc for doc in originals): raise RuntimeError('Source existence check incomplete')
            for doc in originals:
                if doc.get('found') is False:
                    self.request('DELETE', '/' + quote(self.index, safe='') + '/_doc/' + quote(doc['_id'], safe=''))
        return updated
