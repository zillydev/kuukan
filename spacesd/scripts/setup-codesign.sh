#!/bin/sh
# Create a self-signed code-signing cert "spacesd-selfsign" and import it, so
# the daemon's Accessibility grant survives rebuilds. Run once. Idempotent.
set -e
KC="$HOME/Library/Keychains/login.keychain-db"
if security find-certificate -c spacesd-selfsign "$KC" >/dev/null 2>&1; then
  echo "spacesd-selfsign already present"; exit 0
fi
TMP=$(mktemp -d)
cat > "$TMP/c.cnf" <<CNF
[ req ]
distinguished_name = dn
x509_extensions = ext
prompt = no
[ dn ]
CN = spacesd-selfsign
[ ext ]
basicConstraints = critical, CA:false
keyUsage = critical, digitalSignature
extendedKeyUsage = critical, codeSigning
CNF
openssl req -x509 -newkey rsa:2048 -keyout "$TMP/k.pem" -out "$TMP/c.pem" -days 3650 -nodes -config "$TMP/c.cnf"
openssl pkcs12 -export -legacy -certpbe PBE-SHA1-3DES -keypbe PBE-SHA1-3DES -macalg sha1 \
  -out "$TMP/c.p12" -inkey "$TMP/k.pem" -in "$TMP/c.pem" -passout pass:spacesd -name spacesd-selfsign
security import "$TMP/c.p12" -k "$KC" -P spacesd -A
rm -rf "$TMP"
echo "spacesd-selfsign created. Rebuild (make) then enable spacesd in System Settings > Privacy & Security > Accessibility."
