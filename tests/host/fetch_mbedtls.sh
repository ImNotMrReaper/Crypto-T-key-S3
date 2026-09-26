#!/usr/bin/env bash
# Builds mbedTLS 3.6 (the major version ESP-IDF 5 ships) for the host tests into tests/host/.deps.
set -euo pipefail
cd "$(dirname "$0")"
DEST=.deps/mbedtls
[ -f "$DEST/library/libmbedcrypto.a" ] && exit 0
mkdir -p .deps
rm -rf "$DEST"
# The release tarball ships the generated PSA sources a plain git checkout lacks
URL=https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.2/mbedtls-3.6.2.tar.bz2
SHA=8b54fb9bcf4d5a7078028e0520acddefb7900b3e66fec7f7175bb5b7d85ccdca
curl -fsSL "$URL" -o .deps/mbedtls.tar.bz2
echo "$SHA  .deps/mbedtls.tar.bz2" | sha256sum -c --quiet
mkdir -p "$DEST"
tar -xjf .deps/mbedtls.tar.bz2 -C "$DEST" --strip-components=1
make -s -C "$DEST/library" -j"$(nproc)" libmbedcrypto.a libmbedx509.a CFLAGS="-O2 -fPIC"
