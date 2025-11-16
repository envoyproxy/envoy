#include <chrono>
#include <cstddef>
#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/base.pb.validate.h"

#include "source/common/common/base64.h"
#include "source/extensions/common/aws/credential_providers/iam_roles_anywhere_x509_credentials_provider.h"

#include "test/extensions/common/aws/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/environment.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InvokeWithoutArgs;
using testing::Return;
using testing::StartsWith;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

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
//             Not Before: Nov 12 10:16:07 2025 GMT
//             Not After : Sep 25 10:16:07 2225 GMT
//         Subject: C = XX, L = Default City, O = Default Company Ltd, CN = test-ecdsa.test
//         Subject Public Key Info:
//                 Public-Key: (256 bit)
//                 pub:
//                     04:39:bd:3c:e8:92:d4:8f:10:d1:c6:f3:49:d0:02:
//                     f1:62:77:1f:df:4c:d6:40:8c:0c:ae:47:3c:14:c2:
//                     b0:bb:77:04:39:9e:da:45:e2:14:81:93:77:1d:68:
//                     42:f7:77:39:ea:e5:19:9c:cb:a6:21:07:a3:f2:74:
//                     5c:88:74:eb:74
//                 ASN1 OID: prime256v1
//                 NIST CURVE: P-256
//         X509v3 extensions:
//             X509v3 Basic Constraints:
//                 CA:FALSE
//
//             X509v3 Subject Key Identifier:
//                 7E:13:23:B2:E5:45:55:18:E9:A2:9A:2D:60:88:E8:A0:4E:0D:B7:3F
//             X509v3 Key Usage: critical
//             X509v3 Extended Key Usage:
//                 TLS Web Server Authentication, TLS Web Client Authentication
//     Signature Algorithm: ecdsa-with-SHA256
//          30:46:02:21:00:bd:50:46:03:a6:c0:55:22:8a:c5:8c:93:16:
//          a8:b0:35:f2:ff:66:08:8b:56:c8:35:71:ef:c3:d3:86:c9:15:
//          9c:02:21:00:9f:73:1c:77:04:00:8a:aa:ef:d7:ca:05:ef:c6:
//          34:14:68:8d:9c:6c:08:33:d5:66:a8:6e:d2:8b:b7:52:69:77
//

std::string server_root_cert_ecdsa_der_b64 = R"EOF(
MIIB8zCCAZigAwIBAgIQA+x+3DenV9P2mjCafZGlFjAKBggqhkjOPQQDAjAaMRgwFgYDVQQDDA90
ZXN0LWVjZHNhLXAyNTYwIBcNMjUxMTEyMTAxNjA3WhgPMjIyNTA5MjUxMDE2MDdaMFwxCzAJBgNV
BAYTAlhYMRUwEwYDVQQHDAxEZWZhdWx0IENpdHkxHDAaBgNVBAoME0RlZmF1bHQgQ29tcGFueSBM
dGQxGDAWBgNVBAMMD3Rlc3QtZWNkc2EudGVzdDBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABDm9
POiS1I8Q0cbzSdAC8WJ3H99M1kCMDK5HPBTCsLt3BDme2kXiFIGTdx1oQvd3OerlGZzLpiEHo/J0
XIh063SjfDB6MAkGA1UdEwQCMAAwHwYDVR0jBBgwFoAU8AfpSBXUJ8q9PVK5YzQ3QAI0bSwwHQYD
VR0OBBYEFH4TI7LlRVUY6aKaLWCI6KBODbc/MA4GA1UdDwEB/wQEAwIFoDAdBgNVHSUEFjAUBggr
BgEFBQcDAQYIKwYBBQUHAwIwCgYIKoZIzj0EAwIDSQAwRgIhAL1QRgOmwFUiisWMkxaosDXy/2YI
i1bINXHvw9OGyRWcAiEAn3McdwQAiqrv18oF78Y0FGiNnGwIM9VmqG7Si7dSaXc=
)EOF";

std::string server_root_cert_ecdsa_pem = R"EOF(
-----BEGIN CERTIFICATE-----
MIIB8zCCAZigAwIBAgIQA+x+3DenV9P2mjCafZGlFjAKBggqhkjOPQQDAjAaMRgw
FgYDVQQDDA90ZXN0LWVjZHNhLXAyNTYwIBcNMjUxMTEyMTAxNjA3WhgPMjIyNTA5
MjUxMDE2MDdaMFwxCzAJBgNVBAYTAlhYMRUwEwYDVQQHDAxEZWZhdWx0IENpdHkx
HDAaBgNVBAoME0RlZmF1bHQgQ29tcGFueSBMdGQxGDAWBgNVBAMMD3Rlc3QtZWNk
c2EudGVzdDBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABDm9POiS1I8Q0cbzSdAC
8WJ3H99M1kCMDK5HPBTCsLt3BDme2kXiFIGTdx1oQvd3OerlGZzLpiEHo/J0XIh0
63SjfDB6MAkGA1UdEwQCMAAwHwYDVR0jBBgwFoAU8AfpSBXUJ8q9PVK5YzQ3QAI0
bSwwHQYDVR0OBBYEFH4TI7LlRVUY6aKaLWCI6KBODbc/MA4GA1UdDwEB/wQEAwIF
oDAdBgNVHSUEFjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwCgYIKoZIzj0EAwIDSQAw
RgIhAL1QRgOmwFUiisWMkxaosDXy/2YIi1bINXHvw9OGyRWcAiEAn3McdwQAiqrv
18oF78Y0FGiNnGwIM9VmqG7Si7dSaXc=
-----END CERTIFICATE-----
)EOF";

std::string server_root_private_key_ecdsa_pem = R"EOF(
-----BEGIN EC PARAMETERS-----
BggqhkjOPQMBBw==
-----END EC PARAMETERS-----
-----BEGIN EC PRIVATE KEY-----
MHcCAQEEILjtTl7LGvCXeWPPx3Hq6A6EQQF6VuXNTk01TWbVfnfQoAoGCCqGSM49
AwEHoUQDQgAEOb086JLUjxDRxvNJ0ALxYncf30zWQIwMrkc8FMKwu3cEOZ7aReIU
gZN3HWhC93c56uUZnMumIQej8nRciHTrdA==
-----END EC PRIVATE KEY-----
)EOF";

// Test ECDSA signed certificate - Issued from Subordinate CA
// Certificate:
//     Data:
//         Version: 3 (0x2)
//         Serial Number:
//             cd:2e:95:2e:32:ba:16:98:13:04:67:f7:47:c4:77:5f:fb:c4:cb:89
//         Signature Algorithm: ecdsa-with-SHA256
//         Issuer: CN = test-ecdsa-p256
//         Validity
//             Not Before: Nov 12 12:08:33 2025 GMT
//             Not After : Sep 25 12:08:33 2225 GMT
//         Subject: C = XX, L = Default City, O = Default Company Ltd, CN =
//         test-ecdsa-p256-subordinate Subject Public Key Info:
//                 Public-Key: (256 bit)
//                 pub:
//                     04:c7:d0:66:fc:ce:dc:5e:11:30:ea:f9:7d:74:11:
//                     b3:35:17:65:c6:e2:19:23:7f:d5:0c:43:92:e9:32:
//                     fd:06:62:8b:64:cf:94:9e:c0:44:f7:c0:fa:9b:93:
//                     ef:89:c3:3c:d9:8e:b9:17:d8:7e:1c:59:d3:a7:fa:
//                     c9:9f:e2:87:92
//                 ASN1 OID: prime256v1
//                 NIST CURVE: P-256
//         X509v3 extensions:
//             X509v3 Basic Constraints: critical
//                 CA:TRUE
//             X509v3 Subject Key Identifier:
//                 2B:BC:CF:4E:63:DA:9F:12:54:04:FB:4A:B8:58:0B:A8:89:34:77:97
//
//     Signature Algorithm: ecdsa-with-SHA256
//          30:44:02:20:21:24:51:54:36:42:1e:84:d2:77:e1:6f:b0:63:
//          a1:48:92:f0:58:40:71:5d:20:4f:32:1a:59:8d:47:23:55:17:
//          02:20:43:68:d7:0f:23:34:4b:76:ca:52:11:5d:25:9b:91:e7:
//          ef:c9:6c:69:1f:10:16:48:f3:17:e2:5f:93:fc:c2:28

