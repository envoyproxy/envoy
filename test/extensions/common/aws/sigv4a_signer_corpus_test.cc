#include <filesystem>
#include <fstream>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/crypto/utility.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/extensions/common/aws/sigv4a_key_derivation.h"
#include "source/extensions/common/aws/sigv4a_signer_impl.h"
#include "source/extensions/common/aws/utility.h"

#include "test/extensions/common/aws/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

std::vector<std::string> directoryListing() {
  std::vector<std::string> directories;
  for (auto const& entry : std::filesystem::directory_iterator(
           TestEnvironment::runfilesDirectory() +
           "/external/com_github_awslabs_aws_c_auth/tests/aws-signing-test-suite/v4a")) {
    directories.push_back(entry.path().string());
  }
  return directories;
}

class SigV4ASignerCorpusTest : public ::testing::TestWithParam<std::string> {
public:
  SigV4ASignerCorpusTest() = default;

  void addMethod(const std::string& method) { message_.headers().setMethod(method); }

  void addPath(const std::string& path) { message_.headers().setPath(path); }

  void addHeader(const std::string& key, const std::string& value) {
    message_.headers().addCopy(Http::LowerCaseString(key), value);
  }

  std::string readStringFile(std::string path) {
    std::ifstream infile(rundir_ + "/" + path);
    std::stringstream buffer;
    buffer << infile.rdbuf();
    return buffer.str();
  }

  void loadContext() {

    json_context_ = Json::Factory::loadFromString(readStringFile("context.json"));

    normalize_ = json_context_->getBoolean("normalize");
    expiration_ = json_context_->getInteger("expiration_in_seconds");
    region_ = json_context_->getString("region");
    service_ = json_context_->getString("service");
    timestamp_ = json_context_->getString("timestamp");
    akid_ = json_context_->getObject("credentials")->getString("access_key_id");
    skid_ = json_context_->getObject("credentials")->getString("secret_access_key");

    token_ = "";

    try {
      omit_session_token_ = json_context_->getBoolean("omit_session_token");
    } catch (EnvoyException& e) {
      omit_session_token_ = false;
    }
    if (!omit_session_token_) {
      try {
        token_ = json_context_->getObject("credentials")->getString("token");
      } catch (EnvoyException& e) {
      }
    }
  }

  void setTime() {
    past_time_ = TestUtility::parseTime(timestamp_, "%E4Y-%m-%dT%H:%M:%S%z");
    time_system_.setSystemTime(absl::ToChronoTime(past_time_));
    ON_CALL(context_, timeSystem()).WillByDefault(ReturnRef(time_system_));
  }

  void setupPathAndHeaders() {

    auto full_request_ = readStringFile("request.txt");
    std::vector<std::string> split_full_request_ = absl::StrSplit(full_request_, "\n\n");
    auto request_and_headers = split_full_request_[0];
    if (split_full_request_.size() > 1) {
      body_ = split_full_request_[1];
    }

    std::vector<std::string> split_request_ = absl::StrSplit(request_and_headers, '\n');
    // path is contained between method and HTTP/1.1 string
    path_ = split_request_[0];
    path_.erase(0, path_.find(' ') + 1);
    path_.erase(path_.find_last_of(' '), path_.length());
    addPath(path_);

    std::vector<std::string> http_request_ = absl::StrSplit(split_request_[0], ' ');
    EXPECT_EQ(static_cast<std::string>(http_request_.back()), "HTTP/1.1");

    method_ = http_request_[0];
    addMethod(method_);

    // remove http request line leaving only headers
    split_request_.erase(split_request_.begin());
    // add all headers
    for (auto& it : split_request_) {
      std::vector<std::string> header_ = absl::StrSplit(it, ':');
      if (header_.size() > 1) {
        addHeader(header_[0], header_[1]);
      }
    }
  }

  void setDate() {
    auto long_date_formatter_ = DateFormatter(std::string(SignatureConstants::LongDateFormat));
    long_date_ = long_date_formatter_.now(time_system_);
    auto short_date_formatter_ = DateFormatter(std::string(SignatureConstants::ShortDateFormat));
    short_date_ = short_date_formatter_.now(time_system_);
  }
  std::string getHeaderSignature() {
    auto authheader = message_.headers()
                          .get(Envoy::Http::LowerCaseString("Authorization"))[0]
                          ->value()
                          .getStringView();
    std::vector<std::string> authsplit = absl::StrSplit(authheader, ',');
    std::vector<std::string> sigsplit = absl::StrSplit(authsplit[2], '=');
    return sigsplit[1];
  }

  std::string getQuerySignature() {
    auto query =
        Http::Utility::QueryParamsMulti::parseQueryString(message_.headers().getPathValue());
    auto val = query.getFirstValue("X-Amz-Signature");
    if (val.has_value()) {
      return val.value();
    } else {
      return "";
    }
  }

