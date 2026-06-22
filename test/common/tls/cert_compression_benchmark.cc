#include <cstdint>
#include <vector>

#include "source/common/common/assert.h"
#include "source/common/common/macros.h"
#include "source/common/tls/cert_compression.h"

#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "openssl/pem.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace {

#ifndef ENVOY_SSL_OPENSSL

using CompressFn = int (*)(SSL*, CBB*, const uint8_t*, size_t);
using DecompressFn = int (*)(SSL*, CRYPTO_BUFFER**, size_t, const uint8_t*, size_t);
using ChainFn = const std::vector<std::vector<uint8_t>>& (*)();

// Sample three-certificate chains spanning a range of key sizes, embedded so the benchmark runs
// without runfiles. The RSA 2048 chain is test/common/tls/test_data/test_long_cert_chain.pem.
constexpr absl::string_view CertChainRsa2048Pem = R"EOF(
-----BEGIN CERTIFICATE-----
MIID7TCCAtWgAwIBAgIUOrNwiIu6GAcd0b6vdOeMYT5JBWIwDQYJKoZIhvcNAQEL
BQAwdjELMAkGA1UEBhMCVVMxEzARBgNVBAgMCkNhbGlmb3JuaWExFjAUBgNVBAcM
DVNhbiBGcmFuY2lzY28xDTALBgNVBAoMBEx5ZnQxGTAXBgNVBAsMEEx5ZnQgRW5n
aW5lZXJpbmcxEDAOBgNVBAMMB1Rlc3QgQ0EwHhcNMjQwODIxMTkxNDAyWhcNMjYw
ODIxMTkxNDAyWjCBhTELMAkGA1UEBhMCVVMxEzARBgNVBAgMCkNhbGlmb3JuaWEx
FjAUBgNVBAcMDVNhbiBGcmFuY2lzY28xDTALBgNVBAoMBEx5ZnQxGTAXBgNVBAsM
EEx5ZnQgRW5naW5lZXJpbmcxHzAdBgNVBAMMFlRlc3QgSW50ZXJtZWRpYXRlIENB
IDIwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDySm5jDEi+iTEMxCHq
2XsTMRLvjxk9mIVOstRQ4qmWIv9upCyGF7fx25i7JeLfFS1U0Q/6U4Ae064pUjfW
KTD2zmhjca12L1zQd5uJ97gUVVlQGGrAZpbE+uNv3BVdClXE60lVbQPSVxWO32L5
u3T+sO2thRu8sbhrCdg+fH7d37X689szM7zUWboPCKvBfT+Rc+Pb4aPKfz5CX/DK
1Hi/N0C0qhulpxM9DfRw9hnVGSMyc3ViDRVs7gxKmoQVKlg4ZsEvqaqCkubXMzMO
t407r6rduO2dwS4hpjpX3OwopTSDz7unRaFmz+WO2rX9Pn4b223GxOI6Wdx3MuOq
4kX5AgMBAAGjYzBhMA8GA1UdEwEB/wQFMAMBAf8wDgYDVR0PAQH/BAQDAgEGMB0G
A1UdDgQWBBRVaoWCA7AEY4z8XN5YpfTwhHh2dDAfBgNVHSMEGDAWgBQPoM8lvVgX
fggedphlyUOvH0+FVjANBgkqhkiG9w0BAQsFAAOCAQEAQTsVfW4824chRNx0Nu4Z
dyoVNXKpuk0vOJSRQbG9b7owN0lh4bV/RSj72vgrxBr2ebRqKs7Y3XRCtueHtwlL
ddINNo/pcrZthwICIHTDsB3166HogVoTO6RABJD4BLBdnGTtU25MB6RRP0vnWjcm
UdSa4fx39yXVJvbQphuXU2NDkCRXMML9So9BiNcmmM0+kxG/KMPaZi0GrOAhdNrj
XVmDI7JO6dSR0O0R/IoDVRxvB8zEAVzhEqX803+K6QMkCBcDEPIGeQ3NYLChQ6eN
a64s9X/97YDckCC0igS7MguOuTgS6y8RAcTh6E79gPztra7+VJg9jpFJ/HgTN7xH
pQ==
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIID/TCCAuWgAwIBAgIURwiSMlGPZRbFFc6M+UTTR9D7pg8wDQYJKoZIhvcNAQEL
BQAwgYUxCzAJBgNVBAYTAlVTMRMwEQYDVQQIDApDYWxpZm9ybmlhMRYwFAYDVQQH
DA1TYW4gRnJhbmNpc2NvMQ0wCwYDVQQKDARMeWZ0MRkwFwYDVQQLDBBMeWZ0IEVu
Z2luZWVyaW5nMR8wHQYDVQQDDBZUZXN0IEludGVybWVkaWF0ZSBDQSAyMB4XDTI0
MDgyMTE5MTQwM1oXDTI2MDgyMTE5MTQwM1owgYUxCzAJBgNVBAYTAlVTMRMwEQYD
VQQIDApDYWxpZm9ybmlhMRYwFAYDVQQHDA1TYW4gRnJhbmNpc2NvMQ0wCwYDVQQK
DARMeWZ0MRkwFwYDVQQLDBBMeWZ0IEVuZ2luZWVyaW5nMR8wHQYDVQQDDBZUZXN0
IEludGVybWVkaWF0ZSBDQSAyMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKC
AQEAzQ4Pf5899qGHfzxGLhaiThO38Y85mUF0Dc6yO/3SDkfXKKTMq7n2fIsp+IWs
nOLp2ruruwxqJbITMer27ErTuB/5nATsk0Q1YjxSzWR4NvgQeDSSvZVHiiEIKDm2
2j/ljyfHT9F0wtzOHJym6PN5Z0xkMpi/2X2/8CwUQCk95k7hqriofXjit0So9Opb
1DeFc+GHRSnqRSbRMTKifJJYt4iVYpPX29FeAPj8QP/E/M2ZojAkn4vIdJA2Q2wr
k9V7L11jB5wKS8aiCCFcj6HH8aJTpeVY3IbPBvuuTgVDZYxE/6K4wQKWSZpjuLYw
iaYJmxiWJfmBacy/n7pof9lYMwIDAQABo2MwYTAPBgNVHRMBAf8EBTADAQH/MA4G
A1UdDwEB/wQEAwIBBjAdBgNVHQ4EFgQUaURx1UZUH9sqf+0sJCUYy0xTMVswHwYD
VR0jBBgwFoAUVWqFggOwBGOM/FzeWKX08IR4dnQwDQYJKoZIhvcNAQELBQADggEB
ACM44Rh8uTuZG+Dv2nbXA2ugYBdEhDrREMAaM2fEsXE5RqvBc1JhdBmUi7/1Copn
rEUXlYYbzhmTwb9TPrHl1gW58ztb2+hq++nOo0/UOhF9SH/G4K3ydpzJD2j4zzjS
003zx0riGx9+yo5gZutPaoohOe1kRoFYDfHmfJb8RunsOIuhonIohCLbs/Ud6CZJ
5+zXHPvU3Y5Z6i33+Ndv21h61QWKKlh+Kc1PU/B3sv+DlJn/jIN+Xsj0EnuERS+4
6chMUNySTTnUp2B9iypZCFKHQ1xlusEnEWtcZfJhGBpQG/zH6aSIfT7rBQ3rrkne
Q70XFUIbUs3y5+1C7suGRao=
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIID/TCCAuWgAwIBAgIUFDnIux7vwmS1+LGO+PzDGUXxu7swDQYJKoZIhvcNAQEL
BQAwgYUxCzAJBgNVBAYTAlVTMRMwEQYDVQQIDApDYWxpZm9ybmlhMRYwFAYDVQQH
DA1TYW4gRnJhbmNpc2NvMQ0wCwYDVQQKDARMeWZ0MRkwFwYDVQQLDBBMeWZ0IEVu
Z2luZWVyaW5nMR8wHQYDVQQDDBZUZXN0IEludGVybWVkaWF0ZSBDQSAyMB4XDTI0
MDgyMTE5MTQwM1oXDTI2MDgyMTE5MTQwM1owgYUxCzAJBgNVBAYTAlVTMRMwEQYD
VQQIDApDYWxpZm9ybmlhMRYwFAYDVQQHDA1TYW4gRnJhbmNpc2NvMQ0wCwYDVQQK
DARMeWZ0MRkwFwYDVQQLDBBMeWZ0IEVuZ2luZWVyaW5nMR8wHQYDVQQDDBZUZXN0
IEludGVybWVkaWF0ZSBDQSAzMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKC
AQEAg9SQkje8dgpCHOY6rN2QhBCzPzWN4fXBQnfoUZfQ3GcBHVrJkxguXTieG1Gi
4GtULYVwCpAnkCehIPOoi+drr70Y+cbr8ZwGczPK9ehljBn1J+RNWtTaG5UaDsRz
2lCdVmLv5ocWA9xowWRMxL/awlAFaYFTirHUnxZZl2EXgF8X3NhkOLOv1bbnmGsj
jZdGKPcLZLxB7P6jWothgpBlTcvszYQj/0rPJ+tbA2RQlotZbt5j0mnn3RGKv10o
ePRBYAKvHSATiAdYYBdjRNP1Bc0Dnonaa6FNpAiRBZ/bN491PaqYXPRcOmOWAjsv
XTxL0SpdRAT4NnCvZgPrKQfYLwIDAQABo2MwYTAPBgNVHRMBAf8EBTADAQH/MA4G
A1UdDwEB/wQEAwIBBjAdBgNVHQ4EFgQUeD+JQi5GX8nCsG9GbVCORWS0xpIwHwYD
VR0jBBgwFoAUaURx1UZUH9sqf+0sJCUYy0xTMVswDQYJKoZIhvcNAQELBQADggEB
AL9t7cCLwwydUVexYGMiES9g3XmaGv3nHLDWXKt9z+aWgBub2sIU5Qr4nszQcU8e
NPwwl+VS6N0bcY3Rk4rHQULAtQiBzOS6cKEQt3G3CYPRUpIFjl40HVqQ0vKy/LMy
o+1+4DaYswdqJQjKk3cKMDLA/j3rurGFcnj+bEOWhmGhK2chNGns8sN/8fhmhgf6
NN8mjK1LZrEbL972C0MShHQvnYRMWJFLWuhylm0M3e8LkShvdTZroMJcX6U15Vsc
+QeJPlxxyOB0nESvjaBTFK6wBIYbfipXdJ7P72LvGEVbe3b+ARJK7ui68CtQ2q0I
9EIp/ZPcFjNTrjpQNSf1zjY=
-----END CERTIFICATE-----
)EOF";
constexpr absl::string_view CertChainRsa4096Pem = R"EOF(
-----BEGIN CERTIFICATE-----
MIIGXjCCBEagAwIBAgIUExjly36FP8IYBlzVPYc6OXezkd0wDQYJKoZIhvcNAQEL
BQAwgZAxCzAJBgNVBAYTAlVTMRMwEQYDVQQIDApDYWxpZm9ybmlhMRYwFAYDVQQH
DA1TYW4gRnJhbmNpc2NvMQ4wDAYDVQQKDAVFbnZveTESMBAGA1UECwwJQmVuY2ht
YXJrMTAwLgYDVQQDDCdFbnZveSBCZW5jaG1hcmsgcnNhNDA5NiBJbnRlcm1lZGlh
dGUgQ0EwHhcNMjYwNjE1MjAwOTI0WhcNMzYwNjEyMjAwOTI0WjCBgjELMAkGA1UE
BhMCVVMxEzARBgNVBAgMCkNhbGlmb3JuaWExFjAUBgNVBAcMDVNhbiBGcmFuY2lz
Y28xDjAMBgNVBAoMBUVudm95MRIwEAYDVQQLDAlCZW5jaG1hcmsxIjAgBgNVBAMM
GWJlbmNoLXJzYTQwOTYuZXhhbXBsZS5jb20wggIiMA0GCSqGSIb3DQEBAQUAA4IC
DwAwggIKAoICAQCsc1BS9GKwsxk5GqFmQ4EhkM+g7rxQ9MXftzpyPnoQH1Hrc2pv
SMNJPtbq9cmS/K4eFMhL3OOR70ZpT2zQzygC2eeEhE2cINdw4UC5L97z6Y5WBI1s
5JfSgQL9deUjKS41trULk5vgi3HPQSelsnwnUXuxFeVssAn8QlWKAPBBXaBjvN9G
dbCCKmBKZhJQY7epPw7gf1vWTmDgko627amfTZDH78tEXXkIm9spnmLMV/dGSMWJ
+sY/ZGSLEQq0ee88emv2pE/+ei8jNVjNpBMKXRMTqjmvsvQuKqPqCeuw1Xw7NtZk
OshKNEXX5SexOPNOIjimgOyA+0Ye5X6JNR/CcWK/zN02M/FY1I7HnjkdM88rm1M8
u+ZVwd5oE2wjaXiwVOToANuCp//rJ+3WLPSy9cfNYtP3qYYAQcatFkIppMPs3ReC
Z9FR40Sj8Lb+ijYxSa99mWgBxZuVnRV27X0wv37QxbLzwtR8vIN6kZpMRSNNu7+V
bpZhqMZdPvK0rTef+HKj1neVUzdQOXm3i5RzdEmgOH5mI3EuOeNr0qjK7g+Pb98x
j59+n/XnM9G52YEokxogJipSAQe2mfqNPCjsKPrq1Fyxy+2XKjfuu6DAtgNDbG9g
LURSfYTArkEstGxMuWVrTfa7ZFc2y878KarTZL3L5BT/kbRx9YrsMPirbwIDAQAB
o4G7MIG4MAwGA1UdEwEB/wQCMAAwDgYDVR0PAQH/BAQDAgWgMBMGA1UdJQQMMAoG
CCsGAQUFBwMBMEMGA1UdEQQ8MDqCGWJlbmNoLXJzYTQwOTYuZXhhbXBsZS5jb22C
HXd3dy5iZW5jaC1yc2E0MDk2LmV4YW1wbGUuY29tMB0GA1UdDgQWBBTPcDWCg0mg
AjKdWQxSCemFSOUG0TAfBgNVHSMEGDAWgBTgA32UM3PaW2qcVIxwqAGMNkMu9jAN
BgkqhkiG9w0BAQsFAAOCAgEAkSAjI/X4xyOXwPEdcvcIQcf4cGZRCjbJAZwuHLNc
cNAVQBztWR9Mh0HvhNMkIt8tWO9m3ma32VGeqhB2e4rmJog8JmoRdtRdiO1kMjEL
dBrblTyJ5GNmfpRIddHLHjqFWS29qMAXXBL/+zDBkjAf3KA8GxyhlJe0oJccObUn
pC8i0InFXXT/J8zqhZ54z8vmXYO/vf9DSO5cup3l8bXmvMbr54mmHE0pT3Aa4vhI
t6jUIUt/P5lSM3Wey1qcS3bmS9dQodOyj5EEWymW26N+br6FTf11D3Fps2qXrRIN
qS6+11QXTAiyf7s8x6SAAtwmMB3nI4GHJ9fhwyZPTvYiYcB6uWG638+ynso2x4qv
v5Mvk7vauFM8ZHCRYHZ358SE3VXqnOJYimyPTW5sm5n3MLKjJqzAua5yh9GbzFnd
AwxQ/Ol4kOIlFTzkVosxr/j37TMG7ea1cJT19ZqWAb95golt7NGDIUYtaAhEKGiL
tUk7jKg0jNiode+vrSc1GQO+0FFBNMqYv9qgj7XMzp3LaYkpCy/Vh5Po0tDnNBql
5ALeITTu7V38twMLt2Xmv5HlQ/L1EtKPDyRgKMQtldZoBEqzhJCA/FfGHCKt69ST
iF/FMe540Yi6mgfIiPpwQyVF2riXcG2xmHrSRn9obT4iEcmdk7ry9PUAlTfW0teK
iPE=
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIGCzCCA/OgAwIBAgIUUxj/m1Yfk0uR4XKivNMvcOFRanQwDQYJKoZIhvcNAQEL
BQAwgYgxCzAJBgNVBAYTAlVTMRMwEQYDVQQIDApDYWxpZm9ybmlhMRYwFAYDVQQH
DA1TYW4gRnJhbmNpc2NvMQ4wDAYDVQQKDAVFbnZveTESMBAGA1UECwwJQmVuY2ht
YXJrMSgwJgYDVQQDDB9FbnZveSBCZW5jaG1hcmsgcnNhNDA5NiBSb290IENBMB4X
DTI2MDYxNTIwMDkyNFoXDTM2MDYxMjIwMDkyNFowgZAxCzAJBgNVBAYTAlVTMRMw
EQYDVQQIDApDYWxpZm9ybmlhMRYwFAYDVQQHDA1TYW4gRnJhbmNpc2NvMQ4wDAYD
VQQKDAVFbnZveTESMBAGA1UECwwJQmVuY2htYXJrMTAwLgYDVQQDDCdFbnZveSBC
ZW5jaG1hcmsgcnNhNDA5NiBJbnRlcm1lZGlhdGUgQ0EwggIiMA0GCSqGSIb3DQEB
AQUAA4ICDwAwggIKAoICAQDm4RbmLi5+RuPRuWjH/pWjaJocV36GQaJ2zjSFpB3p
aRz53MdE+5TtFMPqqLvXvVYrMgQ6qWHsBopyfBIQhNrU/f5Rwthckld07HFNQ7ol
1gA5KK0TCQWFcrcbywpPA0NrHQUc9xhyN3Sx1MF5kUca5X2M+l+P02GgxNg4EwNX
wxJot13x6CarxvH+9RNKrWVJgXUkxG9QB3+pIjKCAEVYzHwHsUrDifEMW3MHlAH4
wmMHUzf8vlsGDRYDeMYtVGteDAXIgsug1IFXGEpUo8tsOxNFPblACRBGDAcyU2Yx
1tBQnUZmYfxi5yN9Mhp/6NAMXkO288TXH3i6rNaJyZOYG69bZDMIuSosYViAXLJf
BW9MV74yBnZbL3R1rb7x/5ReidQRGZ+IrVdGnA+xvl8qlcW6tEYJQTeO1nRaN5/G
tSrACqWELZaTw5BtoPFGY1yMF4YPUQabXa/smVUSozUEVnIBofyQsiNEssAyyapm
rWoSw65TvhMAi8YO4j2NWTZS8z8nMoGwaUAmN5HN+LbW+O86/Y+ZizosmQEQVr+P
lyJ6vNGmY2Tk5EdOORa/hjpFT59LhqLQfoQgV6QraiISe0VlRYBq4L6GQtVPjJ2s
2SfH25ogUm03sMDKxFLQz4r6HxuIL3y0f3RBf0EcB9ciWn4DpyngPD27Fy8iyXSr
6wIDAQABo2MwYTAPBgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBBjAdBgNV
HQ4EFgQU4AN9lDNz2ltqnFSMcKgBjDZDLvYwHwYDVR0jBBgwFoAUa0UNLdjK8RKA
5IbYTOP90pT+DMEwDQYJKoZIhvcNAQELBQADggIBAIHLLirFPvaLylLe0YU8GxLW
JmScM82Wq9oB3Yghr54QYUGtu5qingHJHK0JpZlzaPQu7gF06WXqDrOorYHADhoc
j0w8jlI5/UpZc+cp8YaI/6ZGMPARO5zLSQ8KwpQ08qMgB1LJjqilSphiR9YI1tui
wmFIiyEu8MavrcDqyACVKiwiPhdyCK4nSrbyYGGm9lrJ7tJ6Lzvka4O3wk0531Vu
bQnMNQqwcYb8LfbFjeIg9VWtBMYM3EvSjbomhtCDfo8cI4BCGQe2w9bIyGPn0vof
wYxQPiuecMBbVyGdr3V4PBdpxFbgNm3Rx+L6V/ds00fiLXDJtbdZrevdarVFjF5T
UmSn5/RIZOjFdqORQG9crBRTbW31B/ZkD1lJQCJhTo8X9xUBFmR2YHyZAejnf3M0
lC5/4NG4b4yQHq2FM1q6mZhOBJeE51K/R12zEc3lgJcGu+3QnrkWC1GR+SgbIF0j
Oo9tePayEru0rfZ/AX0Rm382LkHgMiQNEpSL4TT4WPT69+RbWpyue0T/Iv6d61Lp
Oxk3/zO5dEJVWsgl3SuHJoGMfhTRKajpysPWqYc4LDG8GCV30kEsbnqe/wNuQCwo
7pn2crcDlLsKB929RPIAASHgC4eEC4EyCWBmgE3ZWRp6qk/5BSlCY/y43L/vpG36
+IodCwoPu5oEi0xAN9tt
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
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
4EDnmDxCtw==
-----END CERTIFICATE-----
)EOF";
constexpr absl::string_view CertChainEcdsaP256Pem = R"EOF(
-----BEGIN CERTIFICATE-----
MIICyjCCAnCgAwIBAgIUExjly36FP8IYBlzVPYc6OXezkd4wCgYIKoZIzj0EAwIw
gY4xCzAJBgNVBAYTAlVTMRMwEQYDVQQIDApDYWxpZm9ybmlhMRYwFAYDVQQHDA1T
YW4gRnJhbmNpc2NvMQ4wDAYDVQQKDAVFbnZveTESMBAGA1UECwwJQmVuY2htYXJr
MS4wLAYDVQQDDCVFbnZveSBCZW5jaG1hcmsgZWNkc2EgSW50ZXJtZWRpYXRlIENB
MB4XDTI2MDYxNTIwMDkyNVoXDTM2MDYxMjIwMDkyNVowgYAxCzAJBgNVBAYTAlVT
MRMwEQYDVQQIDApDYWxpZm9ybmlhMRYwFAYDVQQHDA1TYW4gRnJhbmNpc2NvMQ4w
DAYDVQQKDAVFbnZveTESMBAGA1UECwwJQmVuY2htYXJrMSAwHgYDVQQDDBdiZW5j
aC1lY2RzYS5leGFtcGxlLmNvbTBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABDAu
y68Rnj2SAllpe41UpWi0UCTy25yXdAJShU602nY5MketFr4DB6mY9b/HCBaQOtCX
ciTZHV1NUwTJeY+L+KajgbcwgbQwDAYDVR0TAQH/BAIwADAOBgNVHQ8BAf8EBAMC
BaAwEwYDVR0lBAwwCgYIKwYBBQUHAwEwPwYDVR0RBDgwNoIXYmVuY2gtZWNkc2Eu
ZXhhbXBsZS5jb22CG3d3dy5iZW5jaC1lY2RzYS5leGFtcGxlLmNvbTAdBgNVHQ4E
FgQUfLa3HWToJ+8jowmw71R4BXaE9MYwHwYDVR0jBBgwFoAU6PacJU72kZYNfqNl
xOcryn40HJEwCgYIKoZIzj0EAwIDSAAwRQIhAJBRCAYdZrif0rS1u5trOUN9ma14
QHij+tKZNENJwAbuAiBgIf+MuPQXpCy187g+7DVwd1Ah239geQHVtCaGVTd/PA==
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIICfDCCAiGgAwIBAgIUUxj/m1Yfk0uR4XKivNMvcOFRanUwCgYIKoZIzj0EAwIw
gYYxCzAJBgNVBAYTAlVTMRMwEQYDVQQIDApDYWxpZm9ybmlhMRYwFAYDVQQHDA1T
YW4gRnJhbmNpc2NvMQ4wDAYDVQQKDAVFbnZveTESMBAGA1UECwwJQmVuY2htYXJr
MSYwJAYDVQQDDB1FbnZveSBCZW5jaG1hcmsgZWNkc2EgUm9vdCBDQTAeFw0yNjA2
MTUyMDA5MjVaFw0zNjA2MTIyMDA5MjVaMIGOMQswCQYDVQQGEwJVUzETMBEGA1UE
CAwKQ2FsaWZvcm5pYTEWMBQGA1UEBwwNU2FuIEZyYW5jaXNjbzEOMAwGA1UECgwF
RW52b3kxEjAQBgNVBAsMCUJlbmNobWFyazEuMCwGA1UEAwwlRW52b3kgQmVuY2ht
YXJrIGVjZHNhIEludGVybWVkaWF0ZSBDQTBZMBMGByqGSM49AgEGCCqGSM49AwEH
A0IABCBrxGT/rFK5znrWr6n4Ayf4oQmbXGMK2/LS7Pkbq+T9FQsz4atq6SUODE6w
+WlNXVle+viCPLx306GpJtDPGfmjYzBhMA8GA1UdEwEB/wQFMAMBAf8wDgYDVR0P
AQH/BAQDAgEGMB0GA1UdDgQWBBTo9pwlTvaRlg1+o2XE5yvKfjQckTAfBgNVHSME
GDAWgBQwfFqZ3wozTh7qvQ+wWsPjbIRlWDAKBggqhkjOPQQDAgNJADBGAiEAmx9s
Xp0cyejE0Fy1ybFS5s2DsXLOhq6OJh+NNvmFSIUCIQCXOr55hp6ALuL8FcvO6YqZ
NijSG7FbenXrtQ/u2P4GYw==
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIICczCCAhmgAwIBAgIUCsRCV3HwDi6PfrNg43DV9NPhNkcwCgYIKoZIzj0EAwIw
gYYxCzAJBgNVBAYTAlVTMRMwEQYDVQQIDApDYWxpZm9ybmlhMRYwFAYDVQQHDA1T
YW4gRnJhbmNpc2NvMQ4wDAYDVQQKDAVFbnZveTESMBAGA1UECwwJQmVuY2htYXJr
MSYwJAYDVQQDDB1FbnZveSBCZW5jaG1hcmsgZWNkc2EgUm9vdCBDQTAeFw0yNjA2
MTUyMDA5MjRaFw0zNjA2MTIyMDA5MjRaMIGGMQswCQYDVQQGEwJVUzETMBEGA1UE
CAwKQ2FsaWZvcm5pYTEWMBQGA1UEBwwNU2FuIEZyYW5jaXNjbzEOMAwGA1UECgwF
RW52b3kxEjAQBgNVBAsMCUJlbmNobWFyazEmMCQGA1UEAwwdRW52b3kgQmVuY2ht
YXJrIGVjZHNhIFJvb3QgQ0EwWTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAAQ9zyqd
Al7Cuvdd1W20qHNLpRcrN3OBjpgzstFhggu03QY/M0HOPDpL0dP6qWObIweJUwYZ
qBTERb4tXvcFxIFho2MwYTAdBgNVHQ4EFgQUMHxamd8KM04e6r0PsFrD42yEZVgw
HwYDVR0jBBgwFoAUMHxamd8KM04e6r0PsFrD42yEZVgwDwYDVR0TAQH/BAUwAwEB
/zAOBgNVHQ8BAf8EBAMCAQYwCgYIKoZIzj0EAwIDSAAwRQIhAJO4ennUo9+I+Wp8
5t4cgmVA3F2D7mfNv39XB7dMVeG+AiAzqYhRcWTq5G0kymEpwxwAy1MB5ofbBVRG
RokRDlTAwg==
-----END CERTIFICATE-----
)EOF";

