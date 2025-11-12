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
//             Not Before: Nov 12 06:35:30 2025 GMT
//             Not After : Mar 15 06:35:30 3025 GMT
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
MIIB8zCCAZigAwIBAgIQA+x+3DenV9P2mjCafZGlFjAKBggqhkjOPQQDAjAaMRgwFgYDVQQDDA90
ZXN0LWVjZHNhLXAyNTYwIBcNMjUxMTEyMDYzNTMwWhgPMzAyNTAzMTUwNjM1MzBaMFwxCzAJBgNV
BAYTAlhYMRUwEwYDVQQHDAxEZWZhdWx0IENpdHkxHDAaBgNVBAoME0RlZmF1bHQgQ29tcGFueSBM
dGQxGDAWBgNVBAMMD3Rlc3QtZWNkc2EudGVzdDBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABAYp
XnHm8lZTCl2c5MHYnFZAGCFTfmMwGepOyuBP96a04ja7RA1h710CYxOhLVA/9BpHgAqRRnwkHssx
/MPPemSjfDB6MAkGA1UdEwQCMAAwHwYDVR0jBBgwFoAU3mwNQO2JYNC/qQJDxOKb/ZKWm0owHQYD
VR0OBBYEFCjMYb3fq/fp6MMNyqSbi81QX/eJMA4GA1UdDwEB/wQEAwIFoDAdBgNVHSUEFjAUBggr
BgEFBQcDAQYIKwYBBQUHAwIwCgYIKoZIzj0EAwIDSQAwRgIhALZN+mYW7CntL/H9s/eM1eTn7Rpw
VhqfKrA16ssjFGOYAiEAnrUvCMrZcE9keTEPxxiTLJtaEWafJljl585h/NRRYqk=
)EOF";

std::string server_root_cert_ecdsa_pem = R"EOF(
-----BEGIN CERTIFICATE-----
MIIB8zCCAZigAwIBAgIQA+x+3DenV9P2mjCafZGlFjAKBggqhkjOPQQDAjAaMRgw
FgYDVQQDDA90ZXN0LWVjZHNhLXAyNTYwIBcNMjUxMTEyMDYzNTMwWhgPMzAyNTAz
MTUwNjM1MzBaMFwxCzAJBgNVBAYTAlhYMRUwEwYDVQQHDAxEZWZhdWx0IENpdHkx
HDAaBgNVBAoME0RlZmF1bHQgQ29tcGFueSBMdGQxGDAWBgNVBAMMD3Rlc3QtZWNk
c2EudGVzdDBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABAYpXnHm8lZTCl2c5MHY
nFZAGCFTfmMwGepOyuBP96a04ja7RA1h710CYxOhLVA/9BpHgAqRRnwkHssx/MPP
emSjfDB6MAkGA1UdEwQCMAAwHwYDVR0jBBgwFoAU3mwNQO2JYNC/qQJDxOKb/ZKW
m0owHQYDVR0OBBYEFCjMYb3fq/fp6MMNyqSbi81QX/eJMA4GA1UdDwEB/wQEAwIF
oDAdBgNVHSUEFjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwCgYIKoZIzj0EAwIDSQAw
RgIhALZN+mYW7CntL/H9s/eM1eTn7RpwVhqfKrA16ssjFGOYAiEAnrUvCMrZcE9k
eTEPxxiTLJtaEWafJljl585h/NRRYqk=
-----END CERTIFICATE-----
)EOF";

