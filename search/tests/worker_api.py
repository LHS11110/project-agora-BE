import os
import sys
import time
import ssl
import unittest
import requests
sys.path.insert(0, '/app')
from projection import TLSAdapter

class WorkerApi(unittest.TestCase):
    def test_actual_gateway_worker(self):
        client = requests.Session(); client.trust_env = False
        client.verify = os.environ['ES_CA_CERT']; client.mount('https://', TLSAdapter())
        response = None
        for attempt in range(60):
            try:
                response = client.get('https://agora-spring:8445/health', timeout=3)
                if response.status_code == 200: break
            except requests.RequestException: pass
            time.sleep(.2)
        self.assertIsNotNone(response)
        self.assertEqual(response.status_code, 200)
        self.assertTrue(response.json()['ready'])
        url = 'https://agora-nginx:8444/internal/search/embedding'
        headers = {'X-Agora-Internal-Token': os.environ['CPP_INTERNAL_API_TOKEN']}
        for attempt in range(60):
            try:
                response = client.post(url, headers=headers, json={'texts': ['인공지능 연구']}, timeout=5)
                if response.status_code == 200: break
            except requests.RequestException: pass
            time.sleep(.2)
        self.assertEqual(response.status_code, 200)
        vector = response.json()['vectors'][0]; self.assertEqual(len(vector), 384)
        self.assertAlmostEqual(sum(value*value for value in vector), 1, places=4)
        self.assertEqual(client.post(url, json={'texts': ['query']}, timeout=5).status_code, 403)
        self.assertEqual(client.post(url, headers=headers, json={'texts': 'invalid'}, timeout=5).status_code, 400)
        self.assertEqual(client.post('https://agora-nginx/internal/search/embedding', headers=headers, json={'texts':['query']}, timeout=5).status_code, 404)
        context = ssl.create_default_context(cafile=os.environ['ES_CA_CERT']); context.maximum_version = ssl.TLSVersion.TLSv1_2
        import socket
        with self.assertRaises(ssl.SSLError):
            with socket.create_connection(('agora-nginx',8444),timeout=3) as sock:
                context.wrap_socket(sock, server_hostname='agora-nginx')

if __name__ == '__main__': unittest.main(verbosity=2)
