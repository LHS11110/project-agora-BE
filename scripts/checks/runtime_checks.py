"""Portable preflight regression tests with mocked Docker, no secret output."""
import importlib.util
import json
import os
import re
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('docker_preflight',ROOT/'scripts/docker-preflight.py')
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class PreflightTests(unittest.TestCase):
    def values(self):return {'JWT_SECRET':'j'*32,'CPP_INTERNAL_API_TOKEN':'t'*32,'NGINX_BACKEND_IP':'172.21.0.250'}
    def test_base_never_requires_a_host_system_ca(self):
        text=(ROOT/'docker-compose.backend.yml').read_text()
        self.assertNotIn('/etc/ssl/certs/ca-certificates.crt}',text)
        self.assertIn('DOCKER_DB_FREETDS_CONF:-/etc/freetds/freetds.conf',text)
    def test_missing_mount_reports_target_without_secret(self):
        def docker(args,**kwargs):
            if args[1:3]==['network','inspect']:return subprocess.CompletedProcess(args,1,'','')
            config={'services':{'cpp':{'volumes':[{'type':'bind','source':'/missing/ca.crt','target':'/run/ca.crt'}]}}}
            return subprocess.CompletedProcess(args,0,json.dumps(config),'')
        with patch.object(module,'effective_settings',return_value=self.values()),patch.object(module.subprocess,'run',side_effect=docker):
            with self.assertRaisesRegex(ValueError,'cpp: unreadable/missing bind file'):
                module.validate()
    def test_custom_ip_must_match_the_actual_docker_subnet(self):
        def docker(args,**kwargs):
            return subprocess.CompletedProcess(args,0,json.dumps([{'IPAM':{'Config':[{'Subnet':'172.25.0.0/16'}]}}]),'')
        with patch.object(module,'effective_settings',return_value=self.values()),patch.object(module.subprocess,'run',side_effect=docker):
            with self.assertRaisesRegex(ValueError,'outside agora-net'):
                module.validate(gateway=True)
    def test_public_mount_permissions_are_checked_for_nonroot_containers(self):
        with tempfile.TemporaryDirectory() as directory:
            source=Path(directory)/'ca.crt';source.write_text('public test certificate');source.chmod(0o000)
            def docker(args,**kwargs):
                if args[1:3]==['network','inspect']:return subprocess.CompletedProcess(args,1,'','')
                config={'services':{'spring':{'volumes':[{'type':'bind','source':str(source),'target':'/run/certs/ca.crt'}]}}}
                return subprocess.CompletedProcess(args,0,json.dumps(config),'')
            with patch.object(module,'effective_settings',return_value=self.values()),patch.object(module.subprocess,'run',side_effect=docker),patch.object(module.os,'access',return_value=True):
                with self.assertRaisesRegex(ValueError,'not readable by container UID'):
                    module.validate()

    def test_public_ca_copy_rejects_keys_and_preserves_previous_ca(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            source=root/'ca.crt'; target=root/'certs/ca.crt'
            source.write_text('-----BEGIN CERTIFICATE-----\npublic-one\n-----END CERTIFICATE-----\n')
            old=module.dotenv.copy_public_ca(root,'ca.crt',target)
            source.write_text('-----BEGIN CERTIFICATE-----\npublic-two\n-----END CERTIFICATE-----\n')
            new=module.dotenv.copy_public_ca(root,'ca.crt',target)
            self.assertNotEqual(old,new)
            self.assertIn('public-one',Path(old).read_text())
            self.assertEqual(Path(new).stat().st_mode & 0o777,0o644)
            source.write_text('-----BEGIN PRIVATE KEY-----\nnot-a-real-key\n-----END PRIVATE KEY-----\n')
            with self.assertRaisesRegex(ValueError,'public certificates'):
                module.dotenv.copy_public_ca(root,'ca.crt',target)

    def test_log_auth_shell_uses_curl_stdin_without_password_arguments(self):
        script=(ROOT/'scripts/backend-docker.sh').read_text()
        command=re.search(r"if ! docker exec agora-spring /bin/sh -c '(.*?)'; then",script,re.S).group(1)
        with tempfile.TemporaryDirectory() as directory:
            curl=Path(directory)/'curl'
            curl.write_text('#!/bin/sh\ncat > "$CAPTURE"\nprintf "%s\n" "$@" > "$ARGUMENTS"\n')
            curl.chmod(0o700)
            env={**os.environ,'PATH':directory+os.pathsep+os.environ['PATH'],'ES_LOG_USER_NAME':'test-writer','ES_LOG_USER_PASSWORD':'synthetic-test-only','ES_CA_CERT':'/a path/ca.crt','ES_SCHEME':'https','ES_HOST':'agora-elasticsearch','ES_PORT':'9200','CAPTURE':directory+'/input','ARGUMENTS':directory+'/args'}
            result=subprocess.run(['/bin/sh','-c',command],env=env,capture_output=True)
            self.assertEqual(result.returncode,0,result.stderr.decode())
            self.assertIn('Authorization: Basic',Path(env['CAPTURE']).read_text())
            self.assertNotIn('synthetic-test-only',Path(env['ARGUMENTS']).read_text())
            self.assertEqual(result.stdout,b'')

    def test_nginx_mode_rejects_typos(self):
        result=subprocess.run(['/bin/sh',str(ROOT/'nginx/10-validate-mode.sh')],env={'FRONTEND_MODE':'devlopment'},capture_output=True)
        self.assertNotEqual(result.returncode,0)

if __name__=='__main__':unittest.main()
