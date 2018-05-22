#include "envoy/api/v2/auth/cert.pb.h"

#include "common/secret/secret_impl.h"
#include "common/secret/secret_manager_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Secret {
namespace {

std::string kExpectedCertificateChain =
    R"EOF(-----BEGIN CERTIFICATE-----
MIIDXDCCAsWgAwIBAgIJAPF6WtmgmqziMA0GCSqGSIb3DQEBCwUAMHYxCzAJBgNV
BAYTAlVTMRMwEQYDVQQIDApDYWxpZm9ybmlhMRYwFAYDVQQHDA1TYW4gRnJhbmNp
c2NvMQ0wCwYDVQQKDARMeWZ0MRkwFwYDVQQLDBBMeWZ0IEVuZ2luZWVyaW5nMRAw
DgYDVQQDDAdUZXN0IENBMB4XDTE4MDQwNjIwNTgwNVoXDTIwMDQwNTIwNTgwNVow
gaYxCzAJBgNVBAYTAlVTMRMwEQYDVQQIDApDYWxpZm9ybmlhMRYwFAYDVQQHDA1T
YW4gRnJhbmNpc2NvMQ0wCwYDVQQKDARMeWZ0MRkwFwYDVQQLDBBMeWZ0IEVuZ2lu
ZWVyaW5nMRowGAYDVQQDDBFUZXN0IEJhY2tlbmQgVGVhbTEkMCIGCSqGSIb3DQEJ
ARYVYmFja2VuZC10ZWFtQGx5ZnQuY29tMIGfMA0GCSqGSIb3DQEBAQUAA4GNADCB
iQKBgQCqtS9bbVbo4ZpO1uSBCDortIibXKByL1fgl7s2uJc77+vzJnqC9uLFYygU
1Z198X6jaAjc/vUkLFVXZhOU8607Zex8X+CdZBjQqsN90X2Ste1wqJ7G5SAGhptd
/nOfb1IdGa6YtwPTlVitnMTfRgG4fh+3DA51UulCGTfJXCaC3wIDAQABo4HAMIG9
MAwGA1UdEwEB/wQCMAAwCwYDVR0PBAQDAgXgMB0GA1UdJQQWMBQGCCsGAQUFBwMC
BggrBgEFBQcDATBBBgNVHREEOjA4hh5zcGlmZmU6Ly9seWZ0LmNvbS9iYWNrZW5k
LXRlYW2CCGx5ZnQuY29tggx3d3cubHlmdC5jb20wHQYDVR0OBBYEFLEoDrcF8PTj
2t6gbcjoXQqBlAeeMB8GA1UdIwQYMBaAFJzPb3sKqs4x95aPTM2blA7vWB+0MA0G
CSqGSIb3DQEBCwUAA4GBAJr60+EyNfrdkzUzzFvRA/E7dntBhBIOWKDvB2p8Hcym
ILbC6sJdUotEUg2kxbweY20OjrpyT3jSe9o4E8SDkebybbxrQlXzNCq0XL42R5bI
TSufsKqBICwwJ47yp+NV7RsPhe8AO/GehXhTlJBBwHSX6gfvjapkUG43AmdbY19L
-----END CERTIFICATE-----
)EOF";

std::string kExpectedPrivateKey =
    R"EOF(-----BEGIN RSA PRIVATE KEY-----
MIICXAIBAAKBgQCqtS9bbVbo4ZpO1uSBCDortIibXKByL1fgl7s2uJc77+vzJnqC
9uLFYygU1Z198X6jaAjc/vUkLFVXZhOU8607Zex8X+CdZBjQqsN90X2Ste1wqJ7G
5SAGhptd/nOfb1IdGa6YtwPTlVitnMTfRgG4fh+3DA51UulCGTfJXCaC3wIDAQAB
AoGBAIDbb7n12QrFcTNd5vK3gSGIjy2nR72pmw3/uuPdhttJibPrMcM2FYumA5Vm
ghGVf2BdoYMgOW9qv6jPdqyTHAlkjKRU2rnqqUgiRWscHsTok6qubSeE/NtKYhM3
O2NH0Yv6Avq7aVMaZ9XpmXp/0ovpDggFBzfUZW4d3SNhFGeRAkEA1F7WwgAOh6m4
0OaZgOkTE89shSXRJHeXUegYtR1Ur3+U1Ib/Ana8JcvtmkT17iR0AUjKqDsF/4LE
OrV6Gv6+DQJBAM3HL88Ac6zxfCmmrEYDfmz9PhNj0NhDgKt0wq/mSOlCIl6EUdBu
1jFNQ2b3qDdUbNKRBBMvWJ7agl1Wk11j9ZsCQD5fXFO+EIZnopA4Kf1idufqk8TH
RpWfSiIUOK1439Zrchq5S0w98yRmsHIOruwyaJ+38U1XiHtyvI9BnYswJkECQG2d
wLL1W6lxziFl3vlA3TTzxgCQOG0rsDwla5xGAOr4xtQwimCM2l7S+Ke+H4ax23Jj
u5b4rq2YWr+b4c5q9CcCQH94/pVWoUVa2z/JlBq/1/MbcnucfWcpj8HKxpgoTD3b
t+uGq75okt7lfCeocT3Brt50w43WwPbmvQyeaC0qawU=
-----END RSA PRIVATE KEY-----
)EOF";

class SecretManagerImplTest : public testing::Test {};

TEST_F(SecretManagerImplTest, WeightedClusterFallthroughConfig) {
  envoy::api::v2::auth::Secret secret_config;

  secret_config.set_name("abc.com");
  auto tls_certificate = secret_config.mutable_tls_certificate();
  tls_certificate->mutable_certificate_chain()->set_filename(
      "test/config/integration/certs/servercert.pem");
  tls_certificate->mutable_private_key()->set_filename(
      "test/config/integration/certs/serverkey.pem");

  SecretSharedPtr secret(new SecretImpl(secret_config));

  SecretManagerImpl secret_manager;
  secret_manager.addOrUpdateStaticSecret(secret);

  ASSERT_EQ(secret_manager.staticSecret("undefined"), nullptr);

  ASSERT_NE(secret_manager.staticSecret("abc.com"), nullptr);

  EXPECT_EQ(kExpectedCertificateChain, secret_manager.staticSecret("abc.com")->certificateChain());
  EXPECT_EQ(kExpectedPrivateKey, secret_manager.staticSecret("abc.com")->privateKey());
}

} // namespace
} // namespace Secret
} // namespace Envoy
