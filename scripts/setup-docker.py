#!/usr/bin/env python3
"""Prepare verified-TLS Docker settings using manually supplied certificates."""
from __future__ import annotations

import argparse
import importlib.util
from pathlib import Path
import secrets
import subprocess
import sys
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]


def module_at(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def verify_bundle(db, tls, configs):
    def verify(directory, cert, key, ca, hosts, label):
        ca_path = directory / ca
        if not ca_path.is_file():
            raise ValueError(f'Missing manually supplied CA: {ca_path}. See TLS_SETUP.md.')
        ca_text = ca_path.read_text()
        if '-----BEGIN CERTIFICATE-----' not in ca_text or 'PRIVATE KEY-----' in ca_text:
            raise ValueError(f'CA bundle must contain only public certificates: {ca_path}')
        db.verify_certificate(directory / cert, directory / key, ca_path, hosts, label)

    verify(tls, 'fullchain.pem', 'privkey.pem', 'root-ca.pem',
           ['localhost', '127.0.0.1', 'agora-nginx'], 'Gateway')
    for service, host in (('spring', 'agora-spring'), ('cpp', 'agora-cpp'),
                          ('frontend', 'agora-frontend-dev'), ('redis-insight', 'redis-insight')):
        directory = tls / 'internal' / service
        hosts = [host, 'localhost'] + (['127.0.0.1'] if service == 'redis-insight' else [])
        verify(directory, 'fullchain.pem', 'privkey.pem', 'ca.pem', hosts, service)
        # Nginx and Vite use the gateway's public CA bundle for upstream verification.
        db.verify_certificate(directory / 'fullchain.pem', directory / 'privkey.pem',
                              tls / 'root-ca.pem', [host], f'{service} gateway trust')
    sql = configs['mssql']
    sql_key = tls / 'mssql/server.key'
    if sql_key.is_file() and '-----BEGIN RSA PRIVATE KEY-----' not in sql_key.read_text():
        raise ValueError(f'SQL Server requires an unencrypted RSA PKCS#1 private key: {sql_key}')
    verify(tls / 'mssql', 'server.crt', 'server.key', 'ca.crt',
           ['agora-mssql', 'mssql', 'localhost', sql.get('MSSQL_EXTERNAL_IP', '127.0.0.1')], 'SQL Server')
    redis = configs['redis']
    hosts = [redis.get(name, default) for name, default in (
        ('REDIS_PRIMARY_IP', '172.20.0.2'), ('REDIS_REPLICA_1_IP', '172.20.0.7'),
        ('REDIS_REPLICA_2_IP', '172.20.0.5'), ('REDIS_SENTINEL_1_IP', '172.20.0.6'),
        ('REDIS_SENTINEL_2_IP', '172.20.0.3'), ('REDIS_SENTINEL_3_IP', '172.20.0.4'))]
    verify(tls / 'redis', 'server.crt', 'server.key', 'ca.crt', hosts, 'Redis/Sentinel')
    es = configs['elasticsearch']
    verify(tls / 'elasticsearch', 'http.crt', 'http.key', 'ca.crt',
           ['agora-elasticsearch', 'localhost', es.get('ES_EXTERNAL_IP', '127.0.0.1')], 'Elasticsearch')


def prepare(db_dir, tls_dir, public_origin='https://localhost:8443'):
    db_dir = db_dir.expanduser().resolve()
    tls_dir = tls_dir.expanduser().resolve()
    parsed = urlsplit(public_origin)
    if parsed.scheme != 'https' or not parsed.hostname or parsed.username or parsed.password or parsed.path not in ('', '/') or parsed.query or parsed.fragment:
        raise ValueError('--public-origin must be an HTTPS origin without credentials, path, query or fragment.')
    # Parse the port before writing configuration so invalid input cannot partially apply.
    gateway_port = parsed.port or 443
    public_origin = public_origin.rstrip('/')
    db = module_at('agora_db_setup', db_dir / 'ops/configure-db.py')
    db.prepare(tls_dir)
    configs = {service: db.read_env(path) for service, path in db.ENV_FILES.items()}
    verify_bundle(db, tls_dir, configs)
    db.verify_certificate(tls_dir / 'fullchain.pem', tls_dir / 'privkey.pem', tls_dir / 'root-ca.pem',
                          [parsed.hostname], 'Public gateway hostname')

    sync = module_at('agora_db_sync', ROOT / 'scripts/sync-docker-env.py')
    env_path = ROOT / '.env'
    if env_path.is_symlink():
        raise ValueError('Refusing a symlinked backend .env.')
    first_setup = not env_path.exists()
    if first_setup:
        sync.atomic_write_owner_only(env_path, (ROOT / '.env.example').read_text())
    values = sync.read_env(env_path)
    desired = {
        'NGINX_TLS_CERT_DIR': str(tls_dir), 'NGINX_HTTPS_PORT': str(gateway_port),
        'AGORA_DB_DIR': str(db_dir), 'DB_ENCRYPT': 'true', 'DB_TRUST_SERVER_CERTIFICATE': 'false',
        'REDIS_TLS_ENABLED': 'true', 'ES_SCHEME': 'https',
        'CPP_FREETDS_CONF_HOST_PATH': str(ROOT / 'cpp/config/freetds-docker-tls.conf'),
        'SERVICE_TLS_CERT': str(tls_dir / 'internal/spring/fullchain.pem'),
        'SERVICE_TLS_KEY': str(tls_dir / 'internal/spring/privkey.pem'),
        'SERVICE_TLS_CA': str(tls_dir / 'root-ca.pem'),
    }
    for key in ('JWT_SECRET', 'CPP_INTERNAL_API_TOKEN', 'ADMIN_PASSWORD'):
        if not values.get(key) or db.PLACEHOLDER.search(values[key]):
            desired[key] = secrets.token_hex(32)
    origins = [item.strip() for item in values.get('CORS_ALLOWED_ORIGINS', '').split(',') if item.strip().startswith('https://')]
    desired['CORS_ALLOWED_ORIGINS'] = ','.join(dict.fromkeys(origins + [public_origin]))
    network = db.ipaddress.ip_network(configs['mssql'].get('AGORA_NET_SUBNET', '172.21.0.0/16'))
    proxy_ip = str(network.network_address + 250) if first_setup else values.get('NGINX_BACKEND_IP') or str(network.network_address + 250)
    if db.ipaddress.ip_address(proxy_ip) not in network:
        raise ValueError('NGINX_BACKEND_IP must be in the DB AGORA_NET_SUBNET. Update the existing backend .env.')
    desired['NGINX_BACKEND_IP'] = proxy_ip
    db.update_env_file(env_path, desired)
    previous_argv = sys.argv
    try:
        sys.argv = ['sync-docker-env.py', '--db-dir', str(db_dir), '--backend-env', str(env_path)]
        sync.main()
    finally:
        sys.argv = previous_argv
    print(f'Backend TLS setup complete. Gateway: {public_origin}. Existing secrets preserved; secret values hidden.')
    if not (ROOT / 'cpp/third_party/uWebSockets/uSockets/src/libusockets.h').is_file():
        print('Build dependencies: run git submodule update --init --recursive in the BE repository.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--db-dir', type=Path, default=ROOT.parent / 'project-agora-DB')
    parser.add_argument('--tls-dir', type=Path, required=True, help='Manually supplied bundle; see TLS_SETUP.md.')
    parser.add_argument('--public-origin', default='https://localhost:8443')
    args = parser.parse_args()
    prepare(args.db_dir, args.tls_dir, args.public_origin)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f'Error: {error}', file=sys.stderr)
        sys.exit(1)
