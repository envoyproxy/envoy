#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
  --comment-regex '#include\s*".*internal\.h' \
  --uncomment-regex 'namespace\s*{\s*$' \
  --uncomment-macro TRACED_CALL \
  --uncomment-struct VersionParam \
  --uncomment-regex-range 'static\s*const\s*VersionParam\s*kAllVersions\[\]\s*=' '};' \
  --uncomment-regex 'template\s*<typename\s*T>' \
  --uncomment-class UnownedSSLExData \
  --uncomment-gtest-func SSLTest CipherProperties \
  --uncomment-static-func-impl CertFromPEM \
  --uncomment-static-func-impl KeyFromPEM \
  --uncomment-static-func-impl GetTestCertificate \
  --uncomment-static-func-impl GetTestKey \
  --uncomment-static-func-impl CompleteHandshakes \
  --uncomment-static-func-impl FlushNewSessionTickets \
  --uncomment-static-func-impl CreateClientAndServer \
  --uncomment-struct ClientConfig \
  --uncomment-static-func-impl ConnectClientAndServer \
  --uncomment-regex 'static\s*.*\bg_last_session;\s*$' \
  --uncomment-static-func-impl SaveLastSession \
  --uncomment-static-func-impl CreateClientSession \
  --uncomment-gtest-func SSLTest ClientCAList \
  --uncomment-class SSLVersionTest \
  --uncomment-regex-range 'INSTANTIATE_TEST_SUITE_P(WithVersion, SSLVersionTest' '.*);' \
  --uncomment-gtest-func SSLVersionTest OneSidedShutdown \
  --uncomment-gtest-func SSLVersionTest WriteAfterShutdown \
  --uncomment-gtest-func SSLVersionTest WriteAfterReadSentFatalAlert \
  --uncomment-gtest-func SSLTest SetBIO \
  --uncomment-static-func-impl VerifySucceed \
  --uncomment-gtest-func SSLVersionTest GetPeerCertificate \
  --uncomment-gtest-func SSLVersionTest NoPeerCertificate \
  --uncomment-static-func-impl ExpectSessionReused \
  --uncomment-static-func-impl SwitchSessionIDContextSNI \
  --uncomment-gtest-func-skip SSLVersionTest SessionIDContext \
  --uncomment-static-func-impl GetVersionName \
  --uncomment-gtest-func SSLVersionTest Version \
  --uncomment-gtest-func SSLVersionTest GetServerName \
  --uncomment-gtest-func SSLTest GetCertificate \
  --uncomment-gtest-func SSLTest NoCiphersAvailable \
  --uncomment-gtest-func SSLTest GetCertificateThreads \
  --uncomment-regex 'int BORINGSSL_enum_c_type_test' '}' \
  --uncomment-gtest-func SSLTest EnumTypes \
  --uncomment-gtest-func-skip SSLVersionTest SameKeyResume \
  --uncomment-regex '}\s*//\s*namespace\s*$'