#include "source/extensions/filters/http/credential_injector/credential_injector_filter.h"
#include "source/extensions/injected_credentials/generic/generic_impl.h"

#include "test/mocks/http/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CredentialInjector {

// Mock failed to get credential
class MockCredentialInjector : public InjectedCredentials::Common::CredentialInjector {
public:
  MockCredentialInjector(){};

  // Common::CredentialInjector
  RequestPtr requestCredential(Callbacks& callbacks) override {
    callbacks.onFailure("Failed to get credential");
    return nullptr;
  };

  absl::Status inject(Http::RequestHeaderMap&, bool) override {
    return absl::NotFoundError("Failed to get credential from secret");
  }
};

class MockSecretReader : public InjectedCredentials::Common::SecretReader {
public:
  MockSecretReader(const std::string& secret) : secret_(secret){};
  const std::string& credential() const override { return secret_; }

private:
  const std::string secret_;
};

TEST(Factory, InjectCredential) {
  std::string header = "Authorization";
  auto secret_reader = std::make_shared<MockSecretReader>("Basic base64EncodedUsernamePassword");
  auto extenstion = std::make_shared<InjectedCredentials::Generic::GenericCredentialInjector>(
      header, secret_reader);
  NiceMock<Stats::IsolatedStoreImpl> stats;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks;

  auto config =
      std::make_shared<FilterConfig>(extenstion, false, false, "stats", *stats.rootScope());
  std::shared_ptr<CredentialInjectorFilter> filter =
      std::make_shared<CredentialInjectorFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_filter_callbacks);

  Http::TestRequestHeaderMapImpl request_headers{};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter->decodeHeaders(request_headers, true));
  EXPECT_EQ("Basic base64EncodedUsernamePassword", request_headers.get_("Authorization"));
}

TEST(Factory, InjectCredentialExistingAllowOverwrite) {
  std::string header = "Authorization";
  auto secret_reader = std::make_shared<MockSecretReader>("Basic base64EncodedUsernamePassword");
  auto extenstion = std::make_shared<InjectedCredentials::Generic::GenericCredentialInjector>(
      header, secret_reader);
  NiceMock<Stats::IsolatedStoreImpl> stats;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks;

  auto config =
      std::make_shared<FilterConfig>(extenstion, true, false, "stats", *stats.rootScope());
  std::shared_ptr<CredentialInjectorFilter> filter =
      std::make_shared<CredentialInjectorFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_filter_callbacks);

  Http::TestRequestHeaderMapImpl request_headers{{"Authorization", "Basic existingCredential"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter->decodeHeaders(request_headers, true));
  EXPECT_EQ("Basic base64EncodedUsernamePassword", request_headers.get_("Authorization"));
}

TEST(Factory, InjectCredentialExistingDisallowOverwrite) {
  std::string header = "Authorization";
  auto secret_reader = std::make_shared<MockSecretReader>("Basic base64EncodedUsernamePassword");
  auto extenstion = std::make_shared<InjectedCredentials::Generic::GenericCredentialInjector>(
      header, secret_reader);
  NiceMock<Stats::IsolatedStoreImpl> stats;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks;

  auto config =
      std::make_shared<FilterConfig>(extenstion, false, false, "stats", *stats.rootScope());
  std::shared_ptr<CredentialInjectorFilter> filter =
      std::make_shared<CredentialInjectorFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_filter_callbacks);

  Http::TestRequestHeaderMapImpl request_headers{{"Authorization", "Basic existingCredential"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter->decodeHeaders(request_headers, true));
  EXPECT_EQ("Basic existingCredential", request_headers.get_("Authorization"));
}

