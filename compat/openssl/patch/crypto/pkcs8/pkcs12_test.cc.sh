#!/bin/bash

set -euo pipefail

# TestOpenSSL, TestNSS, TestWindows, TestEmptyPassword, TestNullPassword,
# TestUnicode and TestWindowsCompat stay commented out: their .p12 files use
# pbeWithSHA1And40BitRC2-CBC, which OpenSSL only provides via the legacy
# provider, and we don't load it.
uncomment.sh "$1" --comment \
  --uncomment-regex '#include' \
  --uncomment-regex 'static .* kPassword\[\] = ' \
  --uncomment-static-func-impl TestImpl \
  --uncomment-gtest-func PKCS12Test TestPBES2 \
  --uncomment-gtest-func PKCS12Test TestNoEncryption \