  void addBodySigningIfRequired() {
    // Set body signing true if we have content-length
    sign_body_ = !message_.headers().get(Envoy::Http::LowerCaseString("content-length")).empty();

    if (sign_body_) {
      message_.body().add(body_);
      auto& hashing_util = Envoy::Common::Crypto::UtilitySingleton::get();
      content_hash_ = Hex::encode(hashing_util.getSha256Digest(message_.body()));
      if (!query_string_) {
        message_.headers().setReferenceKey(SignatureHeaders::get().ContentSha256, content_hash_);
      }
    }
  }

  void ecdsaVerifyAWSSignature(std::string akid, std::string skid, std::string string_to_sign,
                               std::string calculated_signature) {
    std::vector<uint8_t> signature;
    auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
    auto hash = crypto_util.getSha256Digest(Buffer::OwnedImpl(string_to_sign));

    EC_KEY* ec_key =
        SigV4AKeyDerivation::derivePrivateKey(absl::string_view(akid), absl::string_view(skid));
    SigV4AKeyDerivation::derivePublicKey(ec_key);
    signature = Hex::decode(calculated_signature);

    EXPECT_EQ(
        1, ECDSA_verify(0, hash.data(), hash.size(), signature.data(), signature.size(), ec_key));
    EC_KEY_free(ec_key);
  }

  NiceMock<MockCredentialsProvider>* credentials_provider_;
  Http::RequestMessageImpl message_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Json::ObjectSharedPtr json_context_;
  bool normalize_, omit_session_token_, sign_body_, query_string_;
  std::string region_, akid_, skid_, timestamp_, service_, method_, path_, long_date_, short_date_,
      token_, rundir_;
  int64_t expiration_;
  std::string body_ = "";
  Event::SimulatedTimeSystem time_system_;
  absl::Time past_time_;
  std::string content_hash_ = "";
};

class SigV4ASignerImplFriend {
public:
  SigV4ASignerImplFriend(SigV4ASignerImpl* signer) : signer_(signer) {}

  std::string createCredentialScope(const absl::string_view short_date,
                                    const absl::string_view override_region) {
    return signer_->createCredentialScope(short_date, override_region);
  }

  std::string createStringToSign(const absl::string_view canonical_request,
                                 const absl::string_view long_date,
                                 const absl::string_view credential_scope) {
    return signer_->createStringToSign(canonical_request, long_date, credential_scope);
  }

  std::string createSignature(ABSL_ATTRIBUTE_UNUSED const absl::string_view access_key_id,
                              const absl::string_view secret_access_key,
                              const absl::string_view short_date,
                              const absl::string_view string_to_sign,
                              const absl::string_view override_region) {
    return signer_->createSignature(access_key_id, secret_access_key, short_date, string_to_sign,
                                    override_region);
  }

  std::string createAuthorizationHeader(const absl::string_view access_key_id,
                                        const absl::string_view credential_scope,
                                        const std::map<std::string, std::string>& canonical_headers,
                                        const absl::string_view signature) {
    return signer_->createAuthorizationHeader(access_key_id, credential_scope, canonical_headers,
                                              signature);
  }

  std::string createAuthorizationCredential(absl::string_view access_key_id,
                                            absl::string_view credential_scope) {
    return signer_->createAuthorizationCredential(access_key_id, credential_scope);
  }

  void createQueryParams(Envoy::Http::Utility::QueryParamsMulti& query_params,
                         const absl::string_view authorization_credential,
                         const absl::string_view long_date,
                         const absl::optional<std::string> session_token,
                         const std::map<std::string, std::string>& signed_headers,
                         const uint16_t expiration_time) {
    return signer_->createQueryParams(query_params, authorization_credential, long_date,
                                      session_token, signed_headers, expiration_time);
  };

  void addRequiredHeaders(Http::RequestHeaderMap& headers, const std::string long_date,
                          const absl::optional<std::string> session_token,
                          const absl::string_view override_region) {
    signer_->addRequiredHeaders(headers, long_date, session_token, override_region);
  }

  void addRegionQueryParam(Envoy::Http::Utility::QueryParamsMulti& query_params,
                           const absl::string_view override_region) {
    signer_->addRegionQueryParam(query_params, override_region);
  }

  SigV4ASignerImpl* signer_;
};

// Avoid multi line header test, as these should never reach the signer
// Vanilla SigV4a query tests are missing in source repository
std::vector<std::string> denylist = {"get-header-value-multiline", "get-vanilla-query-order-key",
                                     "get-vanilla-query-order-value"};

