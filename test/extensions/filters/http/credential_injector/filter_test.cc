#include "source/extensions/filters/http/credential_injector/credential_injector_filter.h"
#include "source/extensions/http/injected_credentials/generic/generic_impl.h"
#include "source/extensions/http/injected_credentials/oauth2/client_credentials_impl.h"

#include "test/mocks/http/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CredentialInjector {
namespace {

class MockSecretReader : public Http::InjectedCredentials::Common::SecretReader {
public:
  MockSecretReader(const std::string& secret) : secret_(secret){};
  const std::string& credential() const override { return secret_; }

private:
  const std::string secret_;
};

class CredentialInjectorFilterTest
    : public testing::TestWithParam<std::shared_ptr<CredentialInjector>> {
protected:
  std::shared_ptr<MockSecretReader> secret_reader_;
  std::shared_ptr<CredentialInjector> extension_;
  NiceMock<Stats::IsolatedStoreImpl> stats_;
  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks_;

  void setup(std::string secret) {
    secret_reader_ = std::make_shared<MockSecretReader>(secret);

    // Determine the type of GetParam()
    if (std::dynamic_pointer_cast<Http::InjectedCredentials::Generic::GenericCredentialInjector>(
            GetParam())) {
      extension_ = std::make_shared<Http::InjectedCredentials::Generic::GenericCredentialInjector>(
          "Authorization", secret_reader_);
      return;
    }
    if (std::dynamic_pointer_cast<
            Http::InjectedCredentials::OAuth2::OAuth2ClientCredentialTokenInjector>(GetParam())) {
      extension_ =
          std::make_shared<Http::InjectedCredentials::OAuth2::OAuth2ClientCredentialTokenInjector>(
              secret_reader_);
      return;
    }
  }
};

std::vector<std::shared_ptr<CredentialInjector>> GetCredentialInjectorImplementations() {
  std::vector<std::shared_ptr<CredentialInjector>> implementations;
  implementations.push_back(
      std::make_shared<Http::InjectedCredentials::Generic::GenericCredentialInjector>(
          "Authorization", nullptr));
  implementations.push_back(
      std::make_shared<Http::InjectedCredentials::OAuth2::OAuth2ClientCredentialTokenInjector>(
          nullptr));
  return implementations;
}

INSTANTIATE_TEST_SUITE_P(CredentialInjectorExtensions, CredentialInjectorFilterTest,
                         ::testing::ValuesIn(GetCredentialInjectorImplementations()));

TEST_P(CredentialInjectorFilterTest, InjectCredential) {
  this->setup("base64EncodedBasicAuthOrBearerToken");
  auto config =
      std::make_shared<FilterConfig>(extension_, false, false, "stats", *stats_.rootScope());
  std::shared_ptr<CredentialInjectorFilter> filter =
      std::make_shared<CredentialInjectorFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_filter_callbacks_);

  Envoy::Http::TestRequestHeaderMapImpl request_headers{};

  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue,
            filter->decodeHeaders(request_headers, true));
  EXPECT_EQ("base64EncodedBasicAuthOrBearerToken", request_headers.get_("Authorization"));
  filter->onDestroy();
}

TEST_P(CredentialInjectorFilterTest, ExistingCredentailDisallowOverwrite) {
  this->setup("base64EncodedBasicAuthOrBearerToken");
  auto config = std::make_shared<FilterConfig>(extension_, false, false, "stats",
                                               *stats_.rootScope()); // Disallow overwrite
  std::shared_ptr<CredentialInjectorFilter> filter =
      std::make_shared<CredentialInjectorFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_filter_callbacks_);

  Envoy::Http::TestRequestHeaderMapImpl request_headers{
      {"Authorization", "Basic existingCredential"}};

  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue,
            filter->decodeHeaders(request_headers, true));
  EXPECT_EQ("Basic existingCredential", request_headers.get_("Authorization"));
}

TEST_P(CredentialInjectorFilterTest, ExistingCredentialAllowOverwrite) {
  this->setup("base64EncodedBasicAuthOrBearerToken");
  auto config = std::make_shared<FilterConfig>(extension_, true, false, "stats",
                                               *stats_.rootScope()); // Allow overwrite
  std::shared_ptr<CredentialInjectorFilter> filter =
      std::make_shared<CredentialInjectorFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_filter_callbacks_);

  Envoy::Http::TestRequestHeaderMapImpl request_headers{
      {"Authorization", "Basic existingCredential"}};

  // The first request will trigger the credential initialization
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue,
            filter->decodeHeaders(request_headers, true));
  EXPECT_EQ("base64EncodedBasicAuthOrBearerToken", request_headers.get_("Authorization"));
}

TEST_P(CredentialInjectorFilterTest, FailedToInjectCredentialDisAllowWithoutCredential) {
  this->setup("");
  auto config =
      std::make_shared<FilterConfig>(extension_, false, false, "stats",
                                     *stats_.rootScope()); // Disallow requests without credentials
  std::shared_ptr<CredentialInjectorFilter> filter =
      std::make_shared<CredentialInjectorFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_filter_callbacks_);

  // The first request will trigger the credential initialization
  Envoy::Http::TestRequestHeaderMapImpl request_headers{};

  EXPECT_CALL(decoder_filter_callbacks_, sendLocalReply(_, _, _, _, _))
      .WillOnce(Invoke([&](Envoy::Http::Code code, absl::string_view body,
                           std::function<void(Envoy::Http::ResponseHeaderMap & headers)>,
                           const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                           absl::string_view details) {
        EXPECT_EQ(Envoy::Http::Code::Unauthorized, code);
        EXPECT_EQ("Failed to inject credential.", body);
        EXPECT_EQ(grpc_status, absl::nullopt);
        EXPECT_EQ(details, "failed_to_inject_credential");
      }));
  filter->decodeHeaders(request_headers, true);
}

TEST_P(CredentialInjectorFilterTest, FailedToInjectCredentialAllowWithoutCredential) {
  this->setup("");
  auto config = std::make_shared<FilterConfig>(
      extension_, false, true, "stats", *stats_.rootScope()); // Allow requests without credentials
  std::shared_ptr<CredentialInjectorFilter> filter =
      std::make_shared<CredentialInjectorFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_filter_callbacks_);

  Envoy::Http::TestRequestHeaderMapImpl request_headers{};

  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue,
            filter->decodeHeaders(request_headers, true));
  EXPECT_EQ("", request_headers.get_("Authorization"));
  filter->onDestroy();
}

} // namespace
} // namespace CredentialInjector
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
