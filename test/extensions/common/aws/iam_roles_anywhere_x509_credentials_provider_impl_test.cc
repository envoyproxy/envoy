#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/base.pb.validate.h"
#include "envoy/extensions/common/aws/v3/credential_provider.pb.h"

#include "source/common/common/base64.h"
#include "source/extensions/common/aws/credentials_provider_impl.h"
#include "source/extensions/common/aws/metadata_fetcher.h"

#include "test/extensions/common/aws/mocks.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/listener_factory_context.h"
#include "test/mocks/upstream/cluster_update_callbacks.h"
#include "test/mocks/upstream/cluster_update_callbacks_handle.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"

using testing::InvokeWithoutArgs;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

class IAMRolesAnywhereX509CredentialsProviderTest : public testing::Test {
public:
  ~IAMRolesAnywhereX509CredentialsProviderTest() override = default;

  // Example certificates generated for test cases

  // Test ECDSA signed certificate - Issued from Root CA
  // Certificate:
  //     Data:
  //         Version: 3 (0x2)
  //         Serial Number:
  //             03:ec:7e:dc:37:a7:57:d3:f6:9a:30:9a:7d:91:a5:16
  //         Signature Algorithm: ecdsa-with-SHA256
  //         Issuer: CN = test-ecdsa-p256
  //         Validity
  //             Not Before: Nov 11 21:42:11 2024 GMT
  //             Not After : Nov 11 22:42:11 2025 GMT
  //         Subject: C = XX, L = Default City, O = Default Company Ltd, CN = test-ecdsa.test
  //         Subject Public Key Info:
  //                 Public-Key: (256 bit)
  //                 pub:
  //                     04:2e:1d:a1:d1:7c:db:e0:4e:38:82:bc:7d:7c:33:
  //                     5e:7f:e4:01:02:fd:70:f0:47:19:50:90:99:68:8c:
  //                     50:5f:de:42:31:0e:83:2f:f1:f6:7a:f9:21:be:a2:
  //                     60:7d:bd:01:fa:89:c4:28:62:b4:b0:cd:5b:a2:5d:
  //                     33:24:fb:85:d9
  //                 ASN1 OID: prime256v1
  //                 NIST CURVE: P-256
  //         X509v3 extensions:
  //             X509v3 Basic Constraints:
  //                 CA:FALSE
  //             X509v3 Authority Key Identifier:
  //                 32:89:A1:4A:00:AC:C1:96:76:5D:D8:D4:D4:CE:18:8D:1B:77:5A:4B
  //             X509v3 Subject Key Identifier:
  //                 37:EC:2F:8B:31:1C:42:FF:79:6E:2F:40:FA:22:DC:FB:2F:C4:76:A1
  //             X509v3 Key Usage: critical
  //                 Digital Signature, Key
  //             X509v3 Extended Key Usage:
  //                 TLS Web Server Authentication, TLS Web Client Authentication
  //     Signature Algorithm: ecdsa-with-SHA256
  //     Signature Value:
  //         30:44:02:20:0e:4c:50:b6:3a:ba:e4:c4:3f:bb:0b:6f:86:7a:
  //         2a:83:0d:0c:69:3b:1c:ba:cf:25:36:10:30:f0:1d:15:cb:cc:
  //         02:20:48:25:16:2f:32:e4:20:40:c9:a4:57:a0:c1:99:7a:d9:
  //         a6:b9:33:95:d1:be:ad:d1:69:c2:c9:6a:1d:2d:89:ef
  //

  std::string server_root_cert_ecdsa_der_b64 = R"EOF(
MIIB7zCCAZagAwIBAgIQA+x+3DenV9P2mjCafZGlFjAKBggqhkjOPQQDAjAaMRgwFgYDVQQDDA90
ZXN0LWVjZHNhLXAyNTYwHhcNMjQxMTExMjE0MjExWhcNMjUxMTExMjI0MjExWjBcMQswCQYDVQQG
EwJYWDEVMBMGA1UEBwwMRGVmYXVsdCBDaXR5MRwwGgYDVQQKDBNEZWZhdWx0IENvbXBhbnkgTHRk
MRgwFgYDVQQDDA90ZXN0LWVjZHNhLnRlc3QwWTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAAQuHaHR
fNvgTjiCvH18M15/5AEC/XDwRxlQkJlojFBf3kIxDoMv8fZ6+SG+omB9vQH6icQoYrSwzVuiXTMk
+4XZo3wwejAJBgNVHRMEAjAAMB8GA1UdIwQYMBaAFDKJoUoArMGWdl3Y1NTOGI0bd1pLMB0GA1Ud
DgQWBBQ37C+LMRxC/3luL0D6Itz7L8R2oTAOBgNVHQ8BAf8EBAMCBaAwHQYDVR0lBBYwFAYIKwYB
BQUHAwEGCCsGAQUFBwMCMAoGCCqGSM49BAMCA0cAMEQCIA5MULY6uuTEP7sLb4Z6KoMNDGk7HLrP
JTYQMPAdFcvMAiBIJRYvMuQgQMmkV6DBmXrZprkzldG+rdFpwslqHS2J7w==
)EOF";

  std::string server_root_cert_ecdsa_pem = R"EOF(
-----BEGIN CERTIFICATE-----
MIIB7zCCAZagAwIBAgIQA+x+3DenV9P2mjCafZGlFjAKBggqhkjOPQQDAjAaMRgw
FgYDVQQDDA90ZXN0LWVjZHNhLXAyNTYwHhcNMjQxMTExMjE0MjExWhcNMjUxMTEx
MjI0MjExWjBcMQswCQYDVQQGEwJYWDEVMBMGA1UEBwwMRGVmYXVsdCBDaXR5MRww
GgYDVQQKDBNEZWZhdWx0IENvbXBhbnkgTHRkMRgwFgYDVQQDDA90ZXN0LWVjZHNh
LnRlc3QwWTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAAQuHaHRfNvgTjiCvH18M15/
5AEC/XDwRxlQkJlojFBf3kIxDoMv8fZ6+SG+omB9vQH6icQoYrSwzVuiXTMk+4XZ
o3wwejAJBgNVHRMEAjAAMB8GA1UdIwQYMBaAFDKJoUoArMGWdl3Y1NTOGI0bd1pL
MB0GA1UdDgQWBBQ37C+LMRxC/3luL0D6Itz7L8R2oTAOBgNVHQ8BAf8EBAMCBaAw
HQYDVR0lBBYwFAYIKwYBBQUHAwEGCCsGAQUFBwMCMAoGCCqGSM49BAMCA0cAMEQC
IA5MULY6uuTEP7sLb4Z6KoMNDGk7HLrPJTYQMPAdFcvMAiBIJRYvMuQgQMmkV6DB
mXrZprkzldG+rdFpwslqHS2J7w==
-----END CERTIFICATE-----
)EOF";

  std::string server_root_private_key_ecdsa_pem = R"EOF(