std::string server_subordinate_cert_ecdsa_pem = R"EOF(
-----BEGIN CERTIFICATE-----
MIIB6jCCAZCgAwIBAgIVAM0ulS4yuhaYEwRn90fEd1/7xMuJMAoGCCqGSM49BAMC
MBoxGDAWBgNVBAMMD3Rlc3QtZWNkc2EtcDI1NjAgFw0yNTExMTIxMjA4MzNaGA8y
MjI1MDkyNTEyMDgzM1owaDELMAkGA1UEBhMCWFgxFTATBgNVBAcMDERlZmF1bHQg
Q2l0eTEcMBoGA1UECgwTRGVmYXVsdCBDb21wYW55IEx0ZDEkMCIGA1UEAwwbdGVz
dC1lY2RzYS1wMjU2LXN1Ym9yZGluYXRlMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcD
QgAEzweun9csA6k1Q2ubBCClYv/SMZa1CtHsm7EThXpyVwh2SCJU6W5xLHzKTHtv
WiU8GN7TjXs+0GqvkjwE4TE+z6NjMGEwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4E
FgQUdXdc+5AklLHtG9IPaarYcIzam68wHwYDVR0jBBgwFoAUXiDvhes3PXlzAq0W
E4rzxe8oj98wDgYDVR0PAQH/BAQDAgGGMAoGCCqGSM49BAMCA0gAMEUCIQCYi3jI
lVB+4u6eC7i3210Zy6Z+Ne5oPRbw+1QxakP8vwIgQmmfXe2BaQbqKuqY/YzVk1Ao
I0RukOw+Rnl0jhwN1rE=
-----END CERTIFICATE-----
)EOF";

std::string server_subordinate_cert_ecdsa_der_b64 = R"EOF(
MIIB6jCCAZCgAwIBAgIVAM0ulS4yuhaYEwRn90fEd1/7xMuJMAoGCCqGSM49BAMCMBoxGDAWBgNV
BAMMD3Rlc3QtZWNkc2EtcDI1NjAgFw0yNTExMTIxMjA4MzNaGA8yMjI1MDkyNTEyMDgzM1owaDEL
MAkGA1UEBhMCWFgxFTATBgNVBAcMDERlZmF1bHQgQ2l0eTEcMBoGA1UECgwTRGVmYXVsdCBDb21w
YW55IEx0ZDEkMCIGA1UEAwwbdGVzdC1lY2RzYS1wMjU2LXN1Ym9yZGluYXRlMFkwEwYHKoZIzj0C
AQYIKoZIzj0DAQcDQgAEzweun9csA6k1Q2ubBCClYv/SMZa1CtHsm7EThXpyVwh2SCJU6W5xLHzK
THtvWiU8GN7TjXs+0GqvkjwE4TE+z6NjMGEwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUdXdc
+5AklLHtG9IPaarYcIzam68wHwYDVR0jBBgwFoAUXiDvhes3PXlzAq0WE4rzxe8oj98wDgYDVR0P
AQH/BAQDAgGGMAoGCCqGSM49BAMCA0gAMEUCIQCYi3jIlVB+4u6eC7i3210Zy6Z+Ne5oPRbw+1Qx
akP8vwIgQmmfXe2BaQbqKuqY/YzVk1AoI0RukOw+Rnl0jhwN1rE=
)EOF";

std::string server_subordinate_private_key_ecdsa_pem = R"EOF(
-----BEGIN EC PARAMETERS-----
BggqhkjOPQMBBw==
-----END EC PARAMETERS-----
-----BEGIN EC PRIVATE KEY-----
MHcCAQEEIIySfOoXNx9Til/OcIJnRASZUfUMvB0uYWX3uOM0Ais3oAoGCCqGSM49
AwEHoUQDQgAE+zlA/fa8wC/M4i4OtUXIHfEKoCsRjXRvzJTKJ6BaIcjxFWMELMKy
k0gX4ixwIwZmi6oE32TEaDTgY5UnxrAfYw==
-----END EC PRIVATE KEY-----
)EOF";

std::string server_subordinate_chain_ecdsa_pem = R"EOF(
-----BEGIN CERTIFICATE-----
MIIB6jCCAZCgAwIBAgIVAM0ulS4yuhaYEwRn90fEd1/7xMuJMAoGCCqGSM49BAMC
MBoxGDAWBgNVBAMMD3Rlc3QtZWNkc2EtcDI1NjAgFw0yNTExMTIxMjA4MzNaGA8y
MjI1MDkyNTEyMDgzM1owaDELMAkGA1UEBhMCWFgxFTATBgNVBAcMDERlZmF1bHQg
Q2l0eTEcMBoGA1UECgwTRGVmYXVsdCBDb21wYW55IEx0ZDEkMCIGA1UEAwwbdGVz
dC1lY2RzYS1wMjU2LXN1Ym9yZGluYXRlMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcD
QgAEzweun9csA6k1Q2ubBCClYv/SMZa1CtHsm7EThXpyVwh2SCJU6W5xLHzKTHtv
WiU8GN7TjXs+0GqvkjwE4TE+z6NjMGEwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4E
FgQUdXdc+5AklLHtG9IPaarYcIzam68wHwYDVR0jBBgwFoAUXiDvhes3PXlzAq0W
E4rzxe8oj98wDgYDVR0PAQH/BAQDAgGGMAoGCCqGSM49BAMCA0gAMEUCIQCYi3jI
lVB+4u6eC7i3210Zy6Z+Ne5oPRbw+1QxakP8vwIgQmmfXe2BaQbqKuqY/YzVk1Ao
I0RukOw+Rnl0jhwN1rE=
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIBiDCCAS6gAwIBAgIRANU2fOxPEClRBUgtykrPZ24wCgYIKoZIzj0EAwIwGjEY
MBYGA1UEAwwPdGVzdC1lY2RzYS1wMjU2MCAXDTI1MTExMjEyMDgzM1oYDzIyMjUw
OTI1MTIwODMzWjAaMRgwFgYDVQQDDA90ZXN0LWVjZHNhLXAyNTYwWTATBgcqhkjO
PQIBBggqhkjOPQMBBwNCAATsqxwEMkg6iwd3vocJd77apHpPx/HBWbySQ+NBBK0M
ExvnawlnaWQVafCTJXNa3XenHWnkGY1IlPtqJ9vggo6Ao1MwUTAdBgNVHQ4EFgQU
XiDvhes3PXlzAq0WE4rzxe8oj98wHwYDVR0jBBgwFoAUXiDvhes3PXlzAq0WE4rz
xe8oj98wDwYDVR0TAQH/BAUwAwEB/zAKBggqhkjOPQQDAgNIADBFAiB/JDje83oo
4l2iXb8CtRo2KYKNDqTIvDkiYPjFHOA37gIhAKDtkHAKxFmt02XXybfK8KVYLa/R
o7Kf455sNAyGOBTs
-----END CERTIFICATE-----
)EOF";

