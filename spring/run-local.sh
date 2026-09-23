#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
ENV_FILE="$PROJECT_DIR/.env"
JAR_FILE="$SCRIPT_DIR/build/libs/frelog-0.0.1-SNAPSHOT.jar"

if [[ ! -f "$ENV_FILE" ]]; then
    echo "Missing $ENV_FILE. Create it from the README environment-variable example." >&2
    exit 1
fi

set -a
source "$ENV_FILE"
set +a

secret_length_bytes=$(printf '%s' "${JWT_SECRET:-}" | LC_ALL=C wc -c)
if (( secret_length_bytes < 32 )); then
    echo "JWT_SECRET must be set in $ENV_FILE and contain at least 32 bytes." >&2
    exit 1
fi

if [[ ! -f "$JAR_FILE" ]]; then
    echo "Missing $JAR_FILE. Build it with ./spring/gradlew -p spring bootJar." >&2
    exit 1
fi

# Gradle may overwrite build/libs while the server is running. Keep this
# process on an immutable snapshot so static resources remain readable.
RUN_JAR="$(mktemp --suffix=.jar /tmp/agora-spring.XXXXXXXX)"
trap 'rm -f -- "$RUN_JAR"' EXIT
cp -- "$JAR_FILE" "$RUN_JAR"

set +e
java -jar "$RUN_JAR"
status=$?
set -e

if [[ $status -ne 0 && -t 0 ]]; then
    read -r -p "Spring exited with status $status. Press Enter to close this terminal." _ || true
fi

exit "$status"