-----BEGIN EC PRIVATE KEY-----
MHcCAQEEIC96bc9hG8Aa3vHDAogZOkndTg0XmyWR7hw7i5dqGdcwoAoGCCqGSM49
AwEHoUQDQgAELh2h0Xzb4E44grx9fDNef+QBAv1w8EcZUJCZaIxQX95CMQ6DL/H2
evkhvqJgfb0B+onEKGK0sM1bol0zJPuF2Q==
-----END EC PRIVATE KEY-----
)EOF";

  // Test ECDSA signed certificate - Issued from Subordinate CA
  // Certificate:
  //     Data:
  //         Version: 3 (0x2)
  //         Serial Number:
  //             39:21:bc:e7:f3:15:1a:4b:7f:ef:54:ef:ef:54:c4:09:a5:03:0f:3a
  //         Signature Algorithm: ecdsa-with-SHA256
  //         Issuer: C = XX, L = Default City, O = Default Company Ltd, CN =
  //         test-ecdsa-p256-subordinate Validity
  //             Not Before: Nov 17 10:44:05 2024 GMT
  //             Not After : Nov 17 10:44:05 2025 GMT
  //         Subject: C = XX, L = Default City, O = Default Company Ltd, CN =
  //         test-ecdsa-p256-subordinate Subject Public Key Info:
  //                 Public-Key: (256 bit)
  //                 pub:
  //                     04:49:81:46:65:f7:22:74:e4:a8:16:6e:0e:ef:cf:
  //                     f0:b8:a3:50:75:e4:82:c9:9d:c5:f1:a9:50:73:3b:
  //                     c1:42:a7:e6:7e:47:dd:4e:bc:e9:33:f9:1a:c0:ad:
  //                     09:20:f6:35:6c:fc:01:1a:2e:d0:f0:8b:e7:b2:7d:
  //                     d8:02:91:74:d8
  //                 ASN1 OID: prime256v1
  //                 NIST CURVE: P-256
  //         X509v3 extensions:
  //             X509v3 Subject Key Identifier:
  //                 53:A2:C6:B8:5A:AA:37:4B:67:9B:E9:20:1E:C2:F3:01:7C:BC:0A:CD
  //             X509v3 Authority Key Identifier:
  //                 53:A2:C6:B8:5A:AA:37:4B:67:9B:E9:20:1E:C2:F3:01:7C:BC:0A:CD
  //             X509v3 Basic Constraints: critical
  //                 CA:TRUE
  //     Signature Algorithm: ecdsa-with-SHA256
  //     Signature Value:
  //         30:45:02:20:2f:ab:3c:58:06:04:4b:c5:00:01:56:46:e8:b5:
  //         c2:f6:84:20:58:e0:30:97:74:3d:e8:6a:43:e5:61:68:55:34:
  //         02:21:00:89:1e:c6:1a:67:e0:b1:e1:20:4b:a2:a5:28:30:18:
  //         6f:22:63:24:85:2a:4d:ee:9a:92:75:02:d1:31:53:2b:70

  std::string server_subordinate_cert_ecdsa_pem = R"EOF(
-----BEGIN CERTIFICATE-----
MIICJTCCAcugAwIBAgIUOSG85/MVGkt/71Tv71TECaUDDzowCgYIKoZIzj0EAwIw
aDELMAkGA1UEBhMCWFgxFTATBgNVBAcMDERlZmF1bHQgQ2l0eTEcMBoGA1UECgwT
RGVmYXVsdCBDb21wYW55IEx0ZDEkMCIGA1UEAwwbdGVzdC1lY2RzYS1wMjU2LXN1
Ym9yZGluYXRlMB4XDTI0MTExNzEwNDQwNVoXDTI1MTExNzEwNDQwNVowaDELMAkG
A1UEBhMCWFgxFTATBgNVBAcMDERlZmF1bHQgQ2l0eTEcMBoGA1UECgwTRGVmYXVs
dCBDb21wYW55IEx0ZDEkMCIGA1UEAwwbdGVzdC1lY2RzYS1wMjU2LXN1Ym9yZGlu
YXRlMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAESYFGZfcidOSoFm4O78/wuKNQ
deSCyZ3F8alQczvBQqfmfkfdTrzpM/kawK0JIPY1bPwBGi7Q8Ivnsn3YApF02KNT
MFEwHQYDVR0OBBYEFFOixrhaqjdLZ5vpIB7C8wF8vArNMB8GA1UdIwQYMBaAFFOi
xrhaqjdLZ5vpIB7C8wF8vArNMA8GA1UdEwEB/wQFMAMBAf8wCgYIKoZIzj0EAwID
SAAwRQIgL6s8WAYES8UAAVZG6LXC9oQgWOAwl3Q96GpD5WFoVTQCIQCJHsYaZ+Cx
4SBLoqUoMBhvImMkhSpN7pqSdQLRMVMrcA==
-----END CERTIFICATE-----
)EOF";

  std::string server_subordinate_cert_ecdsa_der_b64 = R"EOF(
MIICJTCCAcugAwIBAgIUOSG85/MVGkt/71Tv71TECaUDDzowCgYIKoZIzj0EAwIwaDELMAkGA1UE
BhMCWFgxFTATBgNVBAcMDERlZmF1bHQgQ2l0eTEcMBoGA1UECgwTRGVmYXVsdCBDb21wYW55IEx0
ZDEkMCIGA1UEAwwbdGVzdC1lY2RzYS1wMjU2LXN1Ym9yZGluYXRlMB4XDTI0MTExNzEwNDQwNVoX
DTI1MTExNzEwNDQwNVowaDELMAkGA1UEBhMCWFgxFTATBgNVBAcMDERlZmF1bHQgQ2l0eTEcMBoG
A1UECgwTRGVmYXVsdCBDb21wYW55IEx0ZDEkMCIGA1UEAwwbdGVzdC1lY2RzYS1wMjU2LXN1Ym9y
ZGluYXRlMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAESYFGZfcidOSoFm4O78/wuKNQdeSCyZ3F
8alQczvBQqfmfkfdTrzpM/kawK0JIPY1bPwBGi7Q8Ivnsn3YApF02KNTMFEwHQYDVR0OBBYEFFOi
xrhaqjdLZ5vpIB7C8wF8vArNMB8GA1UdIwQYMBaAFFOixrhaqjdLZ5vpIB7C8wF8vArNMA8GA1Ud
EwEB/wQFMAMBAf8wCgYIKoZIzj0EAwIDSAAwRQIgL6s8WAYES8UAAVZG6LXC9oQgWOAwl3Q96GpD
5WFoVTQCIQCJHsYaZ+Cx4SBLoqUoMBhvImMkhSpN7pqSdQLRMVMrcA==
)EOF";

  std::string server_subordinate_private_key_ecdsa_pem = R"EOF(
-----BEGIN EC PARAMETERS-----
BggqhkjOPQMBBw==
-----END EC PARAMETERS-----
-----BEGIN EC PRIVATE KEY-----
MHcCAQEEIPzRePzrwYiOSrv0MLVSJEw6fEvaJVYDzVBrYEGlS45OoAoGCCqGSM49
AwEHoUQDQgAESYFGZfcidOSoFm4O78/wuKNQdeSCyZ3F8alQczvBQqfmfkfdTrzp
M/kawK0JIPY1bPwBGi7Q8Ivnsn3YApF02A==
-----END EC PRIVATE KEY-----
)EOF";

  std::string server_subordinate_chain_ecdsa_pem = R"EOF(