std::string server_root_private_key_ecdsa_pem = R"EOF(
-----BEGIN EC PARAMETERS-----
BggqhkjOPQMBBw==
-----END EC PARAMETERS-----
-----BEGIN EC PRIVATE KEY-----
MHcCAQEEIE0YDyPjAL4JRUnUDAvGW8bN/f5Cj6nEK0c718/sYRuPoAoGCCqGSM49
AwEHoUQDQgAEBilecebyVlMKXZzkwdicVkAYIVN+YzAZ6k7K4E/3prTiNrtEDWHv
XQJjE6EtUD/0GkeACpFGfCQeyzH8w896ZA==
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
//             Not Before: Nov 12 06:35:30 2025 GMT
//             Not After : Mar 15 06:35:30 3025 GMT
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
MIIB2TCCAX+gAwIBAgIUOSG85/MVGkt/71Tv71TECaUDDzowCgYIKoZIzj0EAwIw
GjEYMBYGA1UEAwwPdGVzdC1lY2RzYS1wMjU2MCAXDTI1MTExMjA2MzUzMFoYDzMw
MjUwMzE1MDYzNTMwWjBoMQswCQYDVQQGEwJYWDEVMBMGA1UEBwwMRGVmYXVsdCBD
aXR5MRwwGgYDVQQKDBNEZWZhdWx0IENvbXBhbnkgTHRkMSQwIgYDVQQDDBt0ZXN0
LWVjZHNhLXAyNTYtc3Vib3JkaW5hdGUwWTATBgcqhkjOPQIBBggqhkjOPQMBBwNC
AAT7OUD99rzAL8ziLg61Rcgd8QqgKxGNdG/MlMonoFohyPEVYwQswrKTSBfiLHAj
BmaLqgTfZMRoNOBjlSfGsB9jo1MwUTAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW
BBS0li5LkTAriBvxILHWxMDI/IH0LDAfBgNVHSMEGDAWgBTebA1A7Ylg0L+pAkPE
4pv9kpabSjAKBggqhkjOPQQDAgNIADBFAiEAjJcvLpqPgFx3IDjcWaeSxS62gWnC
eVZ9nmbywJNUOisCICS8x1hTbCddOfL9FRgSMELX49c45qyhtOV/gm7O/m6d
-----END CERTIFICATE-----
)EOF";

