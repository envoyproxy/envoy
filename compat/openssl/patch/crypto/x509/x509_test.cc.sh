#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment \
  --uncomment-regex '#include' \
  --comment-regex '#include\s*"internal\.h"' \
  --comment-regex '#include\s*<openssl/mldsa\.h>' \
  --uncomment-regex 'BSSL_NAMESPACE_BEGIN' \
  --uncomment-regex 'BSSL_NAMESPACE_END' \
  --uncomment-regex 'namespace\s*{\s*$' \
  --uncomment-regex '}\s*//\s*namespace\s*$' \
  --uncomment-func-impl CertFromPEM \
  --uncomment-func-impl CRLFromPEM \
  --uncomment-func-impl PrivateKeyFromPEM \
  --uncomment-static-func-impl CertsToStack \
  --uncomment-static-func-impl CRLsToStack \
  --uncomment-regex 'static const .* kReferenceTime =' \
  --uncomment-static-func-impl Verify \
  --uncomment-gtest-func X509Test TestVerify \
  --uncomment-gtest-func X509Test VerifyThreads \
  --uncomment-gtest-func X509Test ManyNamesAndConstraints \
  --uncomment-static-func-impl MakeGeneralName \
  --uncomment-static-func-impl MakeTestCert \
  --uncomment-gtest-func-skip X509Test NameConstraints \
  --uncomment-gtest-func X509Test TestPSSBadParameters \
  --uncomment-gtest-func X509Test TestEd25519 \
  --uncomment-gtest-func X509Test X509NameSet \
  --uncomment-gtest-func X509Test NoBasicConstraintsCertSign \
  --uncomment-gtest-func X509Test NoBasicConstraintsNetscapeCA \
  --uncomment-gtest-func X509Test PEMX509Info \
  --uncomment-gtest-func-skip X509Test InvalidExtensions \
  --uncomment-gtest-func X509Test NullStore \
  --uncomment-gtest-func X509Test BasicConstraints \
  --uncomment-gtest-func X509Test AlgorithmParameters \
  --uncomment-gtest-func X509Test TrustedFirst \
  --uncomment-gtest-func-skip X509Test BER \
  --uncomment-func-impl MakeTestName \

for VAR in kCrossSigningRootPEM kRootCAPEM kRootCrossSignedPEM kIntermediatePEM kIntermediateSelfSignedPEM kLeafPEM kLeafNoKeyUsagePEM kForgeryPEM kBadPSSCertPEM kRSAKey kP256Key kBasicCRL kEd25519Cert kSANTypesRoot kNoBasicConstraintsCertSignIntermediate kNoBasicConstraintsCertSignLeaf kNoBasicConstraintsNetscapeCAIntermediate kNoBasicConstraintsNetscapeCALeaf kP256NoParam kP256NullParam kP256InvalidParam kRSANoParam kRSANullParam kRSAInvalidParam kConstructedBitString kConstructedOctetString kIndefiniteLength kNonZeroPadding kHighTagNumber kNonMinimalLengthSerial kNonMinimalLengthOuter kNonMinimalLengthSignature; do
  uncomment.sh "$1" --uncomment-regex-range 'static\s*const\s*.*\<'$VAR'\[\]\s*=' '[^;]*;\s*$'
done

# Add [[maybe_unused]] to variables only used under #ifndef BSSL_COMPAT
sed -i 's/^static const char kP256InvalidParam/[[maybe_unused]] static const char kP256InvalidParam/' "$1"
sed -i 's/^static const char kRSAInvalidParam/[[maybe_unused]] static const char kRSAInvalidParam/' "$1"
