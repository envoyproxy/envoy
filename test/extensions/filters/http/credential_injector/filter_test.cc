#include "source/extensions/filters/http/credential_injector/credential_injector_filter.h"
#include "source/extensions/injected_credentials/generic/generic_impl.h"

#include "test/mocks/http/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CredentialInjector {

class MockSecretReader : public InjectedCredentials::Common::SecretReader {
public:
  const std::string& credential() const override {
    CONSTRUCT_ON_FIRST_USE(std::string, "Basic base64EncodedUsernamePassword");
  }
};

class FilterTest : public testing::Test {
public:
  FilterTest() {
    auto secret_reader = std::make_shared<MockSecretReader>();
    std::string header = "Authorization";
    auto extenstion = std::make_shared<InjectedCredentials::Generic::GenericCredentialInjector>(
        header, secret_reader);

    config_ = std::make_shared<FilterConfig>(extenstion, true, true, "stats", *stats_.rootScope());
    filter_ = std::make_shared<CredentialInjectorFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_filter_callbacks_);
  }

  NiceMock<Stats::IsolatedStoreImpl> stats_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_filter_callbacks_;
  FilterConfigSharedPtr config_;
  std::shared_ptr<CredentialInjectorFilter> filter_;
};

TEST_F(FilterTest, InjectCredential) {
  Http::TestRequestHeaderMapImpl request_headers{};

  EXPECT_EQ(Http::FilterHeadersStatus::StopAllIterationAndBuffer,
            filter_->decodeHeaders(request_headers, true));
}

} // namespace CredentialInjector
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
