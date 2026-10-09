#!/usr/bin/env python3
"""Disposable containers only: TLS 1.3 ES through an isolated TCP storage broker.
Requires prebuilt search and Nori images. Never touches Agora deployment containers.
"""
import os
from pathlib import Path
import secrets
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
DB = ROOT.parent / 'project-agora-DB'
def call(args, **kwargs):
    result = subprocess.run(args, **kwargs)
    if result.returncode: raise RuntimeError('Isolated fixture command failed; inspect the preceding test error')
    return result
def main():
    suffix = secrets.token_hex(5); network = 'agora-search-test-' + suffix
    es = network + '-es'; proxy = network + '-proxy'; worker = network + '-worker'; gateway = network + '-nginx'
    password = secrets.token_hex(24); app_password = secrets.token_hex(24)
    with tempfile.TemporaryDirectory(prefix='agora-search-test-') as directory:
        root = Path(directory); root.chmod(0o755)
        (root/'index.json').write_bytes((DB/'elasticsearch/search/index.json').read_bytes())
        call(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1', '-subj', '/CN=Search fixture CA',
              '-addext', 'basicConstraints=critical,CA:TRUE', '-addext', 'keyUsage=critical,keyCertSign,cRLSign', '-addext', 'subjectKeyIdentifier=hash',
              '-keyout', str(root/'ca.key'), '-out', str(root/'ca.pem')], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        call(['openssl', 'req', '-newkey', 'rsa:2048', '-nodes', '-subj', '/CN=agora-elasticsearch',
              '-keyout', str(root/'server.key'), '-out', str(root/'server.csr')], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        (root/'extensions').write_text('subjectAltName=DNS:agora-elasticsearch,DNS:localhost,DNS:agora-spring,DNS:agora-nginx\nextendedKeyUsage=serverAuth\nbasicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\nsubjectKeyIdentifier=hash\nauthorityKeyIdentifier=keyid:always,issuer:always\n')
        call(['openssl', 'x509', '-req', '-in', str(root/'server.csr'), '-CA', str(root/'ca.pem'), '-CAkey', str(root/'ca.key'),
              '-CAcreateserial', '-days', '1', '-extfile', str(root/'extensions'), '-out', str(root/'server.pem')],
              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for name in ('server.key','server.pem','ca.pem'): (root/name).chmod(0o644)
        (root/'haproxy.cfg').write_text(f'global\n  maxconn 64\ndefaults\n  mode tcp\n  timeout connect 3s\n  timeout client 30s\n  timeout server 30s\nlisten es\n  bind :9200\n  server es {es}:9200\n')
        call(['docker','network','create','--internal',network],stdout=subprocess.DEVNULL)
        try:
            call(['docker','run','-d','--name',es,'--network',network,'--memory','1536m',
                '-e','discovery.type=single-node','-e','ES_JAVA_OPTS=-Xms512m -Xmx512m','-e','xpack.security.enabled=true',
                '-e','xpack.security.http.ssl.enabled=true','-e','xpack.security.http.ssl.supported_protocols=TLSv1.3',
                '-e','xpack.security.http.ssl.key=/usr/share/elasticsearch/config/fixture/server.key',
                '-e','xpack.security.http.ssl.certificate=/usr/share/elasticsearch/config/fixture/server.pem',
                '-e','xpack.security.http.ssl.certificate_authorities=/usr/share/elasticsearch/config/fixture/ca.pem',
                '-e','ELASTIC_PASSWORD='+password,'-v',str(root)+':/usr/share/elasticsearch/config/fixture:ro',
                '-v',str(DB/'elasticsearch/search/synonyms.txt')+':/usr/share/elasticsearch/config/analysis/canvas-synonyms.txt:ro',
                'project-agora-elasticsearch:8.19.22-nori'],stdout=subprocess.DEVNULL)
            call(['docker','run','-d','--name',proxy,'--network',network,'--network-alias','agora-elasticsearch',
                '-v',str(root/'haproxy.cfg')+':/usr/local/etc/haproxy/haproxy.cfg:ro','haproxy:3.2-alpine'],stdout=subprocess.DEVNULL)
            client = ['docker','run','--rm','--network',network,'--memory','2g','-v',str(root)+':/fixture:ro',
                '-v',str(ROOT/'search/tests')+':/tests:ro',
                '-e','ES_CA_CERT=/fixture/ca.pem','-e','ES_USER_NAME=elastic','-e','ES_USER_PASSWORD='+password,
                '-e','SEARCH_TEST_APP_PASSWORD='+app_password,
                '--entrypoint','python','project-agora-search:local']
            for attempt in range(30):
                ready = subprocess.run(client+['-c',"from projection import Projection; p=Projection(None); p.request('GET','/_cluster/health')"],
                    stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
                if ready.returncode == 0: break
                time.sleep(2)
            else: raise RuntimeError('Isolated Elasticsearch did not become ready: ' + ready.stderr[-2000:])
            call(client+['/tests/projection_faults.py'])
            call(client+['/tests/integration.py'])
            token = secrets.token_hex(24)
            call(['docker','run','-d','--name',worker,'--network',network,'--network-alias','agora-search-embedding','--network-alias','agora-spring',
                '--memory','2g','-v',str(root)+':/fixture:ro','-e','ES_CA_CERT=/fixture/ca.pem',
                '-e','ES_USER_NAME=search_fixture','-e','ES_USER_PASSWORD='+app_password,'-e','CPP_INTERNAL_API_TOKEN='+token,
                '-e','SERVICE_TLS_CERT=/fixture/server.pem','-e','SERVICE_TLS_KEY=/fixture/server.key',
                '-e','SEARCH_SYNC_SECONDS=1','project-agora-search:local'],stdout=subprocess.DEVNULL)
            wall = ROOT.parent / 'project-agora-Wall'
            call(['docker','run','-d','--name',gateway,'--network',network,'--network-alias','agora-nginx',
                '-e','FRONTEND_MODE=production','-e','NGINX_HTTPS_PORT=443','-e','BROKER_TLS_SERVER_NAME=agora-broker',
                '-e','NGINX_ENVSUBST_FILTER=^(FRONTEND_MODE|NGINX_HTTPS_PORT|BROKER_TLS_SERVER_NAME)$',
                '-v',str(wall/'nginx/agora.conf.example')+':/etc/nginx/templates/default.conf.template:ro',
                '-v',str(wall/'nginx/cpp-routes.conf')+':/etc/nginx/routes/cpp-routes.conf:ro',
                '-v',str(root/'server.pem')+':/etc/nginx/ssl/agora/fullchain.pem:ro',
                '-v',str(root/'server.key')+':/etc/nginx/ssl/agora/privkey.pem:ro',
                '-v',str(root/'ca.pem')+':/etc/nginx/ssl/agora/ca.pem:ro','nginx:stable-alpine'],stdout=subprocess.DEVNULL)
            api_client = client[:-3] + ['-e','CPP_INTERNAL_API_TOKEN='+token] + client[-3:]
            call(api_client+['/tests/worker_api.py'])
            call(['docker','exec',gateway,'nginx','-t'])
        finally:
            subprocess.run(['docker','rm','-f',gateway,worker,proxy,es],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            subprocess.run(['docker','network','rm',network],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)

if __name__ == '__main__': main()