-----BEGIN CERTIFICATE-----
MIIBpDCCAUqgAwIBAgIQEtGBpKhUy68xOvOSxGaJKDAKBggqhkjOPQQDAjAaMRgw
FgYDVQQDDA90ZXN0LWVjZHNhLXAyNTYwHhcNMjQxMTE3MDkzOTUyWhcNMjUxMTIx
MTAzOTE0WjAmMSQwIgYDVQQDDBt0ZXN0LWVjZHNhLXAyNTYtc3Vib3JkaW5hdGUw
WTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAAQpX+Tfr1UOeCVwxKb1FveTbAIG7J5Q
JmoWi8DxuXv4R2D/yKtjL0ezPLIfonVCigodW6lVNRtJUeuG5s/w7lfmo2YwZDAS
BgNVHRMBAf8ECDAGAQH/AgEAMB8GA1UdIwQYMBaAFDKJoUoArMGWdl3Y1NTOGI0b
d1pLMB0GA1UdDgQWBBSWdfzMJkRqFUIwO382QI80sO9hPDAOBgNVHQ8BAf8EBAMC
AYYwCgYIKoZIzj0EAwIDSAAwRQIhAKHYHe2Tmc1BGnw1xT2Tn8RX26Hf1oY+K3QW
7wRhrRQzAiAnfZLHoyJWU57VyYhP1mMAQY7bO5plRGoh2PSScFb59A==
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIBdTCCARugAwIBAgIRAPfOpjFoMikiVrNYSaU01BcwCgYIKoZIzj0EAwIwGjEY
MBYGA1UEAwwPdGVzdC1lY2RzYS1wMjU2MB4XDTI0MTExMTIxMjYyMFoXDTM0MTEx
MTIyMjYwN1owGjEYMBYGA1UEAwwPdGVzdC1lY2RzYS1wMjU2MFkwEwYHKoZIzj0C
AQYIKoZIzj0DAQcDQgAERdsHH8nBE8nC2i5oa1FNhsSpNf/+KtO56HiPNf1I61vV
h09pqLKnI05esqlQapZmFeUYM4ZXWb6UR/b67u05J6NCMEAwDwYDVR0TAQH/BAUw
AwEB/zAdBgNVHQ4EFgQUMomhSgCswZZ2XdjU1M4YjRt3WkswDgYDVR0PAQH/BAQD
AgGGMAoGCCqGSM49BAMCA0gAMEUCIDp85MaWSMiOi4Gu7p/vW5L0fTchzwu6o6BU
pbvDxRwtAiEA2586MbVLvM9wZ0cw+GqfDPzJVQiJUIZouRZ5V3XdQFY=
-----END CERTIFICATE-----
)EOF";

  std::string server_subordinate_chain_ecdsa_der_b64 = R"EOF(
MIIBpDCCAUqgAwIBAgIQEtGBpKhUy68xOvOSxGaJKDAKBggqhkjOPQQDAjAaMRgwFgYDVQQDDA90
ZXN0LWVjZHNhLXAyNTYwHhcNMjQxMTE3MDkzOTUyWhcNMjUxMTIxMTAzOTE0WjAmMSQwIgYDVQQD
DBt0ZXN0LWVjZHNhLXAyNTYtc3Vib3JkaW5hdGUwWTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAAQp
X+Tfr1UOeCVwxKb1FveTbAIG7J5QJmoWi8DxuXv4R2D/yKtjL0ezPLIfonVCigodW6lVNRtJUeuG
5s/w7lfmo2YwZDASBgNVHRMBAf8ECDAGAQH/AgEAMB8GA1UdIwQYMBaAFDKJoUoArMGWdl3Y1NTO
GI0bd1pLMB0GA1UdDgQWBBSWdfzMJkRqFUIwO382QI80sO9hPDAOBgNVHQ8BAf8EBAMCAYYwCgYI
KoZIzj0EAwIDSAAwRQIhAKHYHe2Tmc1BGnw1xT2Tn8RX26Hf1oY+K3QW7wRhrRQzAiAnfZLHoyJW
U57VyYhP1mMAQY7bO5plRGoh2PSScFb59A==,MIIBdTCCARugAwIBAgIRAPfOpjFoMikiVrNYSaU01BcwCgYIKoZIzj0EAwIwGjEYMBYGA1UEAwwP
dGVzdC1lY2RzYS1wMjU2MB4XDTI0MTExMTIxMjYyMFoXDTM0MTExMTIyMjYwN1owGjEYMBYGA1UE
AwwPdGVzdC1lY2RzYS1wMjU2MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAERdsHH8nBE8nC2i5o
a1FNhsSpNf/+KtO56HiPNf1I61vVh09pqLKnI05esqlQapZmFeUYM4ZXWb6UR/b67u05J6NCMEAw
DwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUMomhSgCswZZ2XdjU1M4YjRt3WkswDgYDVR0PAQH/
BAQDAgGGMAoGCCqGSM49BAMCA0gAMEUCIDp85MaWSMiOi4Gu7p/vW5L0fTchzwu6o6BUpbvDxRwt
AiEA2586MbVLvM9wZ0cw+GqfDPzJVQiJUIZouRZ5V3XdQFY=
)EOF";

  // Test RSA signed certificate - Issued from Root CA
  // Certificate:
  //     Data:
  //         Version: 3 (0x2)
  //         Serial Number:
  //             63:2d:25:2d:cd:9a:7f:8e:ff:bb:9d:a2:d3:a4:42:a8
  //         Signature Algorithm: sha256WithRSAEncryption
  //         Issuer: CN = test-rsa
  //         Validity
  //             Not Before: Nov 17 23:23:37 2024 GMT
  //             Not After : Nov 18 00:23:37 2025 GMT
  //         Subject: C = XX, L = Default City, O = Default Company Ltd, CN = test-rsa.test
  //         Subject Public Key Info:
  //             Public Key Algorithm: rsaEncryption
  //                 Public-Key: (2048 bit)
  //                 Modulus:
  //                     00:aa:30:47:af:04:3f:40:ca:9f:a4:40:bf:ee:5e:
  //                     c8:01:66:4e:08:6f:6a:ab:b6:2d:8d:43:01:48:ff:
  //                     9c:2d:10:2d:fe:a9:2d:5d:8d:06:bf:bf:af:35:7a:
  //                     9e:4c:af:e7:68:a6:2a:2e:fe:af:29:c7:a9:d7:fb:
  //                     74:47:59:1c:80:5f:40:79:f2:36:07:51:a6:a7:f8:
  //                     80:7f:ee:d7:94:6d:fa:5c:a0:2e:32:c6:64:d1:22:
  //                     e0:dd:4b:e7:ab:04:2d:87:d7:21:da:e6:70:70:03:
  //                     e6:0d:a7:e2:94:eb:26:fc:65:5b:5b:49:36:5b:90:
  //                     9b:91:68:a0:75:00:51:1c:fd:a3:fe:b2:e9:11:82:
  //                     fa:71:79:2e:a9:15:4f:54:3b:60:06:87:6d:58:28:
  //                     e3:3b:c2:79:e9:ee:4d:8e:55:e2:19:cb:90:63:9a:
  //                     46:97:3b:04:2e:f8:fa:00:87:a6:e5:a6:a3:7b:05:
  //                     d0:fc:9d:41:90:87:3b:a1:ca:bd:7f:9a:87:50:76:
  //                     d0:15:78:87:a2:c1:6d:6b:88:a6:50:c4:69:5e:77:
  //                     eb:6f:2e:e3:2b:77:c5:a2:02:64:83:d4:f0:5a:5b:
  //                     aa:7a:9a:df:82:94:d9:2b:ea:0c:d7:06:df:6b:14:
  //                     0f:2b:c9:c6:17:f9:af:79:02:26:a6:42:92:a8:aa:
  //                     84:49
  //                 Exponent: 65537 (0x10001)
  //         X509v3 extensions:
  //             X509v3 Basic Constraints:
  //                 CA:FALSE
  //             X509v3 Authority Key Identifier:
  //                 41:E2:65:FF:60:32:8D:5F:D9:EC:A0:A5:D3:FA:EC:B0:DE:A6:C2:CF
  //             X509v3 Subject Key Identifier:
  //                 A1:4A:95:75:4D:81:4E:51:FB:D9:62:46:E2:3F:2C:02:79:AD:97:51
  //             X509v3 Key Usage: critical
  //                 Digital Signature, Key
  //             X509v3 Extended Key Usage:
  //                 TLS Web Server Authentication, TLS Web Client Authentication
  //     Signature Algorithm: sha256WithRSAEncryption
  //     Signature Value:
  //         01:db:ce:a7:d0:ff:79:dc:71:48:a6:aa:08:20:b1:42:c3:28:
  //         41:6d:88:d5:1a:2c:04:27:c4:a0:ef:8b:5a:f9:8e:d2:e9:56:
  //         4b:4a:fd:1a:8f:5f:09:3e:a1:30:6b:b0:d1:be:09:e6:29:5e:
  //         94:05:99:85:fe:74:f5:91:50:c4:ca:27:2f:b6:67:3c:56:aa:
  //         cb:f8:7b:14:71:32:9f:28:ca:ad:3c:80:4c:a6:ec:5f:6d:8e:
  //         ec:a0:54:bb:cf:23:64:6e:81:65:50:fb:4f:ad:4e:d0:3e:3c:
  //         f6:f0:bb:02:2d:ab:a1:a0:e2:f0:87:96:61:66:b1:8e:4b:5d:
  //         f4:08:95:5c:13:a2:12:9e:8b:08:93:bd:77:e4:2e:e8:27:83:
  //         23:e8:67:c6:23:22:fb:ad:e8:b6:06:23:a4:8d:94:29:d4:c7:
  //         f3:3f:54:73:60:cf:fd:c5:f2:10:df:1e:48:d0:53:c7:c4:ae:
  //         e7:15:2c:e5:30:0b:b0:3d:d7:7c:24:c5:eb:88:39:05:5f:02:
  //         15:d5:da:c9:80:77:3d:51:c4:0c:b2:3c:e3:83:51:08:8d:ed:
  //         69:15:f6:52:da:2c:e3:83:0a:81:7b:9b:f4:bb:4f:b1:a2:63:
  //         cf:a8:1e:1b:ce:4d:d2:97:f5:38:d1:c1:15:c1:06:43:e9:1a:
  //         c8:91:41:09

  std::string server_root_cert_rsa_pem = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDczCCAlugAwIBAgIQYy0lLc2af47/u52i06RCqDANBgkqhkiG9w0BAQsFADAT