std::string server_subordinate_chain_ecdsa_der_b64 = R"EOF(
MIIB6jCCAZCgAwIBAgIVAM0ulS4yuhaYEwRn90fEd1/7xMuJMAoGCCqGSM49BAMCMBoxGDAWBgNV
BAMMD3Rlc3QtZWNkc2EtcDI1NjAgFw0yNTExMTIxMjA4MzNaGA8yMjI1MDkyNTEyMDgzM1owaDEL
MAkGA1UEBhMCWFgxFTATBgNVBAcMDERlZmF1bHQgQ2l0eTEcMBoGA1UECgwTRGVmYXVsdCBDb21w
YW55IEx0ZDEkMCIGA1UEAwwbdGVzdC1lY2RzYS1wMjU2LXN1Ym9yZGluYXRlMFkwEwYHKoZIzj0C
AQYIKoZIzj0DAQcDQgAEzweun9csA6k1Q2ubBCClYv/SMZa1CtHsm7EThXpyVwh2SCJU6W5xLHzK
THtvWiU8GN7TjXs+0GqvkjwE4TE+z6NjMGEwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUdXdc
+5AklLHtG9IPaarYcIzam68wHwYDVR0jBBgwFoAUXiDvhes3PXlzAq0WE4rzxe8oj98wDgYDVR0P
AQH/BAQDAgGGMAoGCCqGSM49BAMCA0gAMEUCIQCYi3jIlVB+4u6eC7i3210Zy6Z+Ne5oPRbw+1Qx
akP8vwIgQmmfXe2BaQbqKuqY/YzVk1AoI0RukOw+Rnl0jhwN1rE=,MIIBiDCCAS6gAwIBAgIRANU2
fOxPEClRBUgtykrPZ24wCgYIKoZIzj0EAwIwGjEYMBYGA1UEAwwPdGVzdC1lY2RzYS1wMjU2MCAX
DTI1MTExMjEyMDgzM1oYDzIyMjUwOTI1MTIwODMzWjAaMRgwFgYDVQQDDA90ZXN0LWVjZHNhLXAy
NTYwWTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAATsqxwEMkg6iwd3vocJd77apHpPx/HBWbySQ+NB
BK0MExvnawlnaWQVafCTJXNa3XenHWnkGY1IlPtqJ9vggo6Ao1MwUTAdBgNVHQ4EFgQUXiDvhes3
PXlzAq0WE4rzxe8oj98wHwYDVR0jBBgwFoAUXiDvhes3PXlzAq0WE4rzxe8oj98wDwYDVR0TAQH/
BAUwAwEB/zAKBggqhkjOPQQDAgNIADBFAiB/JDje83oo4l2iXb8CtRo2KYKNDqTIvDkiYPjFHOA3
7gIhAKDtkHAKxFmt02XXybfK8KVYLa/Ro7Kf455sNAyGOBTs
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
//             Not Before: Nov 12 10:16:07 2025 GMT
//             Not After : Sep 25 10:16:07 2225 GMT
//         Subject: C = XX, L = Default City, O = Default Company Ltd, CN = test-rsa.test
//         Subject Public Key Info:
//             Public Key Algorithm: rsaEncryption
//                 RSA Public-Key: (2048 bit)
//                 Modulus:
//                     00:ca:17:c7:cf:02:1e:d3:ed:90:0a:06:89:a2:d4:
//                     78:0f:07:41:98:ad:0b:bb:fb:89:1b:60:cd:2e:8e:
//                     40:c9:51:8c:7c:f1:99:3c:24:f8:de:df:af:f7:8c:
//                     34:90:74:17:28:10:14:85:f9:56:4f:1a:1d:a8:a1:
//                     e7:df:35:e6:1b:c8:68:f0:41:ae:a3:c3:99:02:62:
//                     1b:52:6f:97:86:79:62:4e:1e:60:e3:b7:ea:fd:43:
//                     21:38:10:a8:a5:af:8b:6e:7b:b4:b9:bb:6e:f7:34:
//                     d9:74:a0:34:6a:73:48:56:58:92:8c:90:32:b4:9e:
//                     e1:eb:b3:38:84:be:dd:3c:aa:9d:4b:9a:cf:5c:fd:
//                     c4:b8:0b:8a:d1:80:63:f0:b0:0d:d7:d9:ea:77:45:
//                     d8:de:2a:f6:0f:8d:e9:3a:fa:ec:9c:ae:8c:c0:05:
//                     d3:ab:ea:68:87:1f:07:1c:fa:f9:87:03:86:f1:a0:
//                     83:3c:d6:e7:c8:52:e2:9f:73:f3:c7:14:73:ac:7c:
//                     fe:f3:29:43:42:f3:66:7d:bf:21:c6:3f:de:18:f5:
//                     eb:0d:6a:e6:b9:4d:58:1d:86:78:12:58:a3:62:17:
//                     79:d1:53:b3:0c:31:c8:ef:ba:fc:c5:50:d6:af:8b:
//                     cc:89:e4:6e:53:2b:e3:74:4f:a8:43:da:a0:f9:f1:
//                     14:b3
//                 Exponent: 65537 (0x10001)
//         X509v3 extensions:
//             X509v3 Basic Constraints:
//                 CA:FALSE
//
//             X509v3 Subject Key Identifier:
//                 2D:09:1B:BA:63:C2:4F:1A:88:0A:8D:0E:52:F4:42:17:76:9E:10:27
//             X509v3 Key Usage: critical
//             X509v3 Extended Key Usage:
//                 TLS Web Server Authentication, TLS Web Client Authentication
//     Signature Algorithm: sha256WithRSAEncryption
//          11:7c:47:35:29:f6:4d:2f:ef:97:40:02:57:52:79:3f:70:27:
//          c5:ee:ed:6b:8c:fc:93:ba:8d:21:a5:9a:f1:5a:21:a5:cb:21:
//          4d:d5:35:b7:ec:5e:1a:80:00:fe:8e:00:c2:bf:80:13:5e:54:
//          da:a9:eb:54:c3:93:7a:0d:90:3d:9e:dc:1e:f9:19:37:dd:33:
//          00:05:56:47:a3:b9:0c:76:e3:40:8d:0b:de:d0:01:52:57:24:
//          17:02:9d:52:3f:e1:41:f3:06:53:c3:e0:95:de:ab:33:ba:5e:
//          62:d7:51:a7:f3:4c:ea:37:2a:9f:20:0a:ff:a4:6b:d1:f6:94:
//          5f:10:90:58:a7:30:95:86:f3:a8:4b:5c:be:24:49:cc:d6:d2:
//          8f:95:2e:9d:34:c6:94:1a:b6:8a:74:d6:73:cd:1c:31:fa:8e:
//          6e:10:b5:86:52:bc:ce:bb:33:3a:2c:74:96:68:b0:ae:9d:a7:
//          42:e9:28:f7:81:a5:8d:98:49:7e:30:f6:f0:1b:c1:9c:30:5f:
//          d2:1b:12:c3:61:34:17:c9:13:ba:d9:16:76:03:80:50:0a:71:
//          31:3c:07:82:73:9c:5b:b7:b8:ed:6f:f9:a9:59:fe:d7:64:5b:
//          c3:4d:d4:bf:3c:52:ef:c1:a7:8d:f2:8d:c1:5a:aa:19:47:a8:
//          e0:bf:c7:3e
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
MIIDdTCCAl2gAwIBAgIQYy0lLc2af47/u52i06RCqDANBgkqhkiG9w0BAQsFADAT
MREwDwYDVQQDDAh0ZXN0LXJzYTAgFw0yNTExMTIxMDE2MDdaGA8yMjI1MDkyNTEw
MTYwN1owWjELMAkGA1UEBhMCWFgxFTATBgNVBAcMDERlZmF1bHQgQ2l0eTEcMBoG
A1UECgwTRGVmYXVsdCBDb21wYW55IEx0ZDEWMBQGA1UEAwwNdGVzdC1yc2EudGVz
dDCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMoXx88CHtPtkAoGiaLU
eA8HQZitC7v7iRtgzS6OQMlRjHzxmTwk+N7fr/eMNJB0FygQFIX5Vk8aHaih5981
5hvIaPBBrqPDmQJiG1Jvl4Z5Yk4eYOO36v1DITgQqKWvi257tLm7bvc02XSgNGpz
SFZYkoyQMrSe4euzOIS+3TyqnUuaz1z9xLgLitGAY/CwDdfZ6ndF2N4q9g+N6Tr6
7JyujMAF06vqaIcfBxz6+YcDhvGggzzW58hS4p9z88cUc6x8/vMpQ0LzZn2/IcY/
3hj16w1q5rlNWB2GeBJYo2IXedFTswwxyO+6/MVQ1q+LzInkblMr43RPqEPaoPnx
FLMCAwEAAaN8MHowCQYDVR0TBAIwADAfBgNVHSMEGDAWgBRiTsT+FgZY/4KHQ211
jywKwIpxJzAdBgNVHQ4EFgQULQkbumPCTxqICo0OUvRCF3aeECcwDgYDVR0PAQH/
BAQDAgWgMB0GA1UdJQQWMBQGCCsGAQUFBwMBBggrBgEFBQcDAjANBgkqhkiG9w0B
AQsFAAOCAQEAEXxHNSn2TS/vl0ACV1J5P3Anxe7ta4z8k7qNIaWa8VohpcshTdU1
t+xeGoAA/o4Awr+AE15U2qnrVMOTeg2QPZ7cHvkZN90zAAVWR6O5DHbjQI0L3tAB
UlckFwKdUj/hQfMGU8Pgld6rM7peYtdRp/NM6jcqnyAK/6Rr0faUXxCQWKcwlYbz
qEtcviRJzNbSj5UunTTGlBq2inTWc80cMfqObhC1hlK8zrszOix0lmiwrp2nQuko
94GljZhJfjD28BvBnDBf0hsSw2E0F8kTutkWdgOAUApxMTwHgnOcW7e47W/5qVn+
12Rbw03UvzxS78GnjfKNwVqqGUeo4L/HPg==
-----END CERTIFICATE-----
)EOF";