std::vector<std::vector<uint8_t>> loadCertChainDer(absl::string_view pem) {
  bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(pem.data(), pem.size()));
  RELEASE_ASSERT(bio != nullptr, "failed to allocate BIO for certificate chain");

  std::vector<std::vector<uint8_t>> chain;
  while (true) {
    bssl::UniquePtr<X509> cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
    if (cert == nullptr) {
      break;
    }
    uint8_t* der = nullptr;
    const int der_len = i2d_X509(cert.get(), &der);
    RELEASE_ASSERT(der_len > 0, "failed to DER encode certificate");
    bssl::UniquePtr<uint8_t> der_owner(der);
    chain.emplace_back(der, der + der_len);
  }
  // PEM_read_bio_X509 leaves a no-start-line error once the chain is exhausted.
  ERR_clear_error();
  RELEASE_ASSERT(!chain.empty(), "certificate chain contained no certificates");
  return chain;
}

const std::vector<std::vector<uint8_t>>& rsa2048Chain() {
  static const std::vector<std::vector<uint8_t>> chain = loadCertChainDer(CertChainRsa2048Pem);
  return chain;
}

const std::vector<std::vector<uint8_t>>& rsa4096Chain() {
  static const std::vector<std::vector<uint8_t>> chain = loadCertChainDer(CertChainRsa4096Pem);
  return chain;
}