MREwDwYDVQQDDAh0ZXN0LXJzYTAeFw0yNDExMTcyMzIzMzdaFw0yNTExMTgwMDIz
MzdaMFoxCzAJBgNVBAYTAlhYMRUwEwYDVQQHDAxEZWZhdWx0IENpdHkxHDAaBgNV
BAoME0RlZmF1bHQgQ29tcGFueSBMdGQxFjAUBgNVBAMMDXRlc3QtcnNhLnRlc3Qw
ggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCqMEevBD9Ayp+kQL/uXsgB
Zk4Ib2qrti2NQwFI/5wtEC3+qS1djQa/v681ep5Mr+dopiou/q8px6nX+3RHWRyA
X0B58jYHUaan+IB/7teUbfpcoC4yxmTRIuDdS+erBC2H1yHa5nBwA+YNp+KU6yb8
ZVtbSTZbkJuRaKB1AFEc/aP+sukRgvpxeS6pFU9UO2AGh21YKOM7wnnp7k2OVeIZ
y5BjmkaXOwQu+PoAh6blpqN7BdD8nUGQhzuhyr1/modQdtAVeIeiwW1riKZQxGle
d+tvLuMrd8WiAmSD1PBaW6p6mt+ClNkr6gzXBt9rFA8rycYX+a95AiamQpKoqoRJ
AgMBAAGjfDB6MAkGA1UdEwQCMAAwHwYDVR0jBBgwFoAUQeJl/2AyjV/Z7KCl0/rs
sN6mws8wHQYDVR0OBBYEFKFKlXVNgU5R+9liRuI/LAJ5rZdRMA4GA1UdDwEB/wQE
AwIFoDAdBgNVHSUEFjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwDQYJKoZIhvcNAQEL
BQADggEBAAHbzqfQ/3nccUimqgggsULDKEFtiNUaLAQnxKDvi1r5jtLpVktK/RqP
Xwk+oTBrsNG+CeYpXpQFmYX+dPWRUMTKJy+2ZzxWqsv4exRxMp8oyq08gEym7F9t
juygVLvPI2RugWVQ+0+tTtA+PPbwuwItq6Gg4vCHlmFmsY5LXfQIlVwTohKeiwiT
vXfkLugngyPoZ8YjIvut6LYGI6SNlCnUx/M/VHNgz/3F8hDfHkjQU8fErucVLOUw
C7A913wkxeuIOQVfAhXV2smAdz1RxAyyPOODUQiN7WkV9lLaLOODCoF7m/S7T7Gi
Y8+oHhvOTdKX9TjRwRXBBkPpGsiRQQk=
-----END CERTIFICATE-----
)EOF";

  std::string server_root_chain_rsa_pem = R"EOF(
-----BEGIN CERTIFICATE-----
MIIC8zCCAdugAwIBAgIRAKPuvF4B35jaiV9QrXSH9hIwDQYJKoZIhvcNAQELBQAw
EzERMA8GA1UEAwwIdGVzdC1yc2EwHhcNMjQxMTE3MjMyMzMzWhcNMzQxMTE4MDAy
MzMxWjATMREwDwYDVQQDDAh0ZXN0LXJzYTCCASIwDQYJKoZIhvcNAQEBBQADggEP
ADCCAQoCggEBAK+Om/cFBnAM8hytvuKf+sIjjqzQcdedKpk9Jq4T5f8E/arlFQws
CulSLt7Cb45yJ9J6DuiLiFf4o31eCtwrZn47KZcysWmG5Q992OI0vdQgeQMidbUV
DXmcv20iawx3FKoU/LIrnFkLlKWivhbjrAX9pgiL2bQeMvlt/oISevlUi0SYkeCJ
l2X5hVOTnRmCDHoeWUegRoZLSkPH/eQBsNwkVY+2IxwXcRCxozFjV1wPcoNPp0+2
KwGAJ16oUUum9SqIUFcZx9DB3gnIvAu9BxgLJ6KSvs64DVDmWcrHpo+ZU9JsKeBI
9GXOOrCaYZwglxLt7GY6OvNdWPCabkPhJCcCAwEAAaNCMEAwDwYDVR0TAQH/BAUw
AwEB/zAdBgNVHQ4EFgQUQeJl/2AyjV/Z7KCl0/rssN6mws8wDgYDVR0PAQH/BAQD
AgGGMA0GCSqGSIb3DQEBCwUAA4IBAQBYxHCbtZZGpOtLYIvTnGJDns8D0Hc3eaoi
fmhmIPEdmcmxlHXqKyh5HyRdQ+sklNfVdjxBmwQDE96Tx9o3q3Xdfp2AHEaupzQx
XYtl50W49OuHzelXicOZY7aVe5ixb4l8m5UT7bO3A6RG+qgL1SRZbVftDGO2NxFe
iO8Kg7h3ti564Vv6I4SNoZEjkDKg8NWfDN1NYqd2FN1Crzida5RZf5fIXxnjAmnX
ubewSfcSGN98K+IREpCWSbf9DMkaeHx6Sw85UjovZU2KgedQHkQ0bhsXqY1PDlP3
WgTQAfHx04TA8rljw5lyGxOZJQ3WIvsc4qCn2Q1Dv+AjpLNZq411
-----END CERTIFICATE-----
)EOF";

  std::string server_root_chain_rsa_der_b64 = R"EOF(