std::string server_subordinate_cert_ecdsa_der_b64 = R"EOF(
MIIB2TCCAX+gAwIBAgIUOSG85/MVGkt/71Tv71TECaUDDzowCgYIKoZIzj0EAwIwGjEYMBYGA1UE
AwwPdGVzdC1lY2RzYS1wMjU2MCAXDTI1MTExMjA2MzUzMFoYDzMwMjUwMzE1MDYzNTMwWjBoMQsw
CQYDVQQGEwJYWDEVMBMGA1UEBwwMRGVmYXVsdCBDaXR5MRwwGgYDVQQKDBNEZWZhdWx0IENvbXBh
bnkgTHRkMSQwIgYDVQQDDBt0ZXN0LWVjZHNhLXAyNTYtc3Vib3JkaW5hdGUwWTATBgcqhkjOPQIB
BggqhkjOPQMBBwNCAAT7OUD99rzAL8ziLg61Rcgd8QqgKxGNdG/MlMonoFohyPEVYwQswrKTSBfi
LHAjBmaLqgTfZMRoNOBjlSfGsB9jo1MwUTAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQWBBS0li5L
kTAriBvxILHWxMDI/IH0LDAfBgNVHSMEGDAWgBTebA1A7Ylg0L+pAkPE4pv9kpabSjAKBggqhkjO
PQQDAgNIADBFAiEAjJcvLpqPgFx3IDjcWaeSxS62gWnCeVZ9nmbywJNUOisCICS8x1hTbCddOfL9
FRgSMELX49c45qyhtOV/gm7O/m6d
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
MIIBpjCCAUygAwIBAgIQEtGBpKhUy68xOvOSxGaJKDAKBggqhkjOPQQDAjAaMRgw
FgYDVQQDDA90ZXN0LWVjZHNhLXAyNTYwIBcNMjUxMTEyMDYzNTMwWhgPMzAyNTAz
MTUwNjM1MzBaMCYxJDAiBgNVBAMMG3Rlc3QtZWNkc2EtcDI1Ni1zdWJvcmRpbmF0
ZTBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABI1wQ+Q6GKJvjHSlqZ0Sx/l8CMoC
XuXkRGCcdAn1hcFqcakNqUR0ssRYoROAlbTqfNBN6yS0kYv77g+7XJPwpxujZjBk
MBIGA1UdEwEB/wQIMAYBAf8CAQAwHwYDVR0jBBgwFoAU3mwNQO2JYNC/qQJDxOKb
/ZKWm0owHQYDVR0OBBYEFKROStaBUvO7NLfUo+Rg3tlNobWyMA4GA1UdDwEB/wQE
AwIBBjAKBggqhkjOPQQDAgNIADBFAiEAwJ6/PcehNhlj9swzMPrUuFZv6iPzF0Sv
s/2hwSfN47UCIBhWqEtkIhum1aijfLokSBxbBjnd6QnOfac5nSK31de/
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIBiDCCAS6gAwIBAgIRAIjc7qFne7vnqxXxxWcyWWkwCgYIKoZIzj0EAwIwGjEY
MBYGA1UEAwwPdGVzdC1lY2RzYS1wMjU2MCAXDTI1MTExMjA2MzUzMFoYDzMwMjUw
MzE1MDYzNTMwWjAaMRgwFgYDVQQDDA90ZXN0LWVjZHNhLXAyNTYwWTATBgcqhkjO
PQIBBggqhkjOPQMBBwNCAASr/S3+EiDZi365/O+5Q1SaO2NwBnfO2mGKdKtb9ebd
4ReDiUoeD3w5r1N89EDi5UN1PiN8/sJb3FYBvAWWBfTWo1MwUTAdBgNVHQ4EFgQU
3mwNQO2JYNC/qQJDxOKb/ZKWm0owHwYDVR0jBBgwFoAU3mwNQO2JYNC/qQJDxOKb
/ZKWm0owDwYDVR0TAQH/BAUwAwEB/zAKBggqhkjOPQQDAgNIADBFAiEAjcQVVWbR
Ea2Uyi2zuu6L96oAALVuvweulgQMxbo0zIUCIBzfqfYoxxRo8toPSnMMjkyHul3X
yu0akXHq8v4xGitR
-----END CERTIFICATE-----
)EOF";