std::string server_root_chain_rsa_pem = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDBjCCAe6gAwIBAgIRAIlbFz9equFy7I8tNZpLvnMwDQYJKoZIhvcNAQELBQAw
EzERMA8GA1UEAwwIdGVzdC1yc2EwIBcNMjUxMTEyMTIwODMzWhgPMjIyNTA5MjUx
MjA4MzNaMBMxETAPBgNVBAMMCHRlc3QtcnNhMIIBIjANBgkqhkiG9w0BAQEFAAOC
AQ8AMIIBCgKCAQEAtzkfO4XL1jlsFQYLQH+GK05ZQkaHs30FGjwt3aeWLnW2LX06
VGGb/pyKts3S+Z2UZjNXn9/VV6Objzeefq+PbjQVGWYh8MhGwn587NHz9i1mxHOc
5DKNA8NPXloYZFBen0UEykVdISYjdOBIOvfbB1eAo809ROkhniv8bqUdG9ZbAzpk
qyR5PbUN5hWGquZWu5NE8oaYkYbZ377WpV9UhjkjTZAhOUXVp6XQ6RF7GHzzdI9/
sEYKmiYgxH/1OSJayIscEGHa5EF2M3TH7tLco265SdYECMpaXSyvpOY9VqAlXLMh
SLo1vcUmsib0ICZ0kQX8zf7m+SbnnBrrcloqNQIDAQABo1MwUTAdBgNVHQ4EFgQU
/JvuGresYp35PfJYVMJTnNcUMpMwHwYDVR0jBBgwFoAU/JvuGresYp35PfJYVMJT
nNcUMpMwDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAQEAlByM8jOv
rww7Al16Fgs/jKqctJMMNN+UmpMw2IY+asJ6Kw4LQf8JkojNZusbhreyxzUYVsty
3LE5/GppsMLdEifclJIu+ewElJvulnJOpgFgmwjYeAndu15fr/5yzaZF0ojH1Abr
EGhNdddYZAzMQ7DC88udPwFUM+YxzLg+QA1OU7XsXbwQu/srpJaVhvX3Iy8bwgMp
JFVL6nj7VIDGMpgM6wqvRRIrKdJ7zeY2XnucnRRpGiHRxUBGMfZdLQJT3DptT+vO
tbLx99nnT9IGDxheSQ1osyRsY0JtJAryujC5rgKlVpazOw62V9dxntzJ+nUeqdKh
pikAr9Z1f7sC1g==
-----END CERTIFICATE-----
)EOF";

std::string server_root_chain_rsa_der_b64 = R"EOF(
MIIDBjCCAe6gAwIBAgIRAIlbFz9equFy7I8tNZpLvnMwDQYJKoZIhvcNAQELBQAwEzERMA8GA1UE
AwwIdGVzdC1yc2EwIBcNMjUxMTEyMTIwODMzWhgPMjIyNTA5MjUxMjA4MzNaMBMxETAPBgNVBAMM
CHRlc3QtcnNhMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAtzkfO4XL1jlsFQYLQH+G
K05ZQkaHs30FGjwt3aeWLnW2LX06VGGb/pyKts3S+Z2UZjNXn9/VV6Objzeefq+PbjQVGWYh8MhG
wn587NHz9i1mxHOc5DKNA8NPXloYZFBen0UEykVdISYjdOBIOvfbB1eAo809ROkhniv8bqUdG9Zb
AzpkqyR5PbUN5hWGquZWu5NE8oaYkYbZ377WpV9UhjkjTZAhOUXVp6XQ6RF7GHzzdI9/sEYKmiYg
xH/1OSJayIscEGHa5EF2M3TH7tLco265SdYECMpaXSyvpOY9VqAlXLMhSLo1vcUmsib0ICZ0kQX8
zf7m+SbnnBrrcloqNQIDAQABo1MwUTAdBgNVHQ4EFgQU/JvuGresYp35PfJYVMJTnNcUMpMwHwYD
VR0jBBgwFoAU/JvuGresYp35PfJYVMJTnNcUMpMwDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0B
AQsFAAOCAQEAlByM8jOvrww7Al16Fgs/jKqctJMMNN+UmpMw2IY+asJ6Kw4LQf8JkojNZusbhrey
xzUYVsty3LE5/GppsMLdEifclJIu+ewElJvulnJOpgFgmwjYeAndu15fr/5yzaZF0ojH1AbrEGhN
dddYZAzMQ7DC88udPwFUM+YxzLg+QA1OU7XsXbwQu/srpJaVhvX3Iy8bwgMpJFVL6nj7VIDGMpgM
6wqvRRIrKdJ7zeY2XnucnRRpGiHRxUBGMfZdLQJT3DptT+vOtbLx99nnT9IGDxheSQ1osyRsY0Jt
JAryujC5rgKlVpazOw62V9dxntzJ+nUeqdKhpikAr9Z1f7sC1g==
)EOF";

std::string server_root_cert_rsa_der_b64 = R"EOF(
MIIDdTCCAl2gAwIBAgIQYy0lLc2af47/u52i06RCqDANBgkqhkiG9w0BAQsFADATMREwDwYDVQQD
DAh0ZXN0LXJzYTAgFw0yNTExMTIxMDE2MDdaGA8yMjI1MDkyNTEwMTYwN1owWjELMAkGA1UEBhMC
WFgxFTATBgNVBAcMDERlZmF1bHQgQ2l0eTEcMBoGA1UECgwTRGVmYXVsdCBDb21wYW55IEx0ZDEW
MBQGA1UEAwwNdGVzdC1yc2EudGVzdDCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMoX
x88CHtPtkAoGiaLUeA8HQZitC7v7iRtgzS6OQMlRjHzxmTwk+N7fr/eMNJB0FygQFIX5Vk8aHaih
59815hvIaPBBrqPDmQJiG1Jvl4Z5Yk4eYOO36v1DITgQqKWvi257tLm7bvc02XSgNGpzSFZYkoyQ
MrSe4euzOIS+3TyqnUuaz1z9xLgLitGAY/CwDdfZ6ndF2N4q9g+N6Tr67JyujMAF06vqaIcfBxz6
+YcDhvGggzzW58hS4p9z88cUc6x8/vMpQ0LzZn2/IcY/3hj16w1q5rlNWB2GeBJYo2IXedFTswwx
yO+6/MVQ1q+LzInkblMr43RPqEPaoPnxFLMCAwEAAaN8MHowCQYDVR0TBAIwADAfBgNVHSMEGDAW
gBRiTsT+FgZY/4KHQ211jywKwIpxJzAdBgNVHQ4EFgQULQkbumPCTxqICo0OUvRCF3aeECcwDgYD
VR0PAQH/BAQDAgWgMB0GA1UdJQQWMBQGCCsGAQUFBwMBBggrBgEFBQcDAjANBgkqhkiG9w0BAQsF
AAOCAQEAEXxHNSn2TS/vl0ACV1J5P3Anxe7ta4z8k7qNIaWa8VohpcshTdU1t+xeGoAA/o4Awr+A
E15U2qnrVMOTeg2QPZ7cHvkZN90zAAVWR6O5DHbjQI0L3tABUlckFwKdUj/hQfMGU8Pgld6rM7pe
YtdRp/NM6jcqnyAK/6Rr0faUXxCQWKcwlYbzqEtcviRJzNbSj5UunTTGlBq2inTWc80cMfqObhC1
hlK8zrszOix0lmiwrp2nQuko94GljZhJfjD28BvBnDBf0hsSw2E0F8kTutkWdgOAUApxMTwHgnOc
W7e47W/5qVn+12Rbw03UvzxS78GnjfKNwVqqGUeo4L/HPg==
)EOF";

std::string server_root_private_key_rsa_pem = R"EOF(
-----BEGIN RSA PRIVATE KEY-----
MIIEpAIBAAKCAQEAyhfHzwIe0+2QCgaJotR4DwdBmK0Lu/uJG2DNLo5AyVGMfPGZ
PCT43t+v94w0kHQXKBAUhflWTxodqKHn3zXmG8ho8EGuo8OZAmIbUm+XhnliTh5g
47fq/UMhOBCopa+Lbnu0ubtu9zTZdKA0anNIVliSjJAytJ7h67M4hL7dPKqdS5rP
XP3EuAuK0YBj8LAN19nqd0XY3ir2D43pOvrsnK6MwAXTq+pohx8HHPr5hwOG8aCD
PNbnyFLin3PzxxRzrHz+8ylDQvNmfb8hxj/eGPXrDWrmuU1YHYZ4ElijYhd50VOz
DDHI77r8xVDWr4vMieRuUyvjdE+oQ9qg+fEUswIDAQABAoIBAQDKAndiP7ZdFazT
uLFAKK5SJ2i0mtWN9OOakGrJTL0KABA0nLQV4Mc80dBt3KJ2evTiwSAiw5g4vdxD
woOrJY982hm7f4x4en6qWTMCdjW63/8aI1eqiR/GRaIhDtXluNHhgJqoxekoBpYP
9Eww1EfMuADVrRZiYidmmeG3H6q6hfJrIR9Geb/h0pAi7RiPDWaV3vYxvYk3Hg9G
I+lIwovQQrxWDugrqpjQDTJUD+i13IyG88FFB1APB+Rqsv1sFEyVBFgs5NIcK4Hl
FSY86C859B+VqNYI7Z0EhTxKGJYCwbqhOePkVJdOm48CKLoKj8a+40tVVKJSDrav
5NGtjzVRAoGBAOvP6VaBaQ8DlJG/FAseSAbLvZr+fzCYtc/CmdcweHmsVX+2E2a3
/PlaEFEMhoyBAazTjF0vJe3VGRhyp3aKh4FvpCABjR7umrZmeznvh62fI7Ngk4Lr
dWzfvI3a9s/pvlBt3TpkNQVF2hWJ3PJJ9bYkSEszL3cH9vJIY7U1zeWHAoGBANtk
350jqwGcmhquWPNd9D2mYm6HOmUR+qV3qzk30Thdl5jLWNMfux2WH19gZE6dJfFk
8RBEsfr2/Dh5aNz/NP6kyoBKwjMw3XeC7cvF0AzEDmg//eoFtfHYU1tditOHAuk9
/iVRTXo5OJiFvbOxWfb7wViuzPXzn2prLsLdmuJ1AoGAVPH4ZCkJ51aq1jW2yqqF
16zdCFBVEPRxyf2X3WSggXQK+I5mPsJYZpqC9i9E6KgwKkmqboblat8wwxXKLXGJ
jp7gyIbGhzX8lWglS6F1hp2lBqDrgmW/TxDpo1AVSKAy5lYtMzOVxeh7vvaCmOT7
ljlLsYsmtgIweuaIxGY1XVECgYB88noXuFSP2mw5fcnS8FNFORkd8Y3kOdURn5G4
SH2zKDpKHqU7t/qM4w6C9xapXv5Y+DACH91tHHSQhTSfiAjabWeWoPzwwoeepMZh
IwtV+eJqpOcq/I2eaqEui5ug1GdoBpJTFnaVgTkmRCTBzeN6se5vXz4DZPgJV3mO
KT8ocQKBgQCvJ7RPd2u8OVxldiqtlwOoNyErbiga/0rd8HP2TLnhQzYg+DpvIN0l
Gs/f3GRVHHTso72lj5sER2k/kTGp2PTOUjcrGDrh+h7tZEYYVy+kcxfXnn/0He3+
GLRZInotRxU37IebI6HSX5U3D/7rzTD29TbsNmWMDs+CQWoJzoIQvg==
-----END RSA PRIVATE KEY-----
)EOF";