MIIC8zCCAdugAwIBAgIRAKPuvF4B35jaiV9QrXSH9hIwDQYJKoZIhvcNAQELBQAwEzERMA8GA1UE
AwwIdGVzdC1yc2EwHhcNMjQxMTE3MjMyMzMzWhcNMzQxMTE4MDAyMzMxWjATMREwDwYDVQQDDAh0
ZXN0LXJzYTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAK+Om/cFBnAM8hytvuKf+sIj
jqzQcdedKpk9Jq4T5f8E/arlFQwsCulSLt7Cb45yJ9J6DuiLiFf4o31eCtwrZn47KZcysWmG5Q99
2OI0vdQgeQMidbUVDXmcv20iawx3FKoU/LIrnFkLlKWivhbjrAX9pgiL2bQeMvlt/oISevlUi0SY
keCJl2X5hVOTnRmCDHoeWUegRoZLSkPH/eQBsNwkVY+2IxwXcRCxozFjV1wPcoNPp0+2KwGAJ16o
UUum9SqIUFcZx9DB3gnIvAu9BxgLJ6KSvs64DVDmWcrHpo+ZU9JsKeBI9GXOOrCaYZwglxLt7GY6
OvNdWPCabkPhJCcCAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUQeJl/2AyjV/Z
7KCl0/rssN6mws8wDgYDVR0PAQH/BAQDAgGGMA0GCSqGSIb3DQEBCwUAA4IBAQBYxHCbtZZGpOtL
YIvTnGJDns8D0Hc3eaoifmhmIPEdmcmxlHXqKyh5HyRdQ+sklNfVdjxBmwQDE96Tx9o3q3Xdfp2A
HEaupzQxXYtl50W49OuHzelXicOZY7aVe5ixb4l8m5UT7bO3A6RG+qgL1SRZbVftDGO2NxFeiO8K
g7h3ti564Vv6I4SNoZEjkDKg8NWfDN1NYqd2FN1Crzida5RZf5fIXxnjAmnXubewSfcSGN98K+IR
EpCWSbf9DMkaeHx6Sw85UjovZU2KgedQHkQ0bhsXqY1PDlP3WgTQAfHx04TA8rljw5lyGxOZJQ3W
Ivsc4qCn2Q1Dv+AjpLNZq411
)EOF";

  std::string server_root_cert_rsa_der_b64 = R"EOF(
MIIDczCCAlugAwIBAgIQYy0lLc2af47/u52i06RCqDANBgkqhkiG9w0BAQsFADATMREwDwYDVQQD
DAh0ZXN0LXJzYTAeFw0yNDExMTcyMzIzMzdaFw0yNTExMTgwMDIzMzdaMFoxCzAJBgNVBAYTAlhY
MRUwEwYDVQQHDAxEZWZhdWx0IENpdHkxHDAaBgNVBAoME0RlZmF1bHQgQ29tcGFueSBMdGQxFjAU
BgNVBAMMDXRlc3QtcnNhLnRlc3QwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCqMEev
BD9Ayp+kQL/uXsgBZk4Ib2qrti2NQwFI/5wtEC3+qS1djQa/v681ep5Mr+dopiou/q8px6nX+3RH
WRyAX0B58jYHUaan+IB/7teUbfpcoC4yxmTRIuDdS+erBC2H1yHa5nBwA+YNp+KU6yb8ZVtbSTZb
kJuRaKB1AFEc/aP+sukRgvpxeS6pFU9UO2AGh21YKOM7wnnp7k2OVeIZy5BjmkaXOwQu+PoAh6bl
pqN7BdD8nUGQhzuhyr1/modQdtAVeIeiwW1riKZQxGled+tvLuMrd8WiAmSD1PBaW6p6mt+ClNkr
6gzXBt9rFA8rycYX+a95AiamQpKoqoRJAgMBAAGjfDB6MAkGA1UdEwQCMAAwHwYDVR0jBBgwFoAU
QeJl/2AyjV/Z7KCl0/rssN6mws8wHQYDVR0OBBYEFKFKlXVNgU5R+9liRuI/LAJ5rZdRMA4GA1Ud
DwEB/wQEAwIFoDAdBgNVHSUEFjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwDQYJKoZIhvcNAQELBQAD
ggEBAAHbzqfQ/3nccUimqgggsULDKEFtiNUaLAQnxKDvi1r5jtLpVktK/RqPXwk+oTBrsNG+CeYp
XpQFmYX+dPWRUMTKJy+2ZzxWqsv4exRxMp8oyq08gEym7F9tjuygVLvPI2RugWVQ+0+tTtA+PPbw
uwItq6Gg4vCHlmFmsY5LXfQIlVwTohKeiwiTvXfkLugngyPoZ8YjIvut6LYGI6SNlCnUx/M/VHNg
z/3F8hDfHkjQU8fErucVLOUwC7A913wkxeuIOQVfAhXV2smAdz1RxAyyPOODUQiN7WkV9lLaLOOD
CoF7m/S7T7GiY8+oHhvOTdKX9TjRwRXBBkPpGsiRQQk=
)EOF";

  std::string server_root_private_key_rsa_pem = R"EOF(
-----BEGIN PRIVATE KEY-----
MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQCqMEevBD9Ayp+k
QL/uXsgBZk4Ib2qrti2NQwFI/5wtEC3+qS1djQa/v681ep5Mr+dopiou/q8px6nX
+3RHWRyAX0B58jYHUaan+IB/7teUbfpcoC4yxmTRIuDdS+erBC2H1yHa5nBwA+YN
p+KU6yb8ZVtbSTZbkJuRaKB1AFEc/aP+sukRgvpxeS6pFU9UO2AGh21YKOM7wnnp
7k2OVeIZy5BjmkaXOwQu+PoAh6blpqN7BdD8nUGQhzuhyr1/modQdtAVeIeiwW1r
iKZQxGled+tvLuMrd8WiAmSD1PBaW6p6mt+ClNkr6gzXBt9rFA8rycYX+a95Aiam
QpKoqoRJAgMBAAECggEAAkzrhSM9rySmBoh9B632jmVJf/3wj1Bjun15wJi67dWC
h6cWBsYTnacryUFmbyMwEbcwSgkViU8qfbHHkzjSRK507skOP6hUBEB8zS3ncllP
uW2NXlCV94k9CKTAZYyFiIjpC15SzgLReuUGcCyjDuWYV+osDs4MOkmTpK07y3Rh
HOCRoPcjbboyiP/7OS8XvTC+WojNmLBM2GFyXe3zw75I8qXglLjp2s+aKzszo9TN
k2YCCd90PxiqbII2A4TYUB+utRpZ2p4QH5l2+X2EOHwf96qFhRpPKSJDpQp+hsm1
OTitFJWrRzBwG862ET0Mm14KEmUynv462YKz2mMHAQKBgQDPze2dDAzEJqou41e1
SX7klqPMiI/Cr2fL64G4PdoXVf8SmdA69sWksvlv2+TIzA66dVbDq0fMJYOj3hPM
EDB5x7mQEmqR5yfW+rA7xNMxfPSK4deAFbuXZWQbNPHr+3fmcoz9MNyu0mz26t3H
F5Ib42M6tY5kZzGzQT79W7t7SQKBgQDRqPhM154Mt8ow80lLiUES7NeDsCmUSXw6
RwSo2zXER05w4DMYfmWHcXZ/LIMd9HnY2nUFejoB2P8JGkLgB6zbFH+aBNgBX5V4
6vxx7bp3x/utxSdc1f39697BuXrtv6qUn4I4NyIm/rj/vqBDXNRj79J1utdd8U+c
d/aVGBXBAQKBgQCMA0cvQpgzbY3LC9jjyAJciHcS74xVc5PvHN4JQnt4r7OuV76q
i+y9PO2+BZ4QARWHYlo0empkzX314kLagqn207Bet1ngtqvsOHqXutVFidjG3sYx
gfMkXedmQXUjOAsgVVxTmCGJFTTf5X3KkEIc0kfgncW0NqeRDMwhLzaSKQKBgBxN
cg91f/l5igrnnLpcsfMrE8DMNCC3dtSrJ57f0LdJZPZp3Zvt3CjXkUaDrMOLcDNs
8iUmJdSABZWl/OcfQh9k+gDBrKMq0xO6rQ94JxbqYThJCBJJNPtlLvH55vVXTWC4
06xhDPQ0qKalhh7x1h4TjtajvVUKMVQPAbOIx88BAoGANUyfP2m4QdtHR3hOE8W8
U+p2qcbT0/02ggwwdKALqlmUaWgljoG8rarmV8ISc82j2B9fEgJR2z5frA/EKzV5
RBqAC6sxyAYn2wbzuyINJdSLpehQKDkKxEnO4QLodClHYV1F9AlAfbSLmIlRFv/y
1mNT8ElsYLTkJr2AqGNZOlQ=
-----END PRIVATE KEY-----
)EOF";

  void removeSubstrs(std::string& s, std::string p) {
    std::string::size_type n = p.length();

    for (std::string::size_type i = s.find(p); i != std::string::npos; i = s.find(p)) {
      s.erase(i, n);
    }
  }

  Event::DispatcherPtr setupDispatcher() {
    auto dispatcher = std::make_unique<Event::MockDispatcher>();
    EXPECT_CALL(*dispatcher, createFilesystemWatcher_()).WillRepeatedly(InvokeWithoutArgs([this] {
      Filesystem::MockWatcher* mock_watcher = new Filesystem::MockWatcher();
      EXPECT_CALL(*mock_watcher, addWatch(_, Filesystem::Watcher::Events::Modified, _))
          .WillRepeatedly(
              Invoke([this](absl::string_view, uint32_t, Filesystem::Watcher::OnChangedCb cb) {
                watch_cbs_.push_back(cb);
                return absl::OkStatus();
              }));
      return mock_watcher;
    }));
    return dispatcher;
  }
  void SetUp() override {

    removeSubstrs(server_root_cert_ecdsa_der_b64, "\n");
    removeSubstrs(server_subordinate_cert_ecdsa_der_b64, "\n");
    removeSubstrs(server_subordinate_chain_ecdsa_der_b64, "\n");
    removeSubstrs(server_root_cert_rsa_der_b64, "\n");
    removeSubstrs(server_root_chain_rsa_der_b64, "\n");
  }

  std::vector<Filesystem::Watcher::OnChangedCb> watch_cbs_;
  Event::DispatcherPtr dispatcher_;
  Api::ApiPtr api_;
};

