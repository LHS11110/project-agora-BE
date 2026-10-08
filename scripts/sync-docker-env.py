#!/usr/bin/env python3
"""Sync local DB Compose credentials and Redis Sentinel seeds into BE .env."""

from __future__ import annotations

import argparse
import hashlib
import ipaddress
import json
import os
import re
import shlex
import stat
import sys
import tempfile
from pathlib import Path


ENV_ASSIGNMENT = re.compile(r"^\s*(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*?)\s*$")


def read_env(path: Path) -> dict[str, str]:
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"Required environment file is missing or is a symlink: {path}")
    if stat.S_IMODE(path.stat().st_mode) & 0o077:
        raise ValueError(f"Environment file must be owner-only (0600): {path}")

    values: dict[str, str] = {}
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        match = ENV_ASSIGNMENT.match(line)
        if not match:
            continue
        key, raw_value = match.groups()
        if raw_value.startswith(("'", '"')):
            try:
                parsed = shlex.split(raw_value, comments=True, posix=True)
            except ValueError as exc:
                raise ValueError(f"Invalid quoted value in {path}:{line_number}") from exc
            if len(parsed) != 1:
                raise ValueError(f"Expected one value in {path}:{line_number}")
            value = parsed[0]
        else:
            value = re.sub(r"\s+#.*$", "", raw_value).rstrip()
        values[key] = value
    return values


def compose_value(value: str) -> str:
    if any(character in value for character in ("\n", "\r", "$", "`")):
        raise ValueError("Generated environment settings cannot contain shell expansion or line breaks.")
    if not value or re.fullmatch(r"[A-Za-z0-9_./:@,+%!=-]+", value):
        return value
    return json.dumps(value, ensure_ascii=False)


def atomic_write_owner_only(path: Path, contents: str) -> None:
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary_path = Path(temporary_name)
    try:
        os.fchmod(descriptor, stat.S_IRUSR | stat.S_IWUSR)
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            stream.write(contents)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_path, path)
        os.chmod(path, stat.S_IRUSR | stat.S_IWUSR)
    except BaseException:
        try:
            os.close(descriptor)
        except OSError:
            pass
        temporary_path.unlink(missing_ok=True)
        raise


