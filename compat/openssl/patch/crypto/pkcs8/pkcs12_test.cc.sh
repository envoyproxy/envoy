#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment \
  --uncomment-regex '#include' \
  --uncomment-regex 'static .* kPassword\[\] = ' \
  --uncomment-regex 'static .* kUnicodePassword\[\] = ' \
  --uncomment-static-func-impl TestImpl \
  --uncomment-func-impl TestCompat \
  --uncomment-gtest-func PKCS12Test TestOpenSSL \
  --uncomment-gtest-func PKCS12Test TestNSS \
  --uncomment-gtest-func PKCS12Test TestWindows \
  --uncomment-gtest-func PKCS12Test TestPBES2 \
  --uncomment-gtest-func PKCS12Test TestNoEncryption \
  --uncomment-gtest-func PKCS12Test TestEmptyPassword \
  --uncomment-gtest-func PKCS12Test TestNullPassword \
  --uncomment-gtest-func PKCS12Test TestUnicode \
  --uncomment-gtest-func PKCS12Test TestWindowsCompat \
