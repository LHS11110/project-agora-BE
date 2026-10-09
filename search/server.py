import hmac
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
import os
import ssl
import threading
import time
from embedding import E5
from projection import Projection


def main():
    token = os.environ['CPP_INTERNAL_API_TOKEN']
    if len(token.encode()) < 32: raise ValueError('Internal token must contain at least 32 bytes')
    interval = int(os.environ.get('SEARCH_SYNC_SECONDS', '30'))
    if not 1 <= interval <= 3600: raise ValueError('Sync interval must be 1..3600 seconds')
    model = E5()
    projection = Projection(model)
    status = {'ready': False, 'synced-at': None, 'updated': 0}
    def reconcile():
        while True:
            try:
                status['updated'] = projection.synchronize()
                status['ready'] = True; status['synced-at'] = int(time.time())
            except Exception as error:
                # Never render response bodies, credentials, document text or tokens.
                print('Search projection synchronization failed: ' + type(error).__name__, flush=True)
            time.sleep(interval)
    threading.Thread(target=reconcile, daemon=True).start()
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args): pass
        def reply(self, code, value):
            raw = json.dumps(value).encode(); self.send_response(code)
            self.send_header('Content-Type', 'application/json'); self.send_header('Content-Length', str(len(raw)))
            self.end_headers(); self.wfile.write(raw)
        def do_GET(self):
            if self.path != '/health': return self.reply(404, {})
            return self.reply(200 if status['ready'] else 503, dict(status, **{'model-revision': model.revision}))
        def do_POST(self):
            if self.path != '/embed': return self.reply(404, {})
            if not hmac.compare_digest(self.headers.get('X-Agora-Internal-Token', '').encode(), token.encode()): return self.reply(403, {})
            try:
                length = int(self.headers.get('Content-Length', '0'))
                if not 1 <= length <= 65536: return self.reply(413, {})
                body = json.loads(self.rfile.read(length))
                vectors = model.encode(body['texts'], 'query')
                return self.reply(200, {'vectors': vectors, 'model_revision': model.revision})
            except (KeyError, TypeError, ValueError): return self.reply(400, {})
            except TimeoutError: return self.reply(503, {})
            except Exception: return self.reply(500, {})
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); context.minimum_version = ssl.TLSVersion.TLSv1_3
    context.load_cert_chain(os.environ['SERVICE_TLS_CERT'], os.environ['SERVICE_TLS_KEY'])
    class TLSServer(HTTPServer):
        def get_request(self):
            connection, address = self.socket.accept()
            connection.settimeout(5)
            try:
                return context.wrap_socket(connection, server_side=True), address
            except Exception:
                connection.close()
                raise
        def handle_error(self, request, client_address):
            print('Embedding connection failed', flush=True)
    server = TLSServer(('0.0.0.0', 8445), Handler)
    server.serve_forever()


if __name__ == '__main__': main()
