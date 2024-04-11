#include "source/extensions/filters/http/credential_injector/credential_injector_filter.h"
#include "source/extensions/http/injected_credentials/generic/generic_impl.h"

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

TEST(Factory, InjectCredential) {
  std::string header = "Authorization";
  auto secret_reader = std::make_shared<MockSecretReader>("Basic base64EncodedUsernamePassword");
  auto extension = std::make_shared<Http::InjectedCredentials::Generic::GenericCredentialInjector>(
      header, secret_reader);
  NiceMock<Stats::IsolatedStoreImpl> stats;
  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks;

  auto config =
      std::make_shared<FilterConfig>(extension, false, false, "stats", *stats.rootScope());
  std::shared_ptr<CredentialInjectorFilter> filter =
      std::make_shared<CredentialInjectorFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_filter_callbacks);

  Envoy::Http::TestRequestHeaderMapImpl request_headers{};

  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue,
            filter->decodeHeaders(request_headers, true));
  EXPECT_EQ("Basic base64EncodedUsernamePassword", request_headers.get_("Authorization"));
  filter->onDestroy();
}

TEST(Factory, ExistingCredentailDisallowOverwrite) {
  std::string header = "Authorization";
  auto secret_reader = std::make_shared<MockSecretReader>("Basic base64EncodedUsernamePassword");
  auto extension = std::make_shared<Http::InjectedCredentials::Generic::GenericCredentialInjector>(
      header, secret_reader);
  NiceMock<Stats::IsolatedStoreImpl> stats;
  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks;

  auto config = std::make_shared<FilterConfig>(extension, false, false, "stats",
                                               *stats.rootScope()); // Disallow overwrite
  std::shared_ptr<CredentialInjectorFilter> filter =
      std::make_shared<CredentialInjectorFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_filter_callbacks);

  Envoy::Http::TestRequestHeaderMapImpl request_headers{
      {"Authorization", "Basic existingCredential"}};

  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue,
            filter->decodeHeaders(request_headers, true));
  EXPECT_EQ("Basic existingCredential", request_headers.get_("Authorization"));
}

TEST(Factory, ExistingCredentialAllowOverwrite) {
  std::string header = "Authorization";
  auto secret_reader = std::make_shared<MockSecretReader>("Basic base64EncodedUsernamePassword");
  auto extension = std::make_shared<Http::InjectedCredentials::Generic::GenericCredentialInjector>(
      header, secret_reader);
  NiceMock<Stats::IsolatedStoreImpl> stats;
  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks;

  auto config = std::make_shared<FilterConfig>(extension, true, false, "stats",
                                               *stats.rootScope()); // Allow overwrite
  std::shared_ptr<CredentialInjectorFilter> filter =
      std::make_shared<CredentialInjectorFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_filter_callbacks);

  Envoy::Http::TestRequestHeaderMapImpl request_headers{
      {"Authorization", "Basic existingCredential"}};

  // The first request will trigger the credential initialization
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue,
            filter->decodeHeaders(request_headers, true));
  EXPECT_EQ("Basic base64EncodedUsernamePassword", request_headers.get_("Authorization"));
}

TEST(Factory, FailedToInjectCredentialDisAllowWithoutCredential) {
  std::string header = "Authorization";
  auto secret_reader = std::make_shared<MockSecretReader>(""); // Mock failed to inject credential
  auto extension = std::make_shared<Http::InjectedCredentials::Generic::GenericCredentialInjector>(
      header, secret_reader);
  NiceMock<Stats::IsolatedStoreImpl> stats;
  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks;

  auto config =
      std::make_shared<FilterConfig>(extension, false, false, "stats",
                                     *stats.rootScope()); // Disallow requests without credentials
  std::shared_ptr<CredentialInjectorFilter> filter =
      std::make_shared<CredentialInjectorFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_filter_callbacks);

  // The first request will trigger the credential initialization
  Envoy::Http::TestRequestHeaderMapImpl request_headers{};

  EXPECT_CALL(decoder_filter_callbacks, sendLocalReply(_, _, _, _, _))
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

TEST(Factory, FailedToInjectCredentialAllowWithoutCredential) {
  std::string header = "Authorization";
  auto secret_reader = std::make_shared<MockSecretReader>(""); // Mock failed to inject credential
  auto extension = std::make_shared<Http::InjectedCredentials::Generic::GenericCredentialInjector>(
      header, secret_reader);
  NiceMock<Stats::IsolatedStoreImpl> stats;
  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks;

  auto config = std::make_shared<FilterConfig>(
      extension, false, true, "stats", *stats.rootScope()); // Allow requests without credentials
  std::shared_ptr<CredentialInjectorFilter> filter =
      std::make_shared<CredentialInjectorFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_filter_callbacks);

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