TEST_F(IAMRolesAnywhereX509CredentialsProviderTest, InvalidSource) {
  envoy::config::core::v3::DataSource certificate_data_source, private_key_data_source,
      cert_chain_data_source;
  api_ = Api::createApiForTest();
  dispatcher_ = api_->allocateDispatcher("test_thread");

  auto watched_dir = std::make_unique<::envoy::config::core::v3::WatchedDirectory>();

  certificate_data_source.set_allocated_watched_directory(watched_dir.release());
  NiceMock<ThreadLocal::MockInstance> tls;

  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      *api_, tls, *dispatcher_, certificate_data_source, private_key_data_source,
      cert_chain_data_source);
  EXPECT_FALSE(provider->getCredentials().certificateChainDerB64().has_value());
}

TEST_F(IAMRolesAnywhereX509CredentialsProviderTest, InvalidPath) {
  envoy::config::core::v3::DataSource certificate_data_source, private_key_data_source,
      cert_chain_data_source;
  api_ = Api::createApiForTest();
  dispatcher_ = api_->allocateDispatcher("test_thread");
  auto path = TestEnvironment::temporaryPath("testpath/path");
  TestEnvironment::removePath(path);
  certificate_data_source.set_filename(path);
  NiceMock<ThreadLocal::MockInstance> tls;

  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      *api_, tls, *dispatcher_, certificate_data_source, private_key_data_source,
      cert_chain_data_source);
  EXPECT_FALSE(provider->getCredentials().certificateChainDerB64().has_value());
}

TEST_F(IAMRolesAnywhereX509CredentialsProviderTest, PrivateKeyInvalidPath) {
  envoy::config::core::v3::DataSource certificate_data_source, private_key_data_source,
      cert_chain_data_source;
  api_ = Api::createApiForTest();
  dispatcher_ = api_->allocateDispatcher("test_thread");

  auto filename_cert =
      TestEnvironment::writeStringToFileForTest("cert", server_subordinate_cert_ecdsa_pem);
  certificate_data_source.set_filename(filename_cert);
  auto filename_chain =
      TestEnvironment::writeStringToFileForTest("chain", server_subordinate_chain_ecdsa_pem);
  cert_chain_data_source.set_filename(filename_chain);

  auto path = TestEnvironment::temporaryPath("testpath/path");
  TestEnvironment::removePath(path);
  private_key_data_source.set_filename(path);
  NiceMock<ThreadLocal::MockInstance> tls;

  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      *api_, tls, *dispatcher_, certificate_data_source, private_key_data_source,
      cert_chain_data_source);
  EXPECT_FALSE(provider->getCredentials().certificatePrivateKey().has_value());
}

std::string invalid_cert_algorithm = R"EOF(
-----BEGIN CERTIFICATE-----
MIICljCCAX6gAwIBAgIQMSOm4y8jiRORwltpCv9FSjANBgkqhkiG9w0BAQsFADAT
MREwDwYDVQQDDAh0ZXN0LXJzYTAeFw0yNDExMjQyMDQ4MTFaFw0yNTExMjQyMTQ4
MTBaMF4xCzAJBgNVBAYTAlhYMRUwEwYDVQQHDAxEZWZhdWx0IENpdHkxHDAaBgNV
BAoME0RlZmF1bHQgQ29tcGFueSBMdGQxGjAYBgNVBAMMEWludmFsaWQtYWxnb3Jp
dGhtMEMwBQYDK2VxAzoA0Zupaq2Q17RSYHO+5yjiTV/eMJzuwuhQ98Yf0KOzu7hs
iNUWBOBDXgB2V90Aih6rUk3bnLSn9mQAo3wwejAJBgNVHRMEAjAAMB8GA1UdIwQY
MBaAFEHiZf9gMo1f2eygpdP67LDepsLPMB0GA1UdDgQWBBTHZH9P3IfeWWjxpfvl
orlE9gVY2TAOBgNVHQ8BAf8EBAMCBaAwHQYDVR0lBBYwFAYIKwYBBQUHAwEGCCsG
AQUFBwMCMA0GCSqGSIb3DQEBCwUAA4IBAQBxecaA/BBFe/C7nvdaArmiRJ+ZeY3n
54jk4IYXM5Y+Vmh36FW4Tx9NzysKGIa9uFaDYs54yA7NGA5WcjVShrvGSMTfw1cJ
MKvtVIQIUuxuMC6B61QYCRJwhmk+XfqXA/FGx+FBGM3wpyOfIx6xDd231bVjp04W
UbuENfg/9PoNDw0swZgpjOnFIT7OFutnrThMVsiQvDDs1OYL7bW6gx2IWaS78vni
QzbhCLiLBzgNs580uNqX2/wttv7yjxEWPsEw3mGMw3h95uuRi1b9wsN1EE33483E
kE6ZHW3vIyCWgmgzpyZUUxdJTxfnD5WudjQWSv+3sjvFxAuXtyaG6ukX
-----END CERTIFICATE-----
)EOF";

TEST_F(IAMRolesAnywhereX509CredentialsProviderTest, UnsupportedAlgorithm) {
  envoy::config::core::v3::DataSource certificate_data_source, private_key_data_source,
      cert_chain_data_source;
  api_ = Api::createApiForTest();
  dispatcher_ = api_->allocateDispatcher("test_thread");
  // Filesystem - Certs issued from a subordinate to test certificate chain. Verify that we read and
  // convert these correctly.
  auto filename_cert = TestEnvironment::writeStringToFileForTest("cert", invalid_cert_algorithm);
  certificate_data_source.set_filename(filename_cert);
  auto filename_pkey =
      TestEnvironment::writeStringToFileForTest("pkey", server_subordinate_private_key_ecdsa_pem);
  private_key_data_source.set_filename(filename_pkey);
  auto filename_chain =
      TestEnvironment::writeStringToFileForTest("chain", server_subordinate_chain_ecdsa_pem);
  cert_chain_data_source.set_filename(filename_chain);
  NiceMock<ThreadLocal::MockInstance> tls;

  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      *api_, tls, *dispatcher_, certificate_data_source, private_key_data_source,
      cert_chain_data_source);
  auto credentials = provider->getCredentials();

  EXPECT_FALSE(credentials.certificateDerB64().has_value());
}