// Expired certificate generated with OpenSSL
// Certificate:
//     Data:
//         Version: 3 (0x2)
//         Serial Number:
//             66:58:a3:c3:c2:51:25:5d:20:aa:50:53:47:75:1b:dc:69:ec:8a:16
//         Signature Algorithm: sha256WithRSAEncryption
//         Issuer: C=XX, ST=StateName, L=CityName, O=CompanyName, OU=CompanySectionName,
//         CN=CommonNameOrHostname Validity
//             Not Before: Jan  1 00:00:00 2008 GMT
//             Not After : Jan  2 00:00:00 2008 GMT

std::string expired_cert = R"EOF(
-----BEGIN CERTIFICATE-----
MIIF7zCCA9egAwIBAgIUZlijw8JRJV0gqlBTR3Ub3GnsihYwDQYJKoZIhvcNAQEL
BQAwgYYxCzAJBgNVBAYTAlhYMRIwEAYDVQQIDAlTdGF0ZU5hbWUxETAPBgNVBAcM
CENpdHlOYW1lMRQwEgYDVQQKDAtDb21wYW55TmFtZTEbMBkGA1UECwwSQ29tcGFu
eVNlY3Rpb25OYW1lMR0wGwYDVQQDDBRDb21tb25OYW1lT3JIb3N0bmFtZTAeFw0w
ODAxMDEwMDAwMDBaFw0wODAxMDIwMDAwMDBaMIGGMQswCQYDVQQGEwJYWDESMBAG
A1UECAwJU3RhdGVOYW1lMREwDwYDVQQHDAhDaXR5TmFtZTEUMBIGA1UECgwLQ29t
cGFueU5hbWUxGzAZBgNVBAsMEkNvbXBhbnlTZWN0aW9uTmFtZTEdMBsGA1UEAwwU
Q29tbW9uTmFtZU9ySG9zdG5hbWUwggIiMA0GCSqGSIb3DQEBAQUAA4ICDwAwggIK
AoICAQCk6SS4i4AnoC5EbCJoNi8KTNqY3jr8EaKLZpq5hGRLxck4scfJtFj/1xca
8oIvKzqlYhk9PzR7446KS8DIti918O5qeL19jCjtIFgZA9FrK0zY7eh6PjVzFJH1
9dFIwxcwAkOjl2YrOwePeOgSC6SwbZOyLjW71WP1hJ7I7W29N9S0Hb5USQLEx4WE
ziOMXAuR984VRH254B9xBmbXQzG2xVZ7Tdbw6+OyzzzT0UBBhDy8ZoHNNehkbp3l
v2zhNT8SH7WTJzh4mYO5jOf5DhpM8vPYS2xBh12DukRwlKJInmWHwNGBUuN470AT
rjIj4bcgS823OXO1lx1ejQTFJrNr6jHNV7F495XWHWf2bzzSPqWP9Z4xVQvWlK9m
03QfoXp26qanmJwaSg47y6ZZBQOrIxCzedWDevkZKV4fIf+nZjuOt5JdDue6Cyp4
LLkjO3jzQBt79p9TLlEa5Ssuj4peCAUaF/sKL6Wsh8UVPBS1HK1Z8x30E1GXIR5R
YJ9xwyzikF4jmiAeoLWlOl7zYpe/XNq1hHrsSWeyfE3rzvQ1RWidy1e4VN3+MlAn
tDyxWFkbH6qJ1gYtnCNCF7VBkWEmnjMl6kRpnKKrT5c0vC9xIX6ZFT89/B4sRY5m
szZY8GK0a7yyUVon/m52+/ixyP9QXcycGjV+064mI+gWu+7yuwIDAQABo1MwUTAd
BgNVHQ4EFgQU4KY4hK7TOi/iRjD78Ne4RocsZ2wwHwYDVR0jBBgwFoAU4KY4hK7T
Oi/iRjD78Ne4RocsZ2wwDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOC
AgEAUuQQzp+wwsqOC4nHN/PqEopqBObvAARxywdnyAkC23vUHHftrvOVIuTCgSMJ
g0c8uUQzF+/XITikfVJsa/B+hOIUqg8Ej6WKuQjJvWXAP59flc7Gts+UyTjJ9xer
IuZ/2XIM3haTWfYsBuvyQqR5PG8MsrH29PL6OSC8nweYQMn8fF5heb9QabvMMq8e
XJRw135DvL8PobqdO1j0fxs4paHjx9jScPzeo6VOs9mqT1jtXbdwwNOcqbyG9yOT
c/HhogFYXPlfuX8aP/FO9GHVivwkdfZvXkkYft2RqnYrx5EzVOxXi/l13lDXUhqs
qM107UyLUfdRX0Vbol8NpOb1/Y6ariD2FUUj6BeJndO+SpCIFmBo4Jn8sLW6nHvR
OuJKUOnD1EArbJqZtySyMd/QyvZKGS0fqZr9Yr1sjOLpERjPmLYjpc/Rc9OmInMa
Xr1kZIW0r1VApjaQFXDlmVajhkvoVRxGqWzmOxRohdxxHT7qb38T7Eq2tCDcawNr
cBMv+uVPviiOF1EbpmwSSt3zaZ7dQaVvV+ETbKk8DGtEOiKip6UxIi7oMbl2rtn6
mL1pEttiJKykCM3v6V/q/C2NCHtnTpxoYDktE6pVIBYT1wft6ah2IFdLr4Ug3pft
LTuaBIrw2gmfFpfDIQ9mYQjL9KgIcc0k6lvzwAuw39xvl2s=
-----END CERTIFICATE-----
)EOF";

class IAMRolesAnywhereX509CredentialsProviderTest : public testing::Test {
public:
  ~IAMRolesAnywhereX509CredentialsProviderTest() override = default;

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
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
};

TEST_F(IAMRolesAnywhereX509CredentialsProviderTest, InvalidSource) {
  envoy::config::core::v3::DataSource certificate_data_source, private_key_data_source,
      cert_chain_data_source;
  api_ = Api::createApiForTest();
  dispatcher_ = api_->allocateDispatcher("test_thread");

  auto watched_dir = std::make_unique<::envoy::config::core::v3::WatchedDirectory>();

  certificate_data_source.set_allocated_watched_directory(watched_dir.release());
  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      context_, certificate_data_source, private_key_data_source, cert_chain_data_source);
  auto status = provider->initialize();
  EXPECT_FALSE(status.ok());
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

  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      context_, certificate_data_source, private_key_data_source, cert_chain_data_source);
  auto status = provider->initialize();
  EXPECT_FALSE(status.ok());
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

  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      context_, certificate_data_source, private_key_data_source, cert_chain_data_source);
  auto status = provider->initialize();
  EXPECT_FALSE(status.ok());
  EXPECT_FALSE(provider->getCredentials().certificatePrivateKey().has_value());
}