std::string server_subordinate_chain_ecdsa_der_b64 = R"EOF(
MIIBpjCCAUygAwIBAgIQEtGBpKhUy68xOvOSxGaJKDAKBggqhkjOPQQDAjAaMRgwFgYDVQQDDA90
ZXN0LWVjZHNhLXAyNTYwIBcNMjUxMTEyMDYzNTMwWhgPMzAyNTAzMTUwNjM1MzBaMCYxJDAiBgNV
BAMMG3Rlc3QtZWNkc2EtcDI1Ni1zdWJvcmRpbmF0ZTBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IA
BI1wQ+Q6GKJvjHSlqZ0Sx/l8CMoCXuXkRGCcdAn1hcFqcakNqUR0ssRYoROAlbTqfNBN6yS0kYv7
7g+7XJPwpxujZjBkMBIGA1UdEwEB/wQIMAYBAf8CAQAwHwYDVR0jBBgwFoAU3mwNQO2JYNC/qQJD
xOKb/ZKWm0owHQYDVR0OBBYEFKROStaBUvO7NLfUo+Rg3tlNobWyMA4GA1UdDwEB/wQEAwIBBjAK
BggqhkjOPQQDAgNIADBFAiEAwJ6/PcehNhlj9swzMPrUuFZv6iPzF0Svs/2hwSfN47UCIBhWqEtk
Ihum1aijfLokSBxbBjnd6QnOfac5nSK31de/,MIIBiDCCAS6gAwIBAgIRAIjc7qFne7vnqxXxxWc
yWWkwCgYIKoZIzj0EAwIwGjEYMBYGA1UEAwwPdGVzdC1lY2RzYS1wMjU2MCAXDTI1MTExMjA2MzU
zMFoYDzMwMjUwMzE1MDYzNTMwWjAaMRgwFgYDVQQDDA90ZXN0LWVjZHNhLXAyNTYwWTATBgcqhkj
OPQIBBggqhkjOPQMBBwNCAASr/S3+EiDZi365/O+5Q1SaO2NwBnfO2mGKdKtb9ebd4ReDiUoeD3w
5r1N89EDi5UN1PiN8/sJb3FYBvAWWBfTWo1MwUTAdBgNVHQ4EFgQU3mwNQO2JYNC/qQJDxOKb/ZK
Wm0owHwYDVR0jBBgwFoAU3mwNQO2JYNC/qQJDxOKb/ZKWm0owDwYDVR0TAQH/BAUwAwEB/zAKBgg
qhkjOPQQDAgNIADBFAiEAjcQVVWbREa2Uyi2zuu6L96oAALVuvweulgQMxbo0zIUCIBzfqfYoxxR
o8toPSnMMjkyHul3Xyu0akXHq8v4xGitR
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
//             Not Before: Nov 12 06:35:30 2025 GMT
//             Not After : Mar 15 06:35:30 3025 GMT
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
MIIDdTCCAl2gAwIBAgIQYy0lLc2af47/u52i06RCqDANBgkqhkiG9w0BAQsFADAT
MREwDwYDVQQDDAh0ZXN0LXJzYTAgFw0yNTExMTIwNjM1MzBaGA8zMDI1MDMxNTA2
MzUzMFowWjELMAkGA1UEBhMCWFgxFTATBgNVBAcMDERlZmF1bHQgQ2l0eTEcMBoG
A1UECgwTRGVmYXVsdCBDb21wYW55IEx0ZDEWMBQGA1UEAwwNdGVzdC1yc2EudGVz
dDCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBANfS3JH1Tc1GQaCORikR
55aO/0yQvL6SZxl+S+Mh62aEvJl7UknyYsbzjDQhrhPZGjYoaIYE95DqKkP1EPRQ
v4UegxROukrrhJi8mrEgB2FK7GZoZCgHEncyUDbwJmsD1gp33ykq6OsU6UtbEhfO
bo9iRSe2nO0FT1TE+BJAvmMsMHtuCpGmuxNUxDwZnKd3z9uxzVH3tI+lKSWSYQQ9
UV5Cu9r6Qj3i+vY6lfR5O1uQauxpVekj0o7dhYNT01XKxbZYrW91/nGyS51Yvpn+
MlgIBx8dvwK3GJ2JXX5igGQsrJ18sDcouyY0IR/5STvp/rF1VAIbJkPSiE0gBntt
CKECAwEAAaN8MHowCQYDVR0TBAIwADAfBgNVHSMEGDAWgBQMbr5Vasnu4U2GGfzD
c4OdVsEGxzAdBgNVHQ4EFgQUCL4UmaLX9ahoq6nwRrScB0dcdlEwDgYDVR0PAQH/
BAQDAgWgMB0GA1UdJQQWMBQGCCsGAQUFBwMBBggrBgEFBQcDAjANBgkqhkiG9w0B
AQsFAAOCAQEAP6oVl9AgVN5w9km51xKNTJFp22vwNKNBct6HVA0Mfvd+uNk3uWJp
fujZRGrKxcq60un/lijUATj76EFDmCkImXGk8fT2mhlmdVNVzCxRrpDdsiyP39Op
HxxT1WsGxp9XTbOr9b2CUVfCvvOpe3R24RhCi/A4x4vK8wG+LNLHnbe+vrntJhvK
dJ7aBGjIrlovmmkJfESaiZD8tpia6JcT0nAi4a6RxURfU7tmS4Eo6yqjC6W0HFgM
t3M9KbM2IQs5AGEj9yNzja0KTWPhI0VZrUGpwC6iY84vfiGj+2e3aWm1NNEcYHI4
vekz0Qhonb1NNvo99OBc3gmmJ4PYunI0RQ==
-----END CERTIFICATE-----
)EOF";

