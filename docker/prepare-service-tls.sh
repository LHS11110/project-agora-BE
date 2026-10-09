#!/bin/sh
set -eu
umask 077
for service in spring cpp frontend; do
  target="/target/$service"
  mkdir -p "$target"
  for file in fullchain.pem privkey.pem; do
    test -s "/source/$service/$file"
    cp "/source/$service/$file" "$target/$file"
  done
  test -s /source-ca.pem
  cp /source-ca.pem "$target/ca.pem"
  uid=10001
  chown -R "$uid:$uid" "$target"
  chmod 750 "$target"
  chmod 600 "$target/privkey.pem"
  chmod 644 "$target/fullchain.pem" "$target/ca.pem"
done

cp "$JAVA_HOME/lib/security/cacerts" /target/spring/truststore
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
for bundle in service sql; do
  input=/target/spring/ca.pem
  if [ "$bundle" = sql ]; then input=/source-sql-ca.crt; fi
  awk -v prefix="$work/$bundle-" '
    /-----BEGIN CERTIFICATE-----/ { count++; output=prefix count ".pem" }
    output != "" { print > output }
    /-----END CERTIFICATE-----/ { close(output); output="" }
  ' "$input"
  for cert in "$work/$bundle-"*.pem; do
    test -s "$cert"
    alias="agora-$(basename "$cert" .pem)"
    keytool -importcert -noprompt -alias "$alias" -file "$cert" -keystore /target/spring/truststore -storepass changeit
  done
done
chown 10001:10001 /target/spring/truststore
chmod 644 /target/spring/truststore