// Invalid cert algorithm generated with OpenSSL. IAM Roles Anywhere does not
// support this public key algorithm
//
// Certificate:
//     Data:
//         Version: 3 (0x2)
//         Serial Number:
//             31:23:a6:e3:2f:23:89:13:91:c2:5b:69:0a:ff:45:4a
//         Signature Algorithm: sha256WithRSAEncryption
//         Issuer: CN=test-rsa
//         Validity
//             Not Before: Nov 24 20:48:11 2024 GMT
//             Not After : Nov 24 21:48:10 2025 GMT
//         Subject: C=XX, L=Default City, O=Default Company Ltd, CN=invalid-algorithm
//         Subject Public Key Info:
//             Public Key Algorithm: ED448

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
  ON_CALL(context_, api()).WillByDefault(testing::ReturnRef(*api_));

  dispatcher_ = api_->allocateDispatcher("test_thread");
  // Filesystem - Certs issued from a subordinate to test certificate chain. Verify that we read and
  // convert these correctly.
  auto filename_cert = TestEnvironment::writeStringToFileForTest("cert", invalid_cert_algorithm);
  certificate_data_source.set_filename(filename_cert);
  EXPECT_CALL(context_.api_.file_system_, fileReadToEnd(filename_cert))
      .WillRepeatedly(Return(invalid_cert_algorithm));
  auto filename_pkey =
      TestEnvironment::writeStringToFileForTest("pkey", server_subordinate_private_key_ecdsa_pem);
  private_key_data_source.set_filename(filename_pkey);
  EXPECT_CALL(context_.api_.file_system_, fileReadToEnd(filename_pkey))
      .WillRepeatedly(Return(server_subordinate_private_key_ecdsa_pem));
  auto filename_chain =
      TestEnvironment::writeStringToFileForTest("chain", server_subordinate_chain_ecdsa_pem);
  cert_chain_data_source.set_filename(filename_chain);
  EXPECT_CALL(context_.api_.file_system_, fileReadToEnd(filename_chain))
      .WillRepeatedly(Return(server_subordinate_chain_ecdsa_pem));

  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      context_, certificate_data_source, private_key_data_source, cert_chain_data_source);
  auto status = provider->initialize();
  EXPECT_TRUE(status.ok());
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
  ON_CALL(context_, api()).WillByDefault(testing::ReturnRef(*api_));
  dispatcher_ = api_->allocateDispatcher("test_thread");
  // Filesystem - Certs issued from a subordinate to test certificate chain. Verify that we read and
  // convert these correctly.
  auto filename_cert = TestEnvironment::writeStringToFileForTest("cert", missing_serial);
  certificate_data_source.set_filename(filename_cert);
  EXPECT_CALL(context_.api_.file_system_, fileReadToEnd(filename_cert))
      .WillRepeatedly(Return(missing_serial));
  auto filename_pkey =
      TestEnvironment::writeStringToFileForTest("pkey", server_subordinate_private_key_ecdsa_pem);
  private_key_data_source.set_filename(filename_pkey);
  EXPECT_CALL(context_.api_.file_system_, fileReadToEnd(filename_pkey))
      .WillRepeatedly(Return(server_subordinate_private_key_ecdsa_pem));
  auto filename_chain =
      TestEnvironment::writeStringToFileForTest("chain", server_subordinate_chain_ecdsa_pem);
  cert_chain_data_source.set_filename(filename_chain);
  EXPECT_CALL(context_.api_.file_system_, fileReadToEnd(filename_chain))
      .WillRepeatedly(Return(server_subordinate_chain_ecdsa_pem));

  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      context_, certificate_data_source, private_key_data_source, cert_chain_data_source);
  auto status = provider->initialize();
  EXPECT_TRUE(status.ok());
  auto credentials = provider->getCredentials();

  EXPECT_FALSE(credentials.certificateDerB64().has_value());
}

TEST_F(IAMRolesAnywhereX509CredentialsProviderTest, LoadChainFailed) {
  envoy::config::core::v3::DataSource certificate_data_source, private_key_data_source,
      cert_chain_data_source;
  api_ = Api::createApiForTest();
  ON_CALL(context_, api()).WillByDefault(testing::ReturnRef(*api_));
  dispatcher_ = api_->allocateDispatcher("test_thread");
  // Filesystem - Certs issued from a subordinate to test certificate chain. Verify that we read and
  // convert these correctly.
  auto filename_cert = TestEnvironment::writeStringToFileForTest("cert", missing_serial);
  certificate_data_source.set_filename(filename_cert);
  EXPECT_CALL(context_.api_.file_system_, fileReadToEnd(filename_cert))
      .WillRepeatedly(Return(missing_serial));
  auto filename_pkey =
      TestEnvironment::writeStringToFileForTest("pkey", server_subordinate_private_key_ecdsa_pem);
  private_key_data_source.set_filename(filename_pkey);
  EXPECT_CALL(context_.api_.file_system_, fileReadToEnd(filename_pkey))
      .WillRepeatedly(Return(server_subordinate_private_key_ecdsa_pem));
  // No chain is set in the data source
  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      context_, certificate_data_source, private_key_data_source, cert_chain_data_source);
  auto status = provider->initialize();
  EXPECT_FALSE(status.ok());
}

TEST_F(IAMRolesAnywhereX509CredentialsProviderTest, LoadPrivateKeyFailed) {
  envoy::config::core::v3::DataSource certificate_data_source, private_key_data_source,
      cert_chain_data_source;
  api_ = Api::createApiForTest();
  ON_CALL(context_, api()).WillByDefault(testing::ReturnRef(*api_));
  dispatcher_ = api_->allocateDispatcher("test_thread");
  // Filesystem - Certs issued from a subordinate to test certificate chain. Verify that we read and
  // convert these correctly.
  auto filename_cert = TestEnvironment::writeStringToFileForTest("cert", missing_serial);
  certificate_data_source.set_filename(filename_cert);
  EXPECT_CALL(context_.api_.file_system_, fileReadToEnd(filename_cert))
      .WillRepeatedly(Return(missing_serial));
  auto filename_chain =
      TestEnvironment::writeStringToFileForTest("chain", server_subordinate_chain_ecdsa_pem);
  cert_chain_data_source.set_filename(filename_chain);
  EXPECT_CALL(context_.api_.file_system_, fileReadToEnd(filename_chain))
      .WillRepeatedly(Return(server_subordinate_chain_ecdsa_pem));

  // No private key is set in the data source
  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      context_, certificate_data_source, private_key_data_source, cert_chain_data_source);
  auto status = provider->initialize();
  EXPECT_FALSE(status.ok());
}