std::string server_root_chain_rsa_pem = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDBjCCAe6gAwIBAgIRAIuxmJjm2/7pYd0TG4rAhSEwDQYJKoZIhvcNAQELBQAw
EzERMA8GA1UEAwwIdGVzdC1yc2EwIBcNMjUxMTEyMDYzNTMwWhgPMzAyNTAzMTUw
NjM1MzBaMBMxETAPBgNVBAMMCHRlc3QtcnNhMIIBIjANBgkqhkiG9w0BAQEFAAOC
AQ8AMIIBCgKCAQEA3gZ0gqv3BkiyPtWWnKacqhuVkorHNVcEeMro6kwv8L/4NhwZ
z/Z5GjgzZmWox++vvDiYns+5XM0dHLpCQWxPTJNLib+/bkQtpfVJ0B1RysB2q+Bc
i7ap41AynjR8vZbOPh+hjq82ZwY9g3n6mkduLs3hTkB8oSU/x8dmXJMAHuW2Lbj+
pKBku84opw9XaCkfUhTZqWS/5wAVqryQIZl3CCoRkgGFGaigEgRGDWjCdiuKh8HJ
Ff45nDk8lAwLiILcOt9l6EU6CYtIFFSy11GSJWnF9f/VWRmZv3SQsn2TzPJKy3D+
2IzXQMoBmnGuk/4Pfm55ODIy6N3eWExnMcQoEQIDAQABo1MwUTAdBgNVHQ4EFgQU
DG6+VWrJ7uFNhhn8w3ODnVbBBscwHwYDVR0jBBgwFoAUDG6+VWrJ7uFNhhn8w3OD
nVbBBscwDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAQEAopRC5d34
6z7DB0Nn2RGGQ/5ddlyL9S0aCVeZu1UAN8PC6WQjXG9Gu5qXzz48m3LmTfkuoZSM
Zl6J1UMy1UFBKDN71bgZyQ+9UlxQcvjAYsJRsEYRk/tVTRxCHlRlg9HKDDPWc7YT
6J/5M9DI8m3InVUBVsv3YZB0mYuGeyusg/ywfibxMu33MF+1/qZB4gSzFJ2j1Jx7
gOfjldHPDGwQ///akjsDBdbbazoaEqp6R8e5J5NbCG5LjBTZ/m0Aw+xPKO5+DKk4
3IsAnYrVG6tV+63We/OmQBjPRuAAjZjQZg5DdB9Sa7ogihl8bC/kGvhHY652Tmdq
ZraOJr/PyR84LA==
-----END CERTIFICATE-----
)EOF";

