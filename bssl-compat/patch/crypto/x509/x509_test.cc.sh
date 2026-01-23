#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment \
  --uncomment-regex '#include' \
  --comment-regex '#include\s*"internal\.h"' \
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
  --uncomment-gtest-func X509Test NameConstraints \
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
  --uncomment-regex '#ifndef BSSL_COMPAT' \
  --uncomment-regex '#endif // BSSL_COMPAT'

for VAR in kCrossSigningRootPEM kRootCAPEM kRootCrossSignedPEM kIntermediatePEM kIntermediateSelfSignedPEM kLeafPEM kLeafNoKeyUsagePEM kForgeryPEM kBadPSSCertPEM kRSAKey kP256Key kBasicCRL kEd25519Cert kSANTypesRoot kNoBasicConstraintsCertSignIntermediate kNoBasicConstraintsCertSignLeaf kNoBasicConstraintsNetscapeCAIntermediate kNoBasicConstraintsNetscapeCALeaf kP256NoParam kP256NullParam kP256InvalidParam kRSANoParam kRSANullParam kRSAInvalidParam kConstructedBitString kConstructedOctetString kIndefiniteLength kNonZeroPadding kHighTagNumber kNonMinimalLengthSerial kNonMinimalLengthOuter kNonMinimalLengthSignature; do
  uncomment.sh "$1" --uncomment-regex-range 'static\s*const\s*.*\<'$VAR'\[\]\s*=' '[^;]*;\s*$'
done