TEST_F(IAMRolesAnywhereX509CredentialsProviderTest, LoadCredentials) {

  envoy::config::core::v3::DataSource certificate_data_source, private_key_data_source,
      cert_chain_data_source;
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

  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      context_, certificate_data_source, private_key_data_source, absl::nullopt);
  auto status = provider->initialize();
  EXPECT_TRUE(status.ok());
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
  // Not After : Sep 25 10:16:07 2225 GMT
  SystemTime a(std::chrono::seconds(8070142567));
  EXPECT_EQ(credentials.certificateExpiration(), a);
  // std::chrono::time_point<std::chrono::system_clock>(std::chrono::milliseconds(1762900931000000)));

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
      context_, certificate_data_source, private_key_data_source, cert_chain_data_source);
  status = provider->initialize();
  EXPECT_TRUE(status.ok());

  credentials = provider->getCredentials();

  EXPECT_TRUE(credentials.certificateDerB64().has_value());
  EXPECT_EQ(credentials.certificateDerB64().value(), server_subordinate_cert_ecdsa_der_b64);
  EXPECT_TRUE(credentials.publicKeySignatureAlgorithm().has_value());
  EXPECT_EQ(credentials.publicKeySignatureAlgorithm(),
            X509Credentials::PublicKeySignatureAlgorithm::ECDSA);
  EXPECT_TRUE(credentials.certificateSerial().has_value());
  EXPECT_EQ(credentials.certificateSerial(), "1171381937749039847038735600209560248209027419017");
  EXPECT_TRUE(credentials.certificatePrivateKey().has_value());
  EXPECT_EQ(credentials.certificatePrivateKey(), server_subordinate_private_key_ecdsa_pem);
  EXPECT_TRUE(credentials.certificateChainDerB64().has_value());
  EXPECT_EQ(credentials.certificateChainDerB64(), server_subordinate_chain_ecdsa_der_b64);

  // Filesystem - Certs issued from a subordinate to test certificate chain. Verify that we read and
  // convert these correctly.
  ON_CALL(context_, api()).WillByDefault(testing::ReturnRef(*api_));

  auto filename1 =
      TestEnvironment::writeStringToFileForTest("cert", server_subordinate_cert_ecdsa_pem);
  EXPECT_CALL(context_.api_.file_system_, fileReadToEnd(filename1))
      .WillRepeatedly(Return(server_subordinate_cert_ecdsa_pem));

  TestEnvironment::setEnvVar(cert_env, server_root_cert_ecdsa_pem, 1);
  yaml = fmt::format(R"EOF(
    filename: "{}"
  )EOF",
                     filename1);
  TestUtility::loadFromYamlAndValidate(yaml, certificate_data_source);
  auto filename2 =
      TestEnvironment::writeStringToFileForTest("pkey", server_subordinate_private_key_ecdsa_pem);
  EXPECT_CALL(context_.api_.file_system_, fileReadToEnd(filename2))
      .WillRepeatedly(Return(server_subordinate_private_key_ecdsa_pem));

  yaml = fmt::format(R"EOF(
    filename: "{}"
  )EOF",
                     filename2);

  TestUtility::loadFromYamlAndValidate(yaml, private_key_data_source);
  auto filename3 =
      TestEnvironment::writeStringToFileForTest("chain", server_subordinate_chain_ecdsa_pem);
  EXPECT_CALL(context_.api_.file_system_, fileReadToEnd(filename3))
      .WillRepeatedly(Return(server_subordinate_chain_ecdsa_pem));

  yaml = fmt::format(R"EOF(
    filename: "{}"
  )EOF",
                     filename3);

  TestUtility::loadFromYamlAndValidate(yaml, cert_chain_data_source);

  provider.reset();

  provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      context_, certificate_data_source, private_key_data_source, cert_chain_data_source);
  status = provider->initialize();
  EXPECT_TRUE(status.ok());
  credentials = provider->getCredentials();

  EXPECT_TRUE(credentials.certificateDerB64().has_value());
  EXPECT_EQ(credentials.certificateDerB64().value(), server_subordinate_cert_ecdsa_der_b64);
  EXPECT_TRUE(credentials.publicKeySignatureAlgorithm().has_value());
  EXPECT_EQ(credentials.publicKeySignatureAlgorithm(),
            X509Credentials::PublicKeySignatureAlgorithm::ECDSA);
  EXPECT_TRUE(credentials.certificateSerial().has_value());
  EXPECT_EQ(credentials.certificateSerial(), "1171381937749039847038735600209560248209027419017");
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
      context_, certificate_data_source, private_key_data_source, cert_chain_data_source);
  status = provider->initialize();
  EXPECT_TRUE(status.ok());
  credentials = provider->getCredentials();

  EXPECT_TRUE(credentials.certificateDerB64().has_value());
  EXPECT_EQ(credentials.certificateDerB64().value(), server_subordinate_cert_ecdsa_der_b64);
  EXPECT_TRUE(credentials.publicKeySignatureAlgorithm().has_value());
  EXPECT_EQ(credentials.publicKeySignatureAlgorithm(),
            X509Credentials::PublicKeySignatureAlgorithm::ECDSA);
  EXPECT_TRUE(credentials.certificateSerial().has_value());
  EXPECT_EQ(credentials.certificateSerial(), "1171381937749039847038735600209560248209027419017");
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
      context_, certificate_data_source, private_key_data_source, cert_chain_data_source);
  status = provider->initialize();
  EXPECT_TRUE(status.ok());
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
  // Not After : Sep 25 10:16:07 2225 GMT
  SystemTime b(std::chrono::seconds(8070142567));
  EXPECT_EQ(credentials.certificateExpiration(), b);
}

TEST(EmptyPem, PemToAlgorithmSerialExpiration) {

  envoy::config::core::v3::DataSource certificate_data_source, private_key_data_source,
      cert_chain_data_source;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  X509Credentials::PublicKeySignatureAlgorithm algorithm;
  std::string serial;
  SystemTime time;

  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      context, certificate_data_source, private_key_data_source, absl::nullopt);

  auto provider_friend = IAMRolesAnywhereX509CredentialsProviderFriend(std::move(provider));
  auto status = provider_friend.pemToAlgorithmSerialExpiration("", algorithm, serial, time);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.message(), "Invalid certificate size");
}

TEST(ExpiredPem, PemToAlgorithmSerialExpiration) {

  envoy::config::core::v3::DataSource certificate_data_source, private_key_data_source,
      cert_chain_data_source;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  X509Credentials::PublicKeySignatureAlgorithm algorithm;
  std::string serial;
  SystemTime time;

  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      context, certificate_data_source, private_key_data_source, absl::nullopt);

  auto provider_friend = IAMRolesAnywhereX509CredentialsProviderFriend(std::move(provider));
  auto status =
      provider_friend.pemToAlgorithmSerialExpiration(expired_cert, algorithm, serial, time);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.message(), "Certificate has already expired");
}

TEST(PemTooLarge, PemToAlgorithmSerialExpiration) {

  envoy::config::core::v3::DataSource certificate_data_source, private_key_data_source,
      cert_chain_data_source;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  std::string large_cert(10240 + 10, 'a');

  X509Credentials::PublicKeySignatureAlgorithm algorithm;
  std::string serial;
  SystemTime time;

  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      context, certificate_data_source, private_key_data_source, absl::nullopt);

  auto provider_friend = IAMRolesAnywhereX509CredentialsProviderFriend(std::move(provider));
  auto status = provider_friend.pemToAlgorithmSerialExpiration(large_cert, algorithm, serial, time);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.message(), "Invalid certificate size");
}

TEST(JunkPem, PemToAlgorithmSerialExpiration) {

  envoy::config::core::v3::DataSource certificate_data_source, private_key_data_source,
      cert_chain_data_source;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  std::string junk_pem(2000, 'a');

  X509Credentials::PublicKeySignatureAlgorithm algorithm;
  std::string serial;
  SystemTime time;

  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      context, certificate_data_source, private_key_data_source, absl::nullopt);

  auto provider_friend = IAMRolesAnywhereX509CredentialsProviderFriend(std::move(provider));
  auto status = provider_friend.pemToAlgorithmSerialExpiration(junk_pem, algorithm, serial, time);
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.message(), StartsWith("Invalid certificate - PEM read x509 failed"));
}

TEST(ValidPemWithAppendedJunk, PemToAlgorithmSerialExpiration) {

  envoy::config::core::v3::DataSource certificate_data_source, private_key_data_source,
      cert_chain_data_source;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  std::string junk_pem;
  junk_pem.append(server_root_cert_rsa_pem);
  junk_pem.append(std::string(100, 'a'));

  X509Credentials::PublicKeySignatureAlgorithm algorithm;
  std::string serial;
  SystemTime time;

  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      context, certificate_data_source, private_key_data_source, absl::nullopt);

  auto provider_friend = IAMRolesAnywhereX509CredentialsProviderFriend(std::move(provider));
  auto status = provider_friend.pemToAlgorithmSerialExpiration(junk_pem, algorithm, serial, time);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(serial, "131827979019394590882466519576505238184");
  EXPECT_EQ(algorithm, X509Credentials::PublicKeySignatureAlgorithm::RSA);
  EXPECT_EQ(time, SystemTime(std::chrono::seconds(8070142567)));
}

TEST(JunkPem, PemToDerB64) {

  envoy::config::core::v3::DataSource certificate_data_source, private_key_data_source,
      cert_chain_data_source;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  std::string in_cert(100, 'a');

  std::string out_cert;

  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      context, certificate_data_source, private_key_data_source, absl::nullopt);

  auto provider_friend = IAMRolesAnywhereX509CredentialsProviderFriend(std::move(provider));
  auto status = provider_friend.pemToDerB64(in_cert, out_cert, false);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.message(), "No certificates found in PEM data");
}

TEST(JunkPemChain, PemToDerB64) {

  envoy::config::core::v3::DataSource certificate_data_source, private_key_data_source,
      cert_chain_data_source;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  std::string in_cert(100, 'a');

  std::string out_cert;

  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      context, certificate_data_source, private_key_data_source, absl::nullopt);

  auto provider_friend = IAMRolesAnywhereX509CredentialsProviderFriend(std::move(provider));
  auto status = provider_friend.pemToDerB64(in_cert, out_cert, true);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.message(), "No certificates found in PEM data");
}

TEST(JunkCertStartLine, PemToDerB64) {

  envoy::config::core::v3::DataSource certificate_data_source, private_key_data_source,
      cert_chain_data_source;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  std::string in_cert("-----BEGIN CERTIFICATE-----\n");
  in_cert.append("000000000");

  std::string out_cert;

  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      context, certificate_data_source, private_key_data_source, absl::nullopt);

  auto provider_friend = IAMRolesAnywhereX509CredentialsProviderFriend(std::move(provider));
  auto status = provider_friend.pemToDerB64(in_cert, out_cert, false);
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.message(), StartsWith("Certificate could not be parsed"));
}