std::string server_root_chain_rsa_der_b64 = R"EOF(
MIIDBjCCAe6gAwIBAgIRAIuxmJjm2/7pYd0TG4rAhSEwDQYJKoZIhvcNAQELBQAwEzERMA8GA1UE
AwwIdGVzdC1yc2EwIBcNMjUxMTEyMDYzNTMwWhgPMzAyNTAzMTUwNjM1MzBaMBMxETAPBgNVBAMM
CHRlc3QtcnNhMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA3gZ0gqv3BkiyPtWWnKac
qhuVkorHNVcEeMro6kwv8L/4NhwZz/Z5GjgzZmWox++vvDiYns+5XM0dHLpCQWxPTJNLib+/bkQt
pfVJ0B1RysB2q+Bci7ap41AynjR8vZbOPh+hjq82ZwY9g3n6mkduLs3hTkB8oSU/x8dmXJMAHuW2
Lbj+pKBku84opw9XaCkfUhTZqWS/5wAVqryQIZl3CCoRkgGFGaigEgRGDWjCdiuKh8HJFf45nDk8
lAwLiILcOt9l6EU6CYtIFFSy11GSJWnF9f/VWRmZv3SQsn2TzPJKy3D+2IzXQMoBmnGuk/4Pfm55
ODIy6N3eWExnMcQoEQIDAQABo1MwUTAdBgNVHQ4EFgQUDG6+VWrJ7uFNhhn8w3ODnVbBBscwHwYD
VR0jBBgwFoAUDG6+VWrJ7uFNhhn8w3ODnVbBBscwDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0B
AQsFAAOCAQEAopRC5d346z7DB0Nn2RGGQ/5ddlyL9S0aCVeZu1UAN8PC6WQjXG9Gu5qXzz48m3Lm
TfkuoZSMZl6J1UMy1UFBKDN71bgZyQ+9UlxQcvjAYsJRsEYRk/tVTRxCHlRlg9HKDDPWc7YT6J/5
M9DI8m3InVUBVsv3YZB0mYuGeyusg/ywfibxMu33MF+1/qZB4gSzFJ2j1Jx7gOfjldHPDGwQ///a
kjsDBdbbazoaEqp6R8e5J5NbCG5LjBTZ/m0Aw+xPKO5+DKk43IsAnYrVG6tV+63We/OmQBjPRuAA
jZjQZg5DdB9Sa7ogihl8bC/kGvhHY652TmdqZraOJr/PyR84LA==
)EOF";

std::string server_root_cert_rsa_der_b64 = R"EOF(
MIIDdTCCAl2gAwIBAgIQYy0lLc2af47/u52i06RCqDANBgkqhkiG9w0BAQsFADATMREwDwYDVQQD
DAh0ZXN0LXJzYTAgFw0yNTExMTIwNjM1MzBaGA8zMDI1MDMxNTA2MzUzMFowWjELMAkGA1UEBhMC
WFgxFTATBgNVBAcMDERlZmF1bHQgQ2l0eTEcMBoGA1UECgwTRGVmYXVsdCBDb21wYW55IEx0ZDEW
MBQGA1UEAwwNdGVzdC1yc2EudGVzdDCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBANfS
3JH1Tc1GQaCORikR55aO/0yQvL6SZxl+S+Mh62aEvJl7UknyYsbzjDQhrhPZGjYoaIYE95DqKkP1
EPRQv4UegxROukrrhJi8mrEgB2FK7GZoZCgHEncyUDbwJmsD1gp33ykq6OsU6UtbEhfObo9iRSe2
nO0FT1TE+BJAvmMsMHtuCpGmuxNUxDwZnKd3z9uxzVH3tI+lKSWSYQQ9UV5Cu9r6Qj3i+vY6lfR5
O1uQauxpVekj0o7dhYNT01XKxbZYrW91/nGyS51Yvpn+MlgIBx8dvwK3GJ2JXX5igGQsrJ18sDco
uyY0IR/5STvp/rF1VAIbJkPSiE0gBnttCKECAwEAAaN8MHowCQYDVR0TBAIwADAfBgNVHSMEGDAW
gBQMbr5Vasnu4U2GGfzDc4OdVsEGxzAdBgNVHQ4EFgQUCL4UmaLX9ahoq6nwRrScB0dcdlEwDgYD
VR0PAQH/BAQDAgWgMB0GA1UdJQQWMBQGCCsGAQUFBwMBBggrBgEFBQcDAjANBgkqhkiG9w0BAQsF
AAOCAQEAP6oVl9AgVN5w9km51xKNTJFp22vwNKNBct6HVA0Mfvd+uNk3uWJpfujZRGrKxcq60un/
lijUATj76EFDmCkImXGk8fT2mhlmdVNVzCxRrpDdsiyP39OpHxxT1WsGxp9XTbOr9b2CUVfCvvOp
e3R24RhCi/A4x4vK8wG+LNLHnbe+vrntJhvKdJ7aBGjIrlovmmkJfESaiZD8tpia6JcT0nAi4a6R
xURfU7tmS4Eo6yqjC6W0HFgMt3M9KbM2IQs5AGEj9yNzja0KTWPhI0VZrUGpwC6iY84vfiGj+2e3
aWm1NNEcYHI4vekz0Qhonb1NNvo99OBc3gmmJ4PYunI0RQ==
)EOF";