def copy_public_ca(base: Path, raw_path: str, target: Path) -> str:
    if not raw_path:
        raise ValueError("A host-readable TLS CA certificate path is required for Docker synchronization.")
    source = Path(raw_path)
    if not source.is_absolute():
        source = base / source
    data = source.read_text(encoding="utf-8")
    if "-----BEGIN CERTIFICATE-----" not in data or "PRIVATE KEY-----" in data:
        raise ValueError("CA input must contain only public certificates, never a private key.")
    # Content-addressed CA paths preserve the old working CA if later validation
    # or the .env update fails. Only the final atomic .env write selects a CA.
    fingerprint = hashlib.sha256(data.encode()).hexdigest()[:16]
    target = target.with_name(f"{target.stem}-{fingerprint}{target.suffix}")
    target.parent.mkdir(mode=0o700, exist_ok=True)
    atomic_write_owner_only(target, data)
    # Only this public certificate is mounted; non-root UID 10001 must read it.
    # The host directory remains 0700 and private keys are never copied here.
    os.chmod(target, 0o644)
    return str(target)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--db-dir", type=Path, required=True)
    parser.add_argument("--backend-env", type=Path, required=True)
    args = parser.parse_args()

    db_dir = args.db_dir.resolve()
    backend_env = args.backend_env.expanduser().absolute()
    if backend_env.is_symlink() or not backend_env.is_file():
        raise ValueError(f"Backend .env is missing or is a symlink: {backend_env}")

    mssql = read_env(db_dir / "mssql" / ".env")
    elasticsearch = read_env(db_dir / "elasticsearch" / ".env")
    redis = read_env(db_dir / "redis" / ".env")

    desired: dict[str, str] = {
        "DB_NAME": mssql.get("MSSQL_DB", "agora_db"),
        "DB_USER": mssql.get("MSSQL_USER", "agora_user"),
        "DB_PASSWORD": mssql.get("MSSQL_PASSWORD", ""),
        "ES_INDEX": elasticsearch.get("ES_INDEX", "canvas"),
        "ES_USER_NAME": elasticsearch.get("ES_USER_NAME", "agora_user"),
        "ES_USER_PASSWORD": elasticsearch.get("ES_USER_PASSWORD", ""),
        "ES_LOG_INDEX": elasticsearch.get("ES_LOG_INDEX", "agora-logs"),
        "ES_LOG_USER_NAME": elasticsearch.get("ES_LOG_USER_NAME", "agora_log_writer"),
        "ES_LOG_USER_PASSWORD": elasticsearch.get("ES_LOG_USER_PASSWORD", ""),
        "REDIS_USER": redis.get("REDIS_USER", "agora_user"),
        "REDIS_USER_PASSWORD": redis.get("REDIS_USER_PASSWORD", ""),
        "REDIS_SENTINEL_USER": redis.get("REDIS_SENTINEL_USER", ""),
        "REDIS_SENTINEL_PASSWORD": redis.get("REDIS_SENTINEL_PASSWORD", ""),
        "REDIS_SENTINEL_MASTER_NAME": redis.get("REDIS_SENTINEL_MASTER_NAME", "agora-master"),
    }

    sentinel_defaults = ("172.20.0.6", "172.20.0.3", "172.20.0.4")
    sentinel_ips = tuple(
        redis.get(f"REDIS_SENTINEL_{index}_IP", sentinel_defaults[index - 1])
        for index in range(1, 4)
    )
    try:
        for address in sentinel_ips:
            if ipaddress.ip_address(address).version != 4:
                raise ValueError("Local Redis Sentinel addresses must be IPv4 addresses.")
    except ValueError as exc:
        raise ValueError("Local Redis Sentinel addresses must be valid IPv4 addresses.") from exc
    if len(set(sentinel_ips)) != 3:
        raise ValueError("Local Redis Sentinel IP addresses must be unique.")
    desired["REDIS_SENTINELS"] = ",".join(f"{address}:26379" for address in sentinel_ips)

    if elasticsearch.get("ES_HTTP_TLS_ENABLED", "false").lower() != "true" or elasticsearch.get("ES_SCHEME", "http") != "https":
        raise ValueError("Local backend Docker requires Elasticsearch HTTPS. Prepare its TLS configuration before synchronizing.")
    if redis.get("REDIS_TLS_ENABLED", "false").lower() != "true":
        raise ValueError("Local backend Docker requires verified Redis TLS.")
    missing = [key for key, value in desired.items() if not value]
    if missing:
        raise ValueError("Required DB environment values are missing: " + ", ".join(missing))

    certs = backend_env.parent / ".local-certs"
    desired["ES_SCHEME"] = "https"
    desired["ES_CA_CERT"] = copy_public_ca(db_dir / "elasticsearch", elasticsearch.get("ES_CA_CERT", ""), certs / "elasticsearch-ca.crt")
    desired["REDIS_TLS_ENABLED"] = "true"
    desired["REDIS_TLS_CA_CERT"] = copy_public_ca(db_dir / "redis", redis.get("REDIS_TLS_CA_CERT_HOST", ""), certs / "redis-ca.crt")
    desired["DB_TRUST_SERVER_CERTIFICATE"] = mssql.get("DB_TRUST_SERVER_CERTIFICATE", "false")
    desired["DB_ENCRYPT"] = "true"
    desired["DB_MULTI_SUBNET_FAILOVER"] = "false"

    output: list[str] = []
    seen: set[str] = set()
    for line in backend_env.read_text(encoding="utf-8").splitlines():
        match = re.match(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=", line)
        if match and match.group(1) in desired:
            key = match.group(1)
            if key not in seen:
                output.append(f"{key}={compose_value(desired[key])}")
                seen.add(key)
            continue
        output.append(line)
    for key, value in desired.items():
        if key not in seen:
            output.append(f"{key}={compose_value(value)}")

    atomic_write_owner_only(backend_env, "\n".join(output) + "\n")
    print(f"Synchronized {len(desired)} DB connection settings into {backend_env}; secret values hidden (mode 0600).")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        raise SystemExit(1)