std::string missing_serial = R"EOF(
-----BEGIN CERTIFICATE-----
MIIChDCCAWygAwIBAjANBgkqhkiG9w0BAQsFADATMREwDwYDVQQDDAh0ZXN0LXJzYTAeFw0yNDExMjQyMDQ4MTFaFw0yNTExMjQyMTQ4MTBaMF4xCzAJBgNVBAYTAlhYMRUwEwYDVQQHDAxEZWZhdWx0IENpdHkxHDAaBgNVBAoME0RlZmF1bHQgQ29tcGFueSBMdGQxGjAYBgNVBAMMEWludmFsaWQtYWxnb3JpdGhtMEMwBQYDK2VxAzoA0Zupaq2Q17RSYHO+5yjiTV/eMJzuwuhQ98Yf0KOzu7hsiNUWBOBDXgB2V90Aih6rUk3bnLSn9mQAo3wwejAJBgNVHRMEAjAAMB8GA1UdIwQYMBaAFEHiZf9gMo1f2eygpdP67LDepsLPMB0GA1UdDgQWBBTHZH9P3IfeWWjxpfvlorlE9gVY2TAOBgNVHQ8BAf8EBAMCBaAwHQYDVR0lBBYwFAYIKwYBBQUHAwEGCCsGAQUFBwMCMA0GCSqGSIb3DQEBCwUAA4IBAQBxecaA/BBFe/C7nvdaArmiRJ+ZeY3n54jk4IYXM5Y+Vmh36FW4Tx9NzysKGIa9uFaDYs54yA7NGA5WcjVShrvGSMTfw1cJMKvtVIQIUuxuMC6B61QYCRJwhmk+XfqXA/FGx+FBGM3wpyOfIx6xDd231bVjp04WUbuENfg/9PoNDw0swZgpjOnFIT7OFutnrThMVsiQvDDs1OYL7bW6gx2IWaS78vniQzbhCLiLBzgNs580uNqX2/wttv7yjxEWPsEw3mGMw3h95uuRi1b9wsN1EE33483EkE6ZHW3vIyCWgmgzpyZUUxdJTxfnD5WudjQWSv+3sjvFxAuXtyaG6ukX
-----END CERTIFICATE-----
)EOF";

TEST_F(IAMRolesAnywhereX509CredentialsProviderTest, MissingSerial) {
  envoy::config::core::v3::DataSource certificate_data_source, private_key_data_source,
      cert_chain_data_source;
  api_ = Api::createApiForTest();
  dispatcher_ = api_->allocateDispatcher("test_thread");
  // Filesystem - Certs issued from a subordinate to test certificate chain. Verify that we read and
  // convert these correctly.
  auto filename_cert = TestEnvironment::writeStringToFileForTest("cert", invalid_cert_algorithm);
  certificate_data_source.set_filename(filename_cert);
  auto filename_pkey =
      TestEnvironment::writeStringToFileForTest("pkey", server_subordinate_private_key_ecdsa_pem);
  private_key_data_source.set_filename(filename_pkey);
  auto filename_chain =
      TestEnvironment::writeStringToFileForTest("chain", server_subordinate_chain_ecdsa_pem);
  cert_chain_data_source.set_filename(filename_chain);
  NiceMock<ThreadLocal::MockInstance> tls;

  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      *api_, tls, *dispatcher_, certificate_data_source, private_key_data_source,
      cert_chain_data_source);
  auto credentials = provider->getCredentials();

  EXPECT_FALSE(credentials.certificateDerB64().has_value());
}