std::string server_root_private_key_rsa_pem = R"EOF(
-----BEGIN RSA PRIVATE KEY-----
MIIEpQIBAAKCAQEA19LckfVNzUZBoI5GKRHnlo7/TJC8vpJnGX5L4yHrZoS8mXtS
SfJixvOMNCGuE9kaNihohgT3kOoqQ/UQ9FC/hR6DFE66SuuEmLyasSAHYUrsZmhk
KAcSdzJQNvAmawPWCnffKSro6xTpS1sSF85uj2JFJ7ac7QVPVMT4EkC+Yywwe24K
kaa7E1TEPBmcp3fP27HNUfe0j6UpJZJhBD1RXkK72vpCPeL69jqV9Hk7W5Bq7GlV
6SPSjt2Fg1PTVcrFtlitb3X+cbJLnVi+mf4yWAgHHx2/ArcYnYldfmKAZCysnXyw
Nyi7JjQhH/lJO+n+sXVUAhsmQ9KITSAGe20IoQIDAQABAoIBAQDDkUVh1YDss+8g
CAw3hwvt+CILRVRi89MM3M7zQHdI7JriEUedzlAHrAWt758ISUBhNviQCMw0upwV
/GmimdESVXW93yEcZpI+vRkHy9gkJ3pTyeCIDPCEpkjdn3uQmWdJERpc+Y8aBlbo
e9vHSr4Qm6HCvM0yUg9y5xFLqPNcdLJoNProONoIlIJFkuWkgtU1x1Tc72ZBOO/7
x2QlUpQxOb1Yy0lM0imT81ZdWIDQ+ahMrbcnkta8STdukgcBAKwY8TcsPk4ksWpl
RQF7Owr2pcoQYu1YuShZK5JrV5Z2UzowJqK9lpuL3PxB1FzwckxFR2iiesjM0Y3t
aQzvbZOJAoGBAO525V58lUxwmcE0/3rFq1MBRxFsCb9PUp351+oea6lRfEjbGHZ0
TDRUpc70T9/TqGXTwq+zzRXKatU85xrPahYDMCCZdhD2gQ+hG/XUPIFg3k9Sl71Q
PimQX4pj3bT9mCRXYMnUcrka0UTo5TKjDs7/iKzd33q04FVT/D8DExUjAoGBAOex
wJY65onvGRP4AC0cHyBmzKncV1n5aeCDNsvZ/LBRnl+tI18I3WtUD4498J5Mal9W
M0JOBbpUzIsvobGJeWZMKw77EOkUda9QHoK/RVvhcSc0ul3UIy3bqtbkpH8x8mkI
Mapg6XB3p0fIaNFRwenWGhSMs/p/xUcOqeCgMrFrAoGBAK5/WIbU2ZePCiQkuRLJ
FP5vf+bSzSsuS4GIcyRB0HydQTo7AWKk5aKhkpenlc9vHdotpFhYVSmt7pFNuLuY
rcOf35brVUdCatzaAxDXltMB5IvdH2aMEoDtwcwOGg24SJ3HRu9KRA0J/7rSRe3Q
/iQhP41n6oHrXAF0rLAbetpLAoGAbwJTa6c83cmZEmA4dWSL5VJbrObNU2aUu+Ui
U/lVgIUahGzIDbiJnergqR40MyQZsyPx20rWo7gNaIKhzhY3/DNyTfMs3v/WgvC4
LZzG9obFWCuDP4FseYRODjfuOKMNqhUq9p8IOcKae2IiFmg9qHPUexQPB25AbxMv
gVg/8ZECgYEAtUMgGKzDVg3sPYTym/Wx8SgWIZmNGRJ7HYbDdcidCltJuHN5T6M6
W2qLvcOkCYTg8kKzecK84gz4tXHto0r9Bin6Z0jkZY0cxzpNyeaHlMd21874PRrG
y6k6ILU0COw3F6lZEuSFqcH98vCCOmGIN2LKOOh7e18g+typb5kUx7s=
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
  // Not After : Mar 15 06:35:30 3025 GMT
  SystemTime a(std::chrono::seconds(33298929330));
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
  EXPECT_EQ(credentials.certificateSerial(), "326164854566604267642234107702204417305661869882");
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
  // Not After : Mar 15 06:35:30 3025 GMT
  SystemTime b(std::chrono::seconds(33298929330));
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
  EXPECT_EQ(time, SystemTime(std::chrono::seconds(33298929330)));
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
                              "MREwDwYDVQQDDAh0ZXN0LXJzYTAgFw0yNTExMTIwNjM1MzBaGA8zMDI1MDMxNTA2"
                              "MzUzMFowWjELMAkGA1UEBhMCWFgxFTATBgNVBAcMDERlZmF1bHQgQ2l0eTEcMBoG"
                              "A1UECgwTRGVmYXVsdCBDb21wYW55IEx0ZDEWMBQGA1UEAwwNdGVzdC1yc2EudGVz"
                              "dDCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBANfS3JH1Tc1GQaCORikR"
                              "55aO/0yQvL6SZxl+S+Mh62aEvJl7UknyYsbzjDQhrhPZGjYoaIYE95DqKkP1EPRQ"
                              "v4UegxROukrrhJi8mrEgB2FK7GZoZCgHEncyUDbwJmsD1gp33ykq6OsU6UtbEhfO"
                              "bo9iRSe2nO0FT1TE+BJAvmMsMHtuCpGmuxNUxDwZnKd3z9uxzVH3tI+lKSWSYQQ9"
                              "UV5Cu9r6Qj3i+vY6lfR5O1uQauxpVekj0o7dhYNT01XKxbZYrW91/nGyS51Yvpn+"
                              "MlgIBx8dvwK3GJ2JXX5igGQsrJ18sDcouyY0IR/5STvp/rF1VAIbJkPSiE0gBntt"
                              "CKECAwEAAaN8MHowCQYDVR0TBAIwADAfBgNVHSMEGDAWgBQMbr5Vasnu4U2GGfzD"
                              "c4OdVsEGxzAdBgNVHQ4EFgQUCL4UmaLX9ahoq6nwRrScB0dcdlEwDgYDVR0PAQH/"
                              "BAQDAgWgMB0GA1UdJQQWMBQGCCsGAQUFBwMBBggrBgEFBQcDAjANBgkqhkiG9w0B"
                              "AQsFAAOCAQEAP6oVl9AgVN5w9km51xKNTJFp22vwNKNBct6HVA0Mfvd+uNk3uWJp"
                              "fujZRGrKxcq60un/lijUATj76EFDmCkImXGk8fT2mhlmdVNVzCxRrpDdsiyP39Op"
                              "HxxT1WsGxp9XTbOr9b2CUVfCvvOpe3R24RhCi/A4x4vK8wG+LNLHnbe+vrntJhvK"
                              "dJ7aBGjIrlovmmkJfESaiZD8tpia6JcT0nAi4a6RxURfU7tmS4Eo6yqjC6W0HFgM"
                              "t3M9KbM2IQs5AGEj9yNzja0KTWPhI0VZrUGpwC6iY84vfiGj+2e3aWm1NNEcYHI4"
                              "vekz0Qhonb1NNvo99OBc3gmmJ4PYunI0RQ==";

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
