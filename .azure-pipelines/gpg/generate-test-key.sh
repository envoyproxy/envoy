#!/bin/bash -e

gpg --batch --gen-key <<EOF
%echo Generating a basic OpenPGP key
Key-Type: 1
Key-Length: 4096
Subkey-Type: 1
Subkey-Length: 4096
Name-Real: Envoy CI
Name-Email: envoy-ci@for.testing.only
Expire-Date: 0
Passphrase: HACKME
%commit
%echo done
EOF
mkdir -p envoy/
echo "HACKME" \
    | gpg --pinentry-mode loopback \
          --passphrase-fd 0 \
          --export-secret-key -a "Envoy CI" \
          > envoy/ci.snakeoil.gpg.key
