#!/usr/bin/env python3
"""Validate local Compose inputs without printing credentials or requiring a daemon."""
from __future__ import annotations
import argparse
import importlib.util
import ipaddress
import json
import os
from pathlib import Path
import subprocess
import stat
import sys

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('agora_dotenv', ROOT / 'scripts/sync-docker-env.py')
dotenv = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dotenv)
PUBLIC_SETTINGS = {'NGINX_BACKEND_IP', 'CPP_TRUSTED_PROXY_IPS', 'CPP_SQL_CA_CERT_HOST_PATH'}


def effective_settings():
    values = dotenv.read_env(ROOT / '.env')
    return {**values, **os.environ}


def validate(build=False, gateway=False):
    values = effective_settings()
    for key, expected in (("DB_ENCRYPT", "true"), ("DB_TRUST_SERVER_CERTIFICATE", "false"), ("REDIS_TLS_ENABLED", "true"), ("ES_SCHEME", "https")):
        if values.get(key, expected).lower() != expected:
            raise ValueError(f'TLS configuration requires {key}={expected}.')
    proxy_ip = values.get('NGINX_BACKEND_IP') or '172.23.0.250'
    address = ipaddress.ip_address(proxy_ip)
    if address.version != 4 or not address.is_private or address.is_loopback or address.is_unspecified or address.is_multicast:
        raise ValueError('NGINX_BACKEND_IP must be a private IPv4 address on agora-services.')
    inspected = subprocess.CompletedProcess([], 1, '', '')
    if gateway:
        inspected = subprocess.run(['docker', 'network', 'inspect', 'agora-services'], capture_output=True, text=True, timeout=10)
    if inspected.returncode == 0:
        network = json.loads(inspected.stdout)[0]
        pools = [ipaddress.ip_network(pool['Subnet']) for pool in network['IPAM']['Config'] if pool.get('Subnet')]
        if not any(address in pool and address not in (pool.network_address, pool.broadcast_address) for pool in pools):
            raise ValueError('NGINX_BACKEND_IP is outside agora-services. Set it to a free address in the Wall service network subnet.')
        for container in network.get('Containers', {}).values():
            occupied = container.get('IPv4Address', '').split('/')[0]
            if occupied == proxy_ip and container.get('Name') != 'agora-nginx':
                raise ValueError('NGINX_BACKEND_IP is already used by another container. Choose a free address.')
    args = ['docker', 'compose', '--project-directory', str(ROOT), '--env-file', str(ROOT / '.env'), '-f', str(ROOT / 'docker-compose.backend.yml')]
    if values.get('CPP_SQL_CA_CERT_HOST_PATH'):
        args += ['-f', str(ROOT / 'docker-compose.sql-ca.yml')]
    result = subprocess.run(args + ['config', '--format', 'json'], capture_output=True, text=True, env=values)
    if result.returncode:
        raise ValueError('Compose configuration is invalid; run backend-docker.sh config to identify the required setting.')
    config = json.loads(result.stdout)
    for service, definition in config['services'].items():
        for mount in definition.get('volumes', []):
            if mount['type'] != 'bind':
                continue
            path = Path(mount['source'])
            if mount['target'] == '/source':
                if not path.is_dir():
                    raise ValueError('service-tls-init: missing internal certificate directory; run setup-docker.py with --tls-dir.')
                for name in ('spring', 'cpp', 'frontend'):
                    for filename in ('fullchain.pem', 'privkey.pem', 'ca.pem'):
                        if not (path / name / filename).is_file():
                            raise ValueError(f'Missing manually supplied certificate file: {path / name / filename}')
                continue
            if not path.is_file() or not os.access(path, os.R_OK):
                raise ValueError(f'{service}: unreadable/missing bind file for {mount["target"]}. Configure its host path in .env.')
            if str(definition.get('user', '10001')).split(':', 1)[0] == '0':
                continue
            info = path.stat()
            readable = bool(info.st_mode & stat.S_IROTH) or (info.st_uid == 10001 and bool(info.st_mode & stat.S_IRUSR)) or (info.st_gid == 10001 and bool(info.st_mode & stat.S_IRGRP))
            if not readable:
                raise ValueError(f'{service}: bind file for {mount["target"]} is not readable by container UID 10001. Public CA/config files need read permission; never make private keys public.')
    for key in ('JWT_SECRET', 'CPP_INTERNAL_API_TOKEN'):
        if len(values.get(key, '').encode()) < 32:
            raise ValueError(f'{key} must contain at least 32 bytes.')
    explicit_proxies = values.get('CPP_TRUSTED_PROXY_IPS', '')
    if gateway and explicit_proxies and proxy_ip not in [item.strip() for item in explicit_proxies.split(',')]:
        raise ValueError('CPP_TRUSTED_PROXY_IPS must include NGINX_BACKEND_IP for the managed gateway.')
    if build:
        for relative in ('cpp/third_party/uWebSockets/src/App.h',
                         'cpp/third_party/uWebSockets/uSockets/src/libusockets.h'):
            if not (ROOT / relative).is_file():
                raise ValueError('Build dependencies are missing. Run git submodule update --init --recursive in the BE repository.')
    print('Backend configuration, secret presence, bind files' + (' and submodules' if build else '') + ' validated; values hidden.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--setting', choices=sorted(PUBLIC_SETTINGS))
    parser.add_argument('--default', default='')
    parser.add_argument('--build', action='store_true')
    parser.add_argument('--gateway', action='store_true', help='Also validate the managed Nginx network and trusted proxy address.')
    args = parser.parse_args()
    if args.setting:
        print(effective_settings().get(args.setting) or args.default)
    else:
        validate(args.build, args.gateway)

if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError) as error:
        print(f'Error: {error}', file=sys.stderr)
        sys.exit(1)
