#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment \
  --uncomment-regex '#include' \
  --comment-regex '#include "\.\./fipsmodule' \
  --comment-regex '#include "\.\./internal\.h"' \
  --comment-regex '#include "\.\./mem_internal\.h"' \
  --uncomment-regex 'BSSL_NAMESPACE_BEGIN' \
  --uncomment-regex 'BSSL_NAMESPACE_END' \
  --uncomment-regex 'namespace\s*{\s*$' \
  --uncomment-regex '}\s*//\s*namespace\s*$' \
  --uncomment-struct RSAEncryptParam \
  --uncomment-regex 'class\s*RSAEncryptTest\s*:' \
  --uncomment-gtest-func RSAEncryptTest TestKey \
  --uncomment-regex-range 'INSTANTIATE_TEST_SUITE_P(All, RSAEncryptTest' '.*);$' \
  --uncomment-gtest-func RSATest TestDecrypt \
  --uncomment-regex-range 'static const uint8_t kPKCS1Ciphertext2.*' '.*};'

for VAR in kKey1 kPlaintext kOAEPCiphertext1 kKey2 kOAEPCiphertext2 kKey3 kOAEPCiphertext3 ; do
  uncomment.sh "$1" --uncomment-regex-range 'static\s*const\s*.*\<'$VAR'\[\]\s*=' '[^;]*;\s*$'
done
