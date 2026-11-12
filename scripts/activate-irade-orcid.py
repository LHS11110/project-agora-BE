#!/usr/bin/env python3
"""Validate a private raw Compose env file and activate ORCID without printing credentials."""
import argparse
import os
from pathlib import Path
import stat
import subprocess
import tempfile
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parents[1]


def parse(text):
    values = {}
    for line in text.splitlines():
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        key, separator, value = line.partition('=')
        if not separator or key.strip() in values:
            raise ValueError('Invalid or duplicate environment entry')
        values[key.strip()] = value.strip()
    return values


def validate(path):
    if path.is_symlink() or not path.is_file():
        raise ValueError('Provide a regular private environment file')
    metadata = path.stat()
    if stat.S_IMODE(metadata.st_mode) & 0o077:
        raise ValueError('Environment file must not be readable by other users; use chmod 600')
    text = path.read_text()
    values = parse(text)
    allowed = {'ORCID_OAUTH_ENABLED', 'ORCID_CLIENT_ID', 'ORCID_CLIENT_SECRET', 'ORCID_ISSUER', 'ORCID_REDIRECT_URI'}
    if set(values) - allowed:
        raise ValueError('This dedicated file may contain only ORCID settings')
    for key in ['ORCID_CLIENT_ID', 'ORCID_CLIENT_SECRET']:
        value = values.get(key, '')
        if not value or len(value) > 1024 or value[0] in "\"'" or value[-1] in "\"'":
            raise ValueError(f'Enter {key} without surrounding quotes')
    if values.get('ORCID_ISSUER') not in {'https://orcid.org', 'https://sandbox.orcid.org'}:
        raise ValueError('Use an official ORCID issuer')
    callback = urlparse(values.get('ORCID_REDIRECT_URI', ''))
    if callback.scheme != 'https' or not callback.hostname or callback.username or callback.password or callback.query or callback.fragment or callback.path != '/api/irade/auth/orcid/callback':
        raise ValueError('An exact public HTTPS ORCID redirect URI is required')
    return text, metadata


def main():
    options = argparse.ArgumentParser(description=__doc__)
    options.add_argument('--env-file', type=Path)
    options.add_argument('--check', action='store_true', help='Validate only; do not change files or restart services')
    args = options.parse_args()
    root_env = parse((ROOT / '.env').read_text())
    path = (args.env_file or Path(root_env.get('ORCID_ENV_FILE', ROOT / '.secrets/orcid.env'))).absolute()
    try:
        text, metadata = validate(path)
        if args.check:
            print('ORCID settings are complete; credentials were not printed.')
            return
        lines = text.splitlines()
        lines = [line for line in lines if not line.startswith('ORCID_OAUTH_ENABLED=')]
        lines.append('ORCID_OAUTH_ENABLED=true')
        with tempfile.NamedTemporaryFile(mode='w', dir=path.parent, prefix='.orcid-', delete=False) as output:
            temporary = Path(output.name)
            os.fchmod(output.fileno(), 0o600)
            if os.geteuid() == 0:
                os.fchown(output.fileno(), metadata.st_uid, metadata.st_gid)
            output.write('\n'.join(lines) + '\n')
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
        environment = dict(os.environ, ORCID_ENV_FILE=str(path))
        command = ['docker', 'compose', '--env-file', str(ROOT / '.env'), '-f', str(ROOT / 'docker-compose.backend.yml')]
        subprocess.run(command + ['config', '--quiet'], cwd=ROOT, env=environment, check=True)
        subprocess.run(command + ['up', '-d', '--no-deps', 'spring'], cwd=ROOT, env=environment, check=True)
        print('ORCID enabled and Spring recreated. Check /api/irade/auth/orcid/status and Spring health.')
    except (ValueError, OSError) as error:
        raise SystemExit(str(error)) from None


if __name__ == '__main__':
    main()