const std::vector<std::vector<uint8_t>>& ecdsaP256Chain() {
  static const std::vector<std::vector<uint8_t>> chain = loadCertChainDer(CertChainEcdsaP256Pem);
  return chain;
}

// The concatenated DER approximates the certificate payload compressed during a handshake and
// omits the TLS message framing.
std::vector<uint8_t> certChainPrefixDer(ChainFn chain_fn, int num_certs) {
  const std::vector<std::vector<uint8_t>>& chain = chain_fn();
  RELEASE_ASSERT(num_certs >= 1 && num_certs <= static_cast<int>(chain.size()),
                 "requested certificate count is outside the sample chain");
  std::vector<uint8_t> der;
  for (int i = 0; i < num_certs; i++) {
    der.insert(der.end(), chain[i].begin(), chain[i].end());
  }
  return der;
}

// Compresses the cert chain prefix once per iteration, mirroring a single handshake.
void benchmarkCompress(benchmark::State& state, CompressFn compress, ChainFn chain) {
  const std::vector<uint8_t> der = certChainPrefixDer(chain, state.range(0));
  size_t compressed_len = 0;
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    bssl::ScopedCBB out;
    RELEASE_ASSERT(CBB_init(out.get(), 0) == 1, "CBB_init failed");
    RELEASE_ASSERT(compress(nullptr, out.get(), der.data(), der.size()) == CertCompression::SUCCESS,
                   "cert compression failed");
    compressed_len = CBB_len(out.get());
    benchmark::DoNotOptimize(compressed_len);
  }
  state.counters["uncompressed_bytes"] = der.size();
  state.counters["compressed_bytes"] = compressed_len;
  state.SetBytesProcessed(state.iterations() * der.size());
}