TEST_F(IAMRolesAnywhereX509CredentialsProviderTest, LoadCredentials) {
  envoy::config::core::v3::DataSource certificate_data_source, private_key_data_source,
      cert_chain_data_source;
  Envoy::Logger::Registry::setLogLevel(spdlog::level::debug);
  api_ = Api::createApiForTest();
  dispatcher_ = setupDispatcher();

  // Environment Variables - Normal certificate issued from a CA. Verify that we read and convert
  // these correctly.

  auto cert_env = std::string("CERT");
  TestEnvironment::setEnvVar(cert_env, server_root_cert_ecdsa_pem, 1);
  auto yaml = fmt::format(R"EOF(
    environment_variable: "{}"
  )EOF",
                          cert_env);
  TestUtility::loadFromYamlAndValidate(yaml, certificate_data_source);

  auto pkey_env = std::string("PKEY");
  TestEnvironment::setEnvVar(pkey_env, server_root_private_key_ecdsa_pem, 1);
  yaml = fmt::format(R"EOF(
    environment_variable: "{}"
  )EOF",
                     pkey_env);

  TestUtility::loadFromYamlAndValidate(yaml, private_key_data_source);

  NiceMock<ThreadLocal::MockInstance> tls;

  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      *api_, tls, *dispatcher_, certificate_data_source, private_key_data_source, absl::nullopt);
  auto credentials = provider->getCredentials();

  EXPECT_TRUE(credentials.certificateDerB64().has_value());
  EXPECT_EQ(credentials.certificateDerB64().value(), server_root_cert_ecdsa_der_b64);
  EXPECT_TRUE(credentials.publicKeySignatureAlgorithm().has_value());
  EXPECT_EQ(credentials.publicKeySignatureAlgorithm(),
            X509Credentials::PublicKeySignatureAlgorithm::ECDSA);
  EXPECT_TRUE(credentials.certificateSerial().has_value());
  EXPECT_EQ(credentials.certificateSerial(), "5215639076998761095638506031589467414");
  EXPECT_TRUE(credentials.certificatePrivateKey().has_value());
  EXPECT_EQ(credentials.certificatePrivateKey(), server_root_private_key_ecdsa_pem);

  // Environment Variables -  Certs issued from a subordinate to test certificate chain. Verify that
  // we read and convert these correctly.

  cert_env = std::string("CERT");
  TestEnvironment::setEnvVar(cert_env, server_subordinate_cert_ecdsa_pem, 1);
  yaml = fmt::format(R"EOF(
    environment_variable: "{}"
  )EOF",
                     cert_env);
  TestUtility::loadFromYamlAndValidate(yaml, certificate_data_source);

  pkey_env = std::string("PKEY");
  TestEnvironment::setEnvVar(pkey_env, server_subordinate_private_key_ecdsa_pem, 1);
  yaml = fmt::format(R"EOF(
    environment_variable: "{}"
  )EOF",
                     pkey_env);

  TestUtility::loadFromYamlAndValidate(yaml, private_key_data_source);

  auto chain_env = std::string("CHAIN");
  TestEnvironment::setEnvVar(chain_env, server_subordinate_chain_ecdsa_pem, 1);
  yaml = fmt::format(R"EOF(
    environment_variable: "{}"
  )EOF",
                     chain_env);

  TestUtility::loadFromYamlAndValidate(yaml, cert_chain_data_source);

  provider.reset();

  provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      *api_, tls, *dispatcher_, certificate_data_source, private_key_data_source,
      cert_chain_data_source);
  credentials = provider->getCredentials();

  EXPECT_TRUE(credentials.certificateDerB64().has_value());
  EXPECT_EQ(credentials.certificateDerB64().value(), server_subordinate_cert_ecdsa_der_b64);
  EXPECT_TRUE(credentials.publicKeySignatureAlgorithm().has_value());
  EXPECT_EQ(credentials.publicKeySignatureAlgorithm(),
            X509Credentials::PublicKeySignatureAlgorithm::ECDSA);
  EXPECT_TRUE(credentials.certificateSerial().has_value());
  EXPECT_EQ(credentials.certificateSerial(), "326164854566604267642234107702204417305661869882");
  EXPECT_TRUE(credentials.certificatePrivateKey().has_value());
  EXPECT_EQ(credentials.certificatePrivateKey(), server_subordinate_private_key_ecdsa_pem);
  EXPECT_TRUE(credentials.certificateChainDerB64().has_value());
  EXPECT_EQ(credentials.certificateChainDerB64(), server_subordinate_chain_ecdsa_der_b64);

  // Filesystem - Certs issued from a subordinate to test certificate chain. Verify that we read and
  // convert these correctly.

  auto filename =
      TestEnvironment::writeStringToFileForTest("cert", server_subordinate_cert_ecdsa_pem);

  TestEnvironment::setEnvVar(cert_env, server_root_cert_ecdsa_pem, 1);
  yaml = fmt::format(R"EOF(
    filename: "{}"
  )EOF",
                     filename);
  TestUtility::loadFromYamlAndValidate(yaml, certificate_data_source);
  filename =
      TestEnvironment::writeStringToFileForTest("pkey", server_subordinate_private_key_ecdsa_pem);

  yaml = fmt::format(R"EOF(
    filename: "{}"
  )EOF",
                     filename);

  TestUtility::loadFromYamlAndValidate(yaml, private_key_data_source);
  filename = TestEnvironment::writeStringToFileForTest("chain", server_subordinate_chain_ecdsa_pem);

  yaml = fmt::format(R"EOF(
    filename: "{}"
  )EOF",
                     filename);

  TestUtility::loadFromYamlAndValidate(yaml, cert_chain_data_source);

  provider.reset();

  provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      *api_, tls, *dispatcher_, certificate_data_source, private_key_data_source,
      cert_chain_data_source);
  credentials = provider->getCredentials();

  EXPECT_TRUE(credentials.certificateDerB64().has_value());
  EXPECT_EQ(credentials.certificateDerB64().value(), server_subordinate_cert_ecdsa_der_b64);
  EXPECT_TRUE(credentials.publicKeySignatureAlgorithm().has_value());
  EXPECT_EQ(credentials.publicKeySignatureAlgorithm(),
            X509Credentials::PublicKeySignatureAlgorithm::ECDSA);
  EXPECT_TRUE(credentials.certificateSerial().has_value());
  EXPECT_EQ(credentials.certificateSerial(), "326164854566604267642234107702204417305661869882");
  EXPECT_TRUE(credentials.certificatePrivateKey().has_value());
  EXPECT_EQ(credentials.certificatePrivateKey(), server_subordinate_private_key_ecdsa_pem);
  EXPECT_TRUE(credentials.certificateChainDerB64().has_value());
  EXPECT_EQ(credentials.certificateChainDerB64(), server_subordinate_chain_ecdsa_der_b64);

  // Inline - Certs issued from a subordinate to test certificate chain. Verify that we read and
  // convert these correctly.

  TestEnvironment::setEnvVar(cert_env, server_subordinate_cert_ecdsa_pem, 1);
  yaml = fmt::format(R"EOF(
    inline_bytes: "{}"
  )EOF",
                     Base64::encode(server_subordinate_cert_ecdsa_pem.c_str(),
                                    server_subordinate_cert_ecdsa_pem.size()));
  TestUtility::loadFromYamlAndValidate(yaml, certificate_data_source);

  yaml = fmt::format(R"EOF(
    inline_bytes: "{}"
  )EOF",
                     Base64::encode(server_subordinate_private_key_ecdsa_pem.c_str(),
                                    server_subordinate_private_key_ecdsa_pem.size()));

  TestUtility::loadFromYamlAndValidate(yaml, private_key_data_source);

  yaml = fmt::format(R"EOF(
    inline_bytes: "{}"
  )EOF",
                     Base64::encode(server_subordinate_chain_ecdsa_pem.c_str(),
                                    server_subordinate_chain_ecdsa_pem.size()));

  TestUtility::loadFromYamlAndValidate(yaml, cert_chain_data_source);

  provider.reset();

  provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      *api_, tls, *dispatcher_, certificate_data_source, private_key_data_source,
      cert_chain_data_source);
  credentials = provider->getCredentials();

  EXPECT_TRUE(credentials.certificateDerB64().has_value());
  EXPECT_EQ(credentials.certificateDerB64().value(), server_subordinate_cert_ecdsa_der_b64);
  EXPECT_TRUE(credentials.publicKeySignatureAlgorithm().has_value());
  EXPECT_EQ(credentials.publicKeySignatureAlgorithm(),
            X509Credentials::PublicKeySignatureAlgorithm::ECDSA);
  EXPECT_TRUE(credentials.certificateSerial().has_value());
  EXPECT_EQ(credentials.certificateSerial(), "326164854566604267642234107702204417305661869882");
  EXPECT_TRUE(credentials.certificatePrivateKey().has_value());
  EXPECT_EQ(credentials.certificatePrivateKey(), server_subordinate_private_key_ecdsa_pem);
  EXPECT_TRUE(credentials.certificateChainDerB64().has_value());
  EXPECT_EQ(credentials.certificateChainDerB64(), server_subordinate_chain_ecdsa_der_b64);

  // Environment Variables - Normal RSA signed certificate issued from a CA with single cert chain.
  // Verify that we read and convert these correctly.

  cert_env = std::string("CERT");
  TestEnvironment::setEnvVar(cert_env, server_root_cert_rsa_pem, 1);
  yaml = fmt::format(R"EOF(
    environment_variable: {}
  )EOF",
                     cert_env);
  TestUtility::loadFromYamlAndValidate(yaml, certificate_data_source);

  pkey_env = std::string("PKEY");
  TestEnvironment::setEnvVar(pkey_env, server_root_private_key_rsa_pem, 1);
  yaml = fmt::format(R"EOF(
    environment_variable: "{}"
  )EOF",
                     pkey_env);

  TestUtility::loadFromYamlAndValidate(yaml, private_key_data_source);

  chain_env = std::string("CHAIN");
  TestEnvironment::setEnvVar(chain_env, server_root_chain_rsa_pem, 1);
  yaml = fmt::format(R"EOF(
    environment_variable: "{}"
  )EOF",
                     chain_env);

  TestUtility::loadFromYamlAndValidate(yaml, cert_chain_data_source);

  provider.reset();

  provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      *api_, tls, *dispatcher_, certificate_data_source, private_key_data_source,
      cert_chain_data_source);
  credentials = provider->getCredentials();

  EXPECT_TRUE(credentials.certificateDerB64().has_value());
  EXPECT_EQ(credentials.certificateDerB64().value(), server_root_cert_rsa_der_b64);
  EXPECT_TRUE(credentials.publicKeySignatureAlgorithm().has_value());
  EXPECT_EQ(credentials.publicKeySignatureAlgorithm(),
            X509Credentials::PublicKeySignatureAlgorithm::RSA);
  EXPECT_TRUE(credentials.certificateSerial().has_value());
  EXPECT_EQ(credentials.certificateSerial(), "131827979019394590882466519576505238184");
  EXPECT_TRUE(credentials.certificatePrivateKey().has_value());
  EXPECT_EQ(credentials.certificatePrivateKey(), server_root_private_key_rsa_pem);
  EXPECT_TRUE(credentials.certificateChainDerB64().has_value());
  EXPECT_EQ(credentials.certificateChainDerB64(), server_root_chain_rsa_der_b64);
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
