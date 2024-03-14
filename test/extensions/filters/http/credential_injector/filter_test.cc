#include "source/extensions/filters/http/credential_injector/credential_injector_filter.h"
#include "source/extensions/injected_credentials/generic/generic_impl.h"

#include "test/mocks/http/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CredentialInjector {

class MockRequest : public InjectedCredentials::Common::CredentialInjector::Request {
public:
  MockRequest() = default;
  void cancel() override{};
};

// Mock failed to get credential
class MockCredentialInjector : public InjectedCredentials::Common::CredentialInjector {
public:
  MockCredentialInjector(const std::string& header, const std::string& credentail,
                         bool failGetCredential, bool failInjectCredential, bool async)
      : header_(header), credential_(credentail), failGetCredential_(failGetCredential),
        failInjectCredential_(failInjectCredential), async_(async){};

  // Common::CredentialInjector
  RequestPtr requestCredential(Callbacks& callbacks) override {
    // Mock async credential request
    if (async_) {
      return std::make_unique<MockRequest>();
    }

    if (failGetCredential_) {
      callbacks.onFailure("Failed to get credential");
    } else {
      callbacks.onSuccess();
    }
    return nullptr;
  };

  absl::Status inject(Http::RequestHeaderMap& headers, bool) override {
    if (failInjectCredential_) {
      return absl::NotFoundError("Failed to inject credential");
    } else {
      headers.setCopy(Http::LowerCaseString(header_), credential_);
      return absl::OkStatus();
    }
  }

private:
  const std::string header_;
  const std::string credential_;
  const bool failGetCredential_;
  const bool failInjectCredential_;
  const bool async_;
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

  // The first request will trigger the credential initialization
  Http::TestRequestHeaderMapImpl request_headers{};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter->decodeHeaders(request_headers, true));
  EXPECT_EQ("Basic base64EncodedUsernamePassword", request_headers.get_("Authorization"));

  // The second request won't trigger the credential initialization, and the credential will be
  // injected directly.
  Http::TestRequestHeaderMapImpl second_request_headers{};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter->decodeHeaders(second_request_headers, true));
  EXPECT_EQ("Basic base64EncodedUsernamePassword", second_request_headers.get_("Authorization"));
  filter->onDestroy();
}

TEST(Factory, ExistingCredentailDisallowOverwrite) {
  std::string header = "Authorization";
  auto secret_reader = std::make_shared<MockSecretReader>("Basic base64EncodedUsernamePassword");
  auto extenstion = std::make_shared<InjectedCredentials::Generic::GenericCredentialInjector>(
      header, secret_reader);
  NiceMock<Stats::IsolatedStoreImpl> stats;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks;

  auto config = std::make_shared<FilterConfig>(extenstion, false, false, "stats",
                                               *stats.rootScope()); // Disallow overwrite
  std::shared_ptr<CredentialInjectorFilter> filter =
      std::make_shared<CredentialInjectorFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_filter_callbacks);

  Http::TestRequestHeaderMapImpl request_headers{{"Authorization", "Basic existingCredential"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter->decodeHeaders(request_headers, true));
  EXPECT_EQ("Basic existingCredential", request_headers.get_("Authorization"));
  filter->onDestroy();
}

TEST(Factory, ExistingCredentialAllowOverwrite) {
  std::string header = "Authorization";
  auto secret_reader = std::make_shared<MockSecretReader>("Basic base64EncodedUsernamePassword");
  auto extenstion = std::make_shared<InjectedCredentials::Generic::GenericCredentialInjector>(
      header, secret_reader);
  NiceMock<Stats::IsolatedStoreImpl> stats;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks;

  auto config = std::make_shared<FilterConfig>(extenstion, true, false, "stats",
                                               *stats.rootScope()); // Allow overwrite
  std::shared_ptr<CredentialInjectorFilter> filter =
      std::make_shared<CredentialInjectorFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_filter_callbacks);

  Http::TestRequestHeaderMapImpl request_headers{{"Authorization", "Basic existingCredential"}};

  // The first request will trigger the credential initialization
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter->decodeHeaders(request_headers, true));
  EXPECT_EQ("Basic base64EncodedUsernamePassword", request_headers.get_("Authorization"));

  // The second request won't trigger the credential initialization, and the credential will be
  // injected directly.
  Http::TestRequestHeaderMapImpl second_request_headers{
      {"Authorization", "Basic existingCredential"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter->decodeHeaders(second_request_headers, true));
  EXPECT_EQ("Basic base64EncodedUsernamePassword", second_request_headers.get_("Authorization"));
  filter->onDestroy();
}