TEST(JunkChainStartLine, PemToDerB64) {

  envoy::config::core::v3::DataSource certificate_data_source, private_key_data_source,
      cert_chain_data_source;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  std::string in_cert("-----BEGIN CERTIFICATE-----\n"
                      "");
  in_cert.append("000000000");

  std::string out_cert;

  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      context, certificate_data_source, private_key_data_source, absl::nullopt);

  auto provider_friend = IAMRolesAnywhereX509CredentialsProviderFriend(std::move(provider));
  auto status = provider_friend.pemToDerB64(in_cert, out_cert, true);
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.message(), StartsWith("Certificate chain PEM #0 could not be parsed"));
}

TEST(SingleCertTooLarge, PemToDerB64) {

  envoy::config::core::v3::DataSource certificate_data_source, private_key_data_source,
      cert_chain_data_source;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  std::string in_cert(11000, 'a');

  std::string out_cert;

  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      context, certificate_data_source, private_key_data_source, absl::nullopt);

  auto provider_friend = IAMRolesAnywhereX509CredentialsProviderFriend(std::move(provider));
  auto status = provider_friend.pemToDerB64(in_cert, out_cert, false);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.message(), "Invalid certificate size");
}

TEST(ChainTooLarge, PemToDerB64) {

  envoy::config::core::v3::DataSource certificate_data_source, private_key_data_source,
      cert_chain_data_source;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  // This buffer is the size of 6 max size certificates, we only allow 5
  std::string in_chain(10240 * 6, 'a');

  std::string out_chain;

  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      context, certificate_data_source, private_key_data_source, absl::nullopt);

  auto provider_friend = IAMRolesAnywhereX509CredentialsProviderFriend(std::move(provider));
  auto status = provider_friend.pemToDerB64(in_chain, out_chain, true);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.message(), "Invalid certificate chain size");
}

TEST(ChainParse, PemToDerB64) {

  std::string converted_pem = "MIIDdTCCAl2gAwIBAgIQYy0lLc2af47/u52i06RCqDANBgkqhkiG9w0BAQsFADAT"
                              "MREwDwYDVQQDDAh0ZXN0LXJzYTAgFw0yNTExMTIxMDE2MDdaGA8yMjI1MDkyNTEw"
                              "MTYwN1owWjELMAkGA1UEBhMCWFgxFTATBgNVBAcMDERlZmF1bHQgQ2l0eTEcMBoG"
                              "A1UECgwTRGVmYXVsdCBDb21wYW55IEx0ZDEWMBQGA1UEAwwNdGVzdC1yc2EudGVz"
                              "dDCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAMoXx88CHtPtkAoGiaLU"
                              "eA8HQZitC7v7iRtgzS6OQMlRjHzxmTwk+N7fr/eMNJB0FygQFIX5Vk8aHaih5981"
                              "5hvIaPBBrqPDmQJiG1Jvl4Z5Yk4eYOO36v1DITgQqKWvi257tLm7bvc02XSgNGpz"
                              "SFZYkoyQMrSe4euzOIS+3TyqnUuaz1z9xLgLitGAY/CwDdfZ6ndF2N4q9g+N6Tr6"
                              "7JyujMAF06vqaIcfBxz6+YcDhvGggzzW58hS4p9z88cUc6x8/vMpQ0LzZn2/IcY/"
                              "3hj16w1q5rlNWB2GeBJYo2IXedFTswwxyO+6/MVQ1q+LzInkblMr43RPqEPaoPnx"
                              "FLMCAwEAAaN8MHowCQYDVR0TBAIwADAfBgNVHSMEGDAWgBRiTsT+FgZY/4KHQ211"
                              "jywKwIpxJzAdBgNVHQ4EFgQULQkbumPCTxqICo0OUvRCF3aeECcwDgYDVR0PAQH/"
                              "BAQDAgWgMB0GA1UdJQQWMBQGCCsGAQUFBwMBBggrBgEFBQcDAjANBgkqhkiG9w0B"
                              "AQsFAAOCAQEAEXxHNSn2TS/vl0ACV1J5P3Anxe7ta4z8k7qNIaWa8VohpcshTdU1"
                              "t+xeGoAA/o4Awr+AE15U2qnrVMOTeg2QPZ7cHvkZN90zAAVWR6O5DHbjQI0L3tAB"
                              "UlckFwKdUj/hQfMGU8Pgld6rM7peYtdRp/NM6jcqnyAK/6Rr0faUXxCQWKcwlYbz"
                              "qEtcviRJzNbSj5UunTTGlBq2inTWc80cMfqObhC1hlK8zrszOix0lmiwrp2nQuko"
                              "94GljZhJfjD28BvBnDBf0hsSw2E0F8kTutkWdgOAUApxMTwHgnOcW7e47W/5qVn+"
                              "12Rbw03UvzxS78GnjfKNwVqqGUeo4L/HPg==";

  envoy::config::core::v3::DataSource certificate_data_source, private_key_data_source,
      cert_chain_data_source;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  std::string chain;

  // One legitimate certificate and junk appended to the chain
  chain.append(server_root_cert_rsa_pem);
  chain.append(std::string(4000, 'a'));

  std::string out_chain;

  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      context, certificate_data_source, private_key_data_source, absl::nullopt);
  auto status = provider->initialize();
  EXPECT_FALSE(status.ok());
  auto provider_friend = IAMRolesAnywhereX509CredentialsProviderFriend(std::move(provider));
  status = provider_friend.pemToDerB64(chain, out_chain, true);
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(out_chain, converted_pem);
}

TEST(Refresh, InvalidChainInsideRefresh) {

  envoy::config::core::v3::DataSource certificate_data_source, private_key_data_source,
      cert_chain_data_source;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Event::DispatcherPtr dispatcher;
  Api::ApiPtr api;

  private_key_data_source.mutable_inline_string();
  private_key_data_source.set_inline_string(server_subordinate_private_key_ecdsa_pem);
  certificate_data_source.mutable_inline_string();
  certificate_data_source.set_inline_string(server_subordinate_cert_ecdsa_pem);
  cert_chain_data_source.mutable_inline_string();
  cert_chain_data_source.set_inline_string("junk");

  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      context, certificate_data_source, private_key_data_source, cert_chain_data_source);
  auto status = provider->initialize();
  EXPECT_TRUE(status.ok());
  auto provider_friend = IAMRolesAnywhereX509CredentialsProviderFriend(std::move(provider));
  auto a = provider_friend.getCredentials();
  EXPECT_FALSE(provider_friend.getCredentials().certificateChainDerB64().has_value());
}

TEST(Refresh, InvalidKeyInsideRefresh) {

  envoy::config::core::v3::DataSource certificate_data_source, private_key_data_source,
      cert_chain_data_source;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  private_key_data_source.mutable_inline_string();
  private_key_data_source.set_inline_string("junk");
  certificate_data_source.mutable_inline_string();
  certificate_data_source.set_inline_string(server_subordinate_cert_ecdsa_pem);
  certificate_data_source.clear_filename();
  cert_chain_data_source.mutable_inline_string();
  cert_chain_data_source.set_inline_string(server_subordinate_chain_ecdsa_pem);

  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      context, certificate_data_source, private_key_data_source, cert_chain_data_source);
  auto status = provider->initialize();
  EXPECT_TRUE(status.ok());
  auto provider_friend = IAMRolesAnywhereX509CredentialsProviderFriend(std::move(provider));
  auto a = provider_friend.getCredentials();
  EXPECT_FALSE(provider_friend.getCredentials().certificatePrivateKey().has_value());
}

TEST(NeedsRefresh, ExpirationTimeInPast) {
  envoy::config::core::v3::DataSource certificate_data_source, private_key_data_source,
      cert_chain_data_source;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  private_key_data_source.mutable_inline_string();
  private_key_data_source.set_inline_string(server_subordinate_private_key_ecdsa_pem);
  certificate_data_source.mutable_inline_string();
  certificate_data_source.set_inline_string(server_subordinate_cert_ecdsa_pem);
  cert_chain_data_source.mutable_inline_string();
  cert_chain_data_source.set_inline_string(server_subordinate_chain_ecdsa_pem);

  auto provider = std::make_unique<IAMRolesAnywhereX509CredentialsProvider>(
      context, certificate_data_source, private_key_data_source, cert_chain_data_source);
  auto status = provider->initialize();
  EXPECT_TRUE(status.ok());

  auto provider_friend = IAMRolesAnywhereX509CredentialsProviderFriend(std::move(provider));

  // Set expiration time to the past
  auto past_time = context.api().timeSource().systemTime() - std::chrono::hours(1);
  provider_friend.setExpirationTime(past_time);

  // Should return true (needs refresh) when expiration is in the past
  EXPECT_TRUE(provider_friend.needsRefresh());
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
