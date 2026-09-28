#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
DB_DIR="${AGORA_DB_DIR:-$PROJECT_DIR/../project-agora-DB}"
if ! DB_DIR="$(cd -- "$DB_DIR" 2>/dev/null && pwd)"; then
    printf 'Error: DB repository not found. Set AGORA_DB_DIR to its path.\n' >&2
    exit 1
fi
ENV_FILE="$PROJECT_DIR/.env"
WAIT_SECONDS="${STACK_START_WAIT_SECONDS:-240}"

die() {
    printf 'Error: %s\n' "$*" >&2
    exit 1
}

[[ -f "$ENV_FILE" && ! -L "$ENV_FILE" ]] || die "Missing backend .env: $ENV_FILE"
for required_file in \
    "$DB_DIR/mssql/.env" \
    "$DB_DIR/elasticsearch/.env" \
    "$DB_DIR/elasticsearch/ensure-elasticsearch-initialized.sh" \
    "$DB_DIR/elasticsearch/sync-elasticsearch-users.sh" \
    "$DB_DIR/redis/.env" \
    "$DB_DIR/docker-compose.yml" \
    "$DB_DIR/ops/migrate-local-redis-ha.sh" \
    "$DB_DIR/mssql/docker-compose.yml" \
    "$DB_DIR/elasticsearch/docker-compose.yml" \
    "$DB_DIR/redis/docker-compose.sentinel.yml"; do
    [[ -f "$required_file" ]] || die "Required DB file is missing: $required_file"
done
command -v docker >/dev/null 2>&1 || die "Docker is not installed or not on PATH."
command -v python3 >/dev/null 2>&1 || die "python3 is required to synchronize backend settings."
docker compose version >/dev/null 2>&1 || die "Docker Compose v2 is required."
[[ "$WAIT_SECONDS" =~ ^[1-9][0-9]*$ ]] || die "STACK_START_WAIT_SECONDS must be a positive integer."

assert_container_project() {
    local name="$1"
    local expected_project="$2"
    local actual_project

    if ! docker inspect "$name" >/dev/null 2>&1; then
        return 0
    fi
    actual_project="$(docker inspect --format '{{ index .Config.Labels "com.docker.compose.project" }}' "$name" 2>/dev/null || true)"
    [[ "$actual_project" == "$expected_project" ]] \
        || die "Fixed container name '$name' is already owned by '${actual_project:-a non-Compose container}', expected Compose project '$expected_project'. No container was removed."
}

assert_container_project agora-mssql project-agora-db
assert_container_project agora-elasticsearch project-agora-db

for volume in \
    project-agora-db_mssql_data \
    project-agora-db_es_data; do
    docker volume inspect "$volume" >/dev/null 2>&1 \
        || die "Expected data volume '$volume' is missing. This restart script will not create an empty replacement; restore or initialize the DB stack first."
done

python3 "$SCRIPT_DIR/sync-docker-env.py" --db-dir "$DB_DIR" --backend-env "$ENV_FILE"

cd "$DB_DIR"

printf 'Starting MSSQL and waiting for health...\n'
docker compose -p project-agora-db -f docker-compose.yml \
    up -d --wait --wait-timeout "$WAIT_SECONDS" mssql

printf 'Starting Elasticsearch and waiting for health...\n'
docker compose -p project-agora-db -f docker-compose.yml \
    up -d --wait --wait-timeout "$WAIT_SECONDS" elasticsearch

printf 'Checking Elasticsearch index and log schema...\n'
"$DB_DIR/elasticsearch/ensure-elasticsearch-initialized.sh"

printf 'Synchronizing Elasticsearch application accounts...\n'
"$DB_DIR/elasticsearch/sync-elasticsearch-users.sh"

printf 'Preparing the default Redis Sentinel HA stack...\n'
"$DB_DIR/ops/migrate-local-redis-ha.sh"

printf 'Applying Redis HA ACLs, search index, and SQL registration...\n'
"$DB_DIR/redis/init-redis-sentinel.sh"

printf 'Starting backend containers...\n'
env -u DOCKER_DB_HOST -u DOCKER_DB_PORT -u DOCKER_ES_HOST -u DOCKER_ES_PORT \
    -u REDIS_SENTINELS -u REDIS_SENTINEL_MASTER_NAME \
    "$SCRIPT_DIR/backend-docker.sh" up
