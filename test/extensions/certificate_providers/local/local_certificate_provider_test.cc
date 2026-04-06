#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "source/extensions/certificate_providers/local/local_certificate_minter.h"
#include "source/extensions/certificate_providers/local/local_certificate_provider.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace CertificateProviders {
namespace Local {
namespace {

using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::ReturnRef;

class BlockingLocalCertificateMinter : public Ssl::LocalCertificateMinter {
public:
  absl::Status validateCaMaterial(absl::string_view, absl::string_view) const override {
    return absl::OkStatus();
  }

  absl::StatusOr<MintedCertificate> mint(const MintRequest&) const override {
    started_.fetch_add(1);
    release_.WaitForNotification();
    completed_.fetch_add(1);
    return absl::AbortedError("blocked for saturation test");
  }

  void release() { release_.Notify(); }
  int started() const { return started_.load(); }
  int completed() const { return completed_.load(); }

private:
  mutable std::atomic<int> started_{0};
  mutable std::atomic<int> completed_{0};
  mutable absl::Notification release_;
};

class LocalCertificateProviderTest : public testing::Test {
protected:
  LocalCertificateProviderTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("local_cert_test")) {
    ON_CALL(context_, mainThreadDispatcher()).WillByDefault(ReturnRef(*dispatcher_));
    ON_CALL(context_.api_, threadFactory())
        .WillByDefault(Invoke([this]() -> Thread::ThreadFactory& { return api_->threadFactory(); }));
    ON_CALL(context_.api_, fileSystem())
        .WillByDefault(Invoke([this]() -> Filesystem::Instance& { return api_->fileSystem(); }));
  }

  envoy::extensions::bootstrap::certificate_providers::local::v3::LocalSigner makeConfig() const {
    envoy::extensions::bootstrap::certificate_providers::local::v3::LocalSigner config;
    config.set_ca_cert_path(TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
    config.set_ca_key_path(TestEnvironment::runfilesPath("test/config/integration/certs/cakey.pem"));
    return config;
  }

  void drainDispatcherUntil(const std::function<bool()>& done) {
    for (int i = 0; i < 500 && !done(); ++i) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
      absl::SleepFor(absl::Milliseconds(10));
    }
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
};

TEST_F(LocalCertificateProviderTest, RejectsWhenMintQueueIsFull) {
  auto fake_minter = std::make_shared<BlockingLocalCertificateMinter>();
  auto previous_minter = Ssl::setDefaultLocalCertificateMinterForTest(fake_minter);

  LocalNamedTlsCertificateProvider provider(makeConfig());
  std::vector<Secret::TlsCertificateConfigProviderSharedPtr> providers;
  providers.reserve(256);

  bool saw_rejection = false;
  for (size_t i = 0; i < 256; ++i) {
    auto cert_provider = provider.getProvider(absl::StrCat("test-", i, ".example.com"), context_);
    if (cert_provider == nullptr) {
      saw_rejection = true;
      break;
    }
    providers.push_back(cert_provider);
  }

  EXPECT_TRUE(saw_rejection);

  fake_minter->release();
  drainDispatcherUntil([&]() { return fake_minter->completed() >= static_cast<int>(providers.size()); });

  Ssl::setDefaultLocalCertificateMinterForTest(previous_minter);
}

} // namespace
} // namespace Local
} // namespace CertificateProviders
} // namespace Extensions
} // namespace Envoy