TEST_P(SigV4ASignerCorpusTest, SigV4ASignerCorpusHeaderSigning) {
  rundir_ = GetParam();
  query_string_ = false;

  // Do not perform denylist tests
  for (auto& it : denylist) {
    if (rundir_.ends_with(it)) {
      return;
    }
  }

  loadContext();
  setupPathAndHeaders();
  setTime();
  setDate();
  addBodySigningIfRequired();

  auto* credentials_provider_ = new NiceMock<MockCredentialsProvider>();

  SigV4ASignerImpl headersigner_(
      service_, region_, CredentialsProviderSharedPtr{credentials_provider_}, context_,
      Extensions::Common::Aws::AwsSigningHeaderExclusionVector{}, false, expiration_);

  auto signer_friend = SigV4ASignerImplFriend(&headersigner_);

  signer_friend.addRequiredHeaders(message_.headers(), long_date_,
                                   absl::optional<std::string>(token_), region_);

  const auto calculated_canonical_headers = Utility::canonicalizeHeaders(message_.headers(), {});

  if (content_hash_.empty()) {
    content_hash_ = SignatureConstants::HashedEmptyString;
  }

  const auto calculated_canonical_request = Utility::createCanonicalRequest(
      method_, message_.headers().Path()->value().getStringView(), calculated_canonical_headers,
      content_hash_, normalize_, true);

  auto source_canonical_request_ = readStringFile("header-canonical-request.txt");
  EXPECT_EQ(source_canonical_request_, calculated_canonical_request);

  const auto calculated_credential_scope =
      signer_friend.createCredentialScope(short_date_, region_);
  const auto calculated_string_to_sign = signer_friend.createStringToSign(
      calculated_canonical_request, long_date_, calculated_credential_scope);

  const auto source_string_to_sign = readStringFile("header-string-to-sign.txt");
  EXPECT_EQ(source_string_to_sign, calculated_string_to_sign);

  const auto calculated_signature =
      signer_friend.createSignature(akid_, skid_, short_date_, calculated_string_to_sign, region_);

  const auto source_header_signature = readStringFile("header-signature.txt");

  // Check that the string to sign validates against the signature we calculated
  ecdsaVerifyAWSSignature(akid_, skid_, calculated_string_to_sign, calculated_signature);

  // Check that the string to sign also validates against the signature from the corpus
  ecdsaVerifyAWSSignature(akid_, skid_, calculated_string_to_sign, source_header_signature);
}

TEST_P(SigV4ASignerCorpusTest, SigV4ASignerCorpusQueryStringSigning) {
  rundir_ = GetParam();
  query_string_ = true;

  // Do not perform denylist tests
  for (auto& it : denylist) {
    if (rundir_.ends_with(it)) {
      return;
    }
  }

  loadContext();
  setupPathAndHeaders();
  setTime();
  setDate();
  addBodySigningIfRequired();

  const auto calculated_canonical_headers = Utility::canonicalizeHeaders(message_.headers(), {});
  auto* credentials_provider_ = new NiceMock<MockCredentialsProvider>();

  SigV4ASignerImpl querysigner_(
      service_, region_, CredentialsProviderSharedPtr{credentials_provider_}, context_,
      Extensions::Common::Aws::AwsSigningHeaderExclusionVector{}, true, expiration_);

  auto signer_friend = SigV4ASignerImplFriend(&querysigner_);

  const auto calculated_credential_scope =
      signer_friend.createCredentialScope(short_date_, region_);

  auto query_params =
      Envoy::Http::Utility::QueryParamsMulti::parseQueryString(message_.headers().getPathValue());

  signer_friend.addRegionQueryParam(query_params, region_);

  signer_friend.createQueryParams(
      query_params, signer_friend.createAuthorizationCredential(akid_, calculated_credential_scope),
      long_date_, token_.empty() ? absl::optional<std::string>(absl::nullopt) : token_,
      calculated_canonical_headers, expiration_);

  message_.headers().setPath(query_params.replaceQueryString(message_.headers().Path()->value()));

  if (content_hash_.empty()) {
    content_hash_ = SignatureConstants::HashedEmptyString;
  }

  const auto calculated_canonical_request = Utility::createCanonicalRequest(
      method_, message_.headers().Path()->value().getStringView(), calculated_canonical_headers,
      content_hash_, normalize_, true);

  const auto source_canonical_request_ = readStringFile("query-canonical-request.txt");
  EXPECT_EQ(source_canonical_request_, calculated_canonical_request);

  const auto calculated_string_to_sign = signer_friend.createStringToSign(
      calculated_canonical_request, long_date_, calculated_credential_scope);

  const auto source_string_to_sign = readStringFile("query-string-to-sign.txt");
  EXPECT_EQ(source_string_to_sign, calculated_string_to_sign);

  const auto calculated_signature =
      signer_friend.createSignature(akid_, skid_, short_date_, calculated_string_to_sign, region_);

  const auto source_query_signature_ = readStringFile("query-signature.txt");

  // Check that the string to sign validates against the signature we calculated
  ecdsaVerifyAWSSignature(akid_, skid_, calculated_string_to_sign, calculated_signature);

  // Check that the string to sign also validates against the signature from the corpus
  ecdsaVerifyAWSSignature(akid_, skid_, calculated_string_to_sign, source_query_signature_);
}

INSTANTIATE_TEST_SUITE_P(SigV4ASignerCorpusTestSuite, SigV4ASignerCorpusTest,
                         ::testing::ValuesIn(directoryListing()),
                         [](const testing::TestParamInfo<SigV4ASignerCorpusTest::ParamType>& info) {
                           std::string a = std::filesystem::path(info.param).filename();
                           a.erase(std::remove(a.begin(), a.end(), '-'), a.end());
                           return a;
                         });

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