// Decompresses cert chain data that was compressed once during setup.
void benchmarkDecompress(benchmark::State& state, CompressFn compress, DecompressFn decompress,
                         ChainFn chain) {
  const std::vector<uint8_t> der = certChainPrefixDer(chain, state.range(0));
  bssl::ScopedCBB compressed;
  RELEASE_ASSERT(CBB_init(compressed.get(), 0) == 1, "CBB_init failed");
  RELEASE_ASSERT(compress(nullptr, compressed.get(), der.data(), der.size()) ==
                     CertCompression::SUCCESS,
                 "cert compression failed");
  const std::vector<uint8_t> compressed_der(CBB_data(compressed.get()),
                                            CBB_data(compressed.get()) + CBB_len(compressed.get()));
  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    CRYPTO_BUFFER* out = nullptr;
    RELEASE_ASSERT(decompress(nullptr, &out, der.size(), compressed_der.data(),
                              compressed_der.size()) == CertCompression::SUCCESS,
                   "cert decompression failed");
    benchmark::DoNotOptimize(out);
    bssl::UniquePtr<CRYPTO_BUFFER> out_owner(out);
  }
  state.counters["uncompressed_bytes"] = der.size();
  state.counters["compressed_bytes"] = compressed_der.size();
  state.SetBytesProcessed(state.iterations() * der.size());
}

