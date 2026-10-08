#!/usr/bin/env bash
set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
ENV_FILE="$PROJECT_DIR/.env"
COMPOSE_FILE="$PROJECT_DIR/docker-compose.backend.yml"
PROJECT_NAME="agora-backend"

die() {
    printf 'Error: %s\n' "$*" >&2
    exit 1
}

usage() {
    cat <<'EOF'
Usage: ./scripts/backend-docker.sh <command>

Commands:
  build       Build Spring and C++ backend images
  up          Build and start both backends, then wait for health checks
  start       Start or create both backend containers and wait for health checks
  restart     Recreate both backend containers from their current images
  stop        Stop both backend containers without removing them
  down        Stop and remove only the backend containers
  status      Show backend container status
  health      Call the published Spring and C++ health endpoints
  logs [args] Follow backend logs; optional docker compose logs arguments follow
  config      Validate the Compose configuration without printing secrets
EOF
}

[[ -f "$ENV_FILE" ]] || die "Missing $ENV_FILE. Create it from the README environment-variable section."
command -v python3 >/dev/null 2>&1 || die "python3 is required for portable configuration checks."
command -v docker >/dev/null 2>&1 || die "Docker is not installed or not on PATH."
docker compose version >/dev/null 2>&1 || die "Docker Compose v2 is required."

cd "$PROJECT_DIR"

compose() {
    local network_gateway proxy_ip configured_proxies custom_sql_ca
    local compose_files=(--file "$COMPOSE_FILE")
    proxy_ip="$(python3 "$SCRIPT_DIR/docker-preflight.py" --setting NGINX_BACKEND_IP --default 172.21.0.250)"
    configured_proxies="$(python3 "$SCRIPT_DIR/docker-preflight.py" --setting CPP_TRUSTED_PROXY_IPS)"
    custom_sql_ca="$(python3 "$SCRIPT_DIR/docker-preflight.py" --setting CPP_SQL_CA_CERT_HOST_PATH)"
    if [[ -n "$custom_sql_ca" ]]; then
        compose_files+=(--file "$PROJECT_DIR/docker-compose.sql-ca.yml")
    fi
    network_gateway="$(docker network inspect --format '{{range .IPAM.Config}}{{.Gateway}}{{end}}' agora-net 2>/dev/null || true)"
    if [[ -z "$configured_proxies" ]]; then
        export CPP_TRUSTED_PROXY_IPS="${network_gateway:+$network_gateway,}$proxy_ip"
    fi

    docker compose \
        --project-directory "$PROJECT_DIR" \
        --env-file "$ENV_FILE" \
        --project-name "$PROJECT_NAME" \
        "${compose_files[@]}" \
        "$@"
}

require_network() {
    local network_name="$1"
    docker info >/dev/null 2>&1 || die "Docker engine is unavailable or access is denied. Check Docker Desktop/daemon and socket permissions."
    docker network inspect "$network_name" >/dev/null 2>&1 \
        || die "Docker network '$network_name' is missing. Start the matching DB/Sentinel Compose stack first."
}

backend_network_references_are_stale() {
    local container_name network_name current_network_id container_network_id

    for container_name in agora-spring agora-cpp; do
        docker inspect "$container_name" >/dev/null 2>&1 || continue
        for network_name in agora-net agora-redis-ha; do
            current_network_id="$(docker network inspect --format '{{.Id}}' "$network_name")"
            container_network_id="$(docker inspect --format "{{with index .NetworkSettings.Networks \"$network_name\"}}{{.NetworkID}}{{end}}" "$container_name")"
            if [[ -n "$container_network_id" && "$container_network_id" != "$current_network_id" ]]; then
                return 0
            fi
        done
    done

    return 1
}

wait_for_healthy_container() {
    local container_name="$1"
    local timeout_seconds="${BACKEND_DEPENDENCY_WAIT_SECONDS:-180}"
    local elapsed=0
    local state

    docker inspect "$container_name" >/dev/null 2>&1 \
        || die "Required dependency '$container_name' is missing. Start the DB/Sentinel services first."

    while (( elapsed < timeout_seconds )); do
        state="$(docker inspect --format '{{if .State.Health}}{{.State.Health.Status}}{{else}}{{.State.Status}}{{end}}' "$container_name")"
        case "$state" in
            healthy) return 0 ;;
            unhealthy|exited|dead|removing)
                die "Dependency '$container_name' is $state. Check it with: docker logs $container_name" ;;
        esac
        sleep 3
        elapsed=$((elapsed + 3))
    done

    die "Timed out waiting for '$container_name' to become healthy. Check: docker logs $container_name"
}