TEST(Factory, FailedToGetCredentialDisAllowWithoutCredential) {
  std::string header = "Authorization";
  auto secret_reader = std::make_shared<MockSecretReader>(""); // Mock failed to get credential
  auto extenstion = std::make_shared<InjectedCredentials::Generic::GenericCredentialInjector>(
      header, secret_reader);
  NiceMock<Stats::IsolatedStoreImpl> stats;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks;

  auto config =
      std::make_shared<FilterConfig>(extenstion, false, false, "stats",
                                     *stats.rootScope()); // Disallow requests without credentials
  std::shared_ptr<CredentialInjectorFilter> filter =
      std::make_shared<CredentialInjectorFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_filter_callbacks);

  // The first request will trigger the credential initialization
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

  // The second request won't trigger the credential initialization, and the request will fail
  Http::TestRequestHeaderMapImpl second_request_headers{};

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
  filter->decodeHeaders(second_request_headers, true);
  filter->onDestroy();
}

TEST(Factory, FailedToGetCredentialAllowWithoutCredential) {
  std::string header = "Authorization";
  auto secret_reader = std::make_shared<MockSecretReader>(""); // Mock failed to get credential
  auto extenstion = std::make_shared<InjectedCredentials::Generic::GenericCredentialInjector>(
      header, secret_reader);
  NiceMock<Stats::IsolatedStoreImpl> stats;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks;

  auto config = std::make_shared<FilterConfig>(
      extenstion, false, true, "stats", *stats.rootScope()); // Allow requests without credentials
  std::shared_ptr<CredentialInjectorFilter> filter =
      std::make_shared<CredentialInjectorFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_filter_callbacks);

  Http::TestRequestHeaderMapImpl request_headers{};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter->decodeHeaders(request_headers, true));
  EXPECT_EQ("", request_headers.get_("Authorization"));
  filter->onDestroy();
}

TEST(Factory, FailedToRequestCredentialDisAllowWithoutCredential) {
  auto extenstion = std::make_shared<MockCredentialInjector>(
      "", "", true, false, false); // Mock failed to request credential
  NiceMock<Stats::IsolatedStoreImpl> stats;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks;

  auto config =
      std::make_shared<FilterConfig>(extenstion, false, false, "stats",
                                     *stats.rootScope()); // Disallow requests without credentials
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

  // The second request will fail because the first request has failed to request the credential,
  // and it's not allowed to continue without the credential.
  Http::TestRequestHeaderMapImpl second_request_headers{};

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
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter->decodeHeaders(second_request_headers, true));
  filter->onDestroy();
}

TEST(Factory, FailedToRequestCredentialAllowWithoutCredential) {
  auto extenstion = std::make_shared<MockCredentialInjector>(
      "", "", true, false, false); // Mock failed to request credential
  NiceMock<Stats::IsolatedStoreImpl> stats;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks;

  auto config = std::make_shared<FilterConfig>(
      extenstion, false, true, "stats", *stats.rootScope()); // Allow requests without credentials
  std::shared_ptr<CredentialInjectorFilter> filter =
      std::make_shared<CredentialInjectorFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_filter_callbacks);

  Http::TestRequestHeaderMapImpl request_headers{};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter->decodeHeaders(request_headers, true));
  EXPECT_EQ("", request_headers.get_("Authorization"));

  // The second request will succeed because it's allowed to continue without the credential.
  Http::TestRequestHeaderMapImpl second_request_headers{};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter->decodeHeaders(second_request_headers, true));
  EXPECT_EQ("", second_request_headers.get_("Authorization"));
  filter->onDestroy();
}

TEST(Factory, AsyncRequestCredential) {
  auto extenstion = std::make_shared<MockCredentialInjector>(
      "", "", false, false, true); // Mock failed to request credential
  NiceMock<Stats::IsolatedStoreImpl> stats;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks;

  auto config =
      std::make_shared<FilterConfig>(extenstion, false, false, "stats", *stats.rootScope());
  std::shared_ptr<CredentialInjectorFilter> filter =
      std::make_shared<CredentialInjectorFilter>(config);
  filter->setDecoderFilterCallbacks(decoder_filter_callbacks);

  Http::TestRequestHeaderMapImpl request_headers{};

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
            filter->decodeHeaders(request_headers, true));
  filter->onDestroy();
}

} // namespace CredentialInjector
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