BENCHMARK_CAPTURE(benchmarkCompress, brotli_rsa2048, CertCompression::compressBrotli, rsa2048Chain)
    ->DenseRange(1, 3, 1)
    ->Unit(::benchmark::kMicrosecond);
BENCHMARK_CAPTURE(benchmarkCompress, brotli_rsa4096, CertCompression::compressBrotli, rsa4096Chain)
    ->DenseRange(1, 3, 1)
    ->Unit(::benchmark::kMicrosecond);
BENCHMARK_CAPTURE(benchmarkCompress, brotli_ecdsaP256, CertCompression::compressBrotli,
                  ecdsaP256Chain)
    ->DenseRange(1, 3, 1)
    ->Unit(::benchmark::kMicrosecond);
BENCHMARK_CAPTURE(benchmarkCompress, zlib_rsa2048, CertCompression::compressZlib, rsa2048Chain)
    ->DenseRange(1, 3, 1)
    ->Unit(::benchmark::kMicrosecond);
BENCHMARK_CAPTURE(benchmarkCompress, zlib_rsa4096, CertCompression::compressZlib, rsa4096Chain)
    ->DenseRange(1, 3, 1)
    ->Unit(::benchmark::kMicrosecond);
BENCHMARK_CAPTURE(benchmarkCompress, zlib_ecdsaP256, CertCompression::compressZlib, ecdsaP256Chain)
    ->DenseRange(1, 3, 1)
    ->Unit(::benchmark::kMicrosecond);