TEST(Factory, InjectCredentialFailedToGetCredentialDisAllowWithoutCredential) {
  std::string header = "Authorization";
  auto secret_reader = std::make_shared<MockSecretReader>(""); // Mock failed to get credential
  auto extenstion = std::make_shared<InjectedCredentials::Generic::GenericCredentialInjector>(
      header, secret_reader);
  NiceMock<Stats::IsolatedStoreImpl> stats;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks;

  auto config =
      std::make_shared<FilterConfig>(extenstion, false, false, "stats", *stats.rootScope());
  std::shared_ptr<CredentialInjectorFilter> filter =
      std::make_shared<CredentialInjectorFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_filter_callbacks);

  Http::TestRequestHeaderMapImpl request_headers{};

  EXPECT_CALL(decoder_filter_callbacks, sendLocalReply(_, _, _, _, _))
      .WillOnce(Invoke([&](Http::Code code, absl::string_view body,
                           std::function<void(Http::ResponseHeaderMap & headers)>,
                           const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                           absl::string_view details) {
        EXPECT_EQ(Http::Code::Unauthorized, code);
        EXPECT_EQ("Failed to inject credential.", body);
        EXPECT_EQ(grpc_status, absl::nullopt);
        EXPECT_EQ(details, "failed_to_inject_credential");
      }));
  filter->decodeHeaders(request_headers, true);
}

TEST(Factory, InjectCredentialFailedToGetCredentialAllowWithoutCredential) {
  std::string header = "Authorization";
  auto secret_reader = std::make_shared<MockSecretReader>(""); // Mock failed to get credential
  auto extenstion = std::make_shared<InjectedCredentials::Generic::GenericCredentialInjector>(
      header, secret_reader);
  NiceMock<Stats::IsolatedStoreImpl> stats;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks;

  auto config =
      std::make_shared<FilterConfig>(extenstion, false, false, "stats", *stats.rootScope());
  std::shared_ptr<CredentialInjectorFilter> filter =
      std::make_shared<CredentialInjectorFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_filter_callbacks);

  Http::TestRequestHeaderMapImpl request_headers{};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter->decodeHeaders(request_headers, true));
  EXPECT_EQ("", request_headers.get_("Authorization"));
}

TEST(Factory, InjectCredentialFailedToRequestCredentialDisAllowWithoutCredential) {
  auto extenstion = std::make_shared<MockCredentialInjector>(); // Mock failed to request credential
  NiceMock<Stats::IsolatedStoreImpl> stats;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks;

  auto config =
      std::make_shared<FilterConfig>(extenstion, false, false, "stats", *stats.rootScope());
  std::shared_ptr<CredentialInjectorFilter> filter =
      std::make_shared<CredentialInjectorFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_filter_callbacks);

  Http::TestRequestHeaderMapImpl request_headers{};

  EXPECT_CALL(decoder_filter_callbacks, sendLocalReply(_, _, _, _, _))
      .WillOnce(Invoke([&](Http::Code code, absl::string_view body,
                           std::function<void(Http::ResponseHeaderMap & headers)>,
                           const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                           absl::string_view details) {
        EXPECT_EQ(Http::Code::Unauthorized, code);
        EXPECT_EQ("Failed to inject credential.", body);
        EXPECT_EQ(grpc_status, absl::nullopt);
        EXPECT_EQ(details, "failed_to_inject_credential");
      }));
  filter->decodeHeaders(request_headers, true);
}

TEST(Factory, InjectCredentialFailedToRequestCredentialAllowWithoutCredential) {
  auto extenstion = std::make_shared<MockCredentialInjector>(); // Mock failed to request credential
  NiceMock<Stats::IsolatedStoreImpl> stats;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks;

  auto config =
      std::make_shared<FilterConfig>(extenstion, false, false, "stats", *stats.rootScope());
  std::shared_ptr<CredentialInjectorFilter> filter =
      std::make_shared<CredentialInjectorFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_filter_callbacks);

  Http::TestRequestHeaderMapImpl request_headers{};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter->decodeHeaders(request_headers, true));
  EXPECT_EQ("", request_headers.get_("Authorization"));
}

} // namespace CredentialInjector
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