check_dependencies() {
    require_network agora-net

    require_network agora-redis-ha

    for container_name in \
        agora-mssql \
        agora-elasticsearch \
        agora-redis-primary \
        agora-redis-replica-1 \
        agora-redis-replica-2 \
        agora-redis-sentinel-1 \
        agora-redis-sentinel-2 \
        agora-redis-sentinel-3; do
        printf 'Waiting for %s...\n' "$container_name"
        wait_for_healthy_container "$container_name"
    done
}

wait_for_backend_health() {
    local timeout_seconds="${BACKEND_START_WAIT_SECONDS:-180}"

    if backend_network_references_are_stale; then
        printf 'Backend containers reference a replaced Docker network; recreating them with current networks.\n' >&2
        set -- --force-recreate "$@"
    fi

    if ! compose up -d --wait --wait-timeout "$timeout_seconds" "$@"; then
        compose ps
        compose logs --tail=100
        return 1
    fi
}

health_check() {
    command -v curl >/dev/null 2>&1 || die "curl is required for the host health check."
    curl --fail --silent --show-error --connect-timeout 3 --max-time 10 \
        http://127.0.0.1:8080/api/auth/health
    printf '\n'
    curl --fail --silent --show-error --connect-timeout 3 --max-time 10 \
        http://127.0.0.1:8000/health
    printf '\n'
    # Database health does not establish that the dedicated log writer can authenticate.
    if ! docker exec agora-spring /bin/sh -c '
        set -eu
        credentials=$(printf "%s:%s" "$ES_LOG_USER_NAME" "$ES_LOG_USER_PASSWORD" | base64 | tr -d "\r\n")
        printf "header = \"Authorization: Basic %s\"\n" "$credentials" \
          | curl --config - --fail --silent --show-error --connect-timeout 3 --max-time 10 \
              --cacert "$ES_CA_CERT" \
              --url "$ES_SCHEME://$ES_HOST:$ES_PORT/_security/_authenticate" --output /dev/null
    '; then
        die "Elasticsearch log writer authentication failed. Synchronize ES accounts with the DB repository's elasticsearch/sync-elasticsearch-users.sh and recreate the backend."
    fi
    printf 'Elasticsearch log writer authentication passed.\n'
}

command_name="${1:-help}"
shift || true

case "$command_name" in
    build)
        python3 "$SCRIPT_DIR/docker-preflight.py" --build
        compose build "$@"
        ;;
    up)
        (( $# == 0 )) || die "'up' starts both backend services; do not pass service names."
        python3 "$SCRIPT_DIR/docker-preflight.py" --build
        compose config --quiet
        check_dependencies
        wait_for_backend_health --build "$@"
        health_check
        ;;
    start)
        (( $# == 0 )) || die "'start' starts both backend services; do not pass service names."
        python3 "$SCRIPT_DIR/docker-preflight.py"
        check_dependencies
        wait_for_backend_health "$@"
        health_check
        ;;
    restart)
        (( $# == 0 )) || die "'restart' recreates both backend services; do not pass service names."
        python3 "$SCRIPT_DIR/docker-preflight.py"
        check_dependencies
        wait_for_backend_health --force-recreate "$@"
        health_check
        ;;
    stop)
        compose stop "$@"
        ;;
    down)
        compose down --remove-orphans "$@"
        ;;
    status|ps)
        compose ps "$@"
        ;;
    health)
        health_check
        ;;
    logs)
        compose logs --tail=200 --follow "$@"
        ;;
    config)
        compose config --quiet
        printf 'Docker Compose configuration is valid.\n'
        ;;
    help|-h|--help)
        usage
        ;;
    *)
        usage >&2
        die "Unknown command '$command_name'."
        ;;
esac