BENCHMARK_CAPTURE(benchmarkDecompress, brotli_rsa2048, CertCompression::compressBrotli,
                  CertCompression::decompressBrotli, rsa2048Chain)
    ->DenseRange(1, 3, 1)
    ->Unit(::benchmark::kMicrosecond);
BENCHMARK_CAPTURE(benchmarkDecompress, brotli_rsa4096, CertCompression::compressBrotli,
                  CertCompression::decompressBrotli, rsa4096Chain)
    ->DenseRange(1, 3, 1)
    ->Unit(::benchmark::kMicrosecond);
BENCHMARK_CAPTURE(benchmarkDecompress, brotli_ecdsaP256, CertCompression::compressBrotli,
                  CertCompression::decompressBrotli, ecdsaP256Chain)
    ->DenseRange(1, 3, 1)
    ->Unit(::benchmark::kMicrosecond);
BENCHMARK_CAPTURE(benchmarkDecompress, zlib_rsa2048, CertCompression::compressZlib,
                  CertCompression::decompressZlib, rsa2048Chain)
    ->DenseRange(1, 3, 1)
    ->Unit(::benchmark::kMicrosecond);
BENCHMARK_CAPTURE(benchmarkDecompress, zlib_rsa4096, CertCompression::compressZlib,
                  CertCompression::decompressZlib, rsa4096Chain)
    ->DenseRange(1, 3, 1)
    ->Unit(::benchmark::kMicrosecond);
BENCHMARK_CAPTURE(benchmarkDecompress, zlib_ecdsaP256, CertCompression::compressZlib,
                  CertCompression::decompressZlib, ecdsaP256Chain)
    ->DenseRange(1, 3, 1)
    ->Unit(::benchmark::kMicrosecond);

#endif // ENVOY_SSL_OPENSSL

} // namespace
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
