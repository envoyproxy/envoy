#include "envoy/extensions/transport_sockets/tls/v3/secret.pb.h"

#include "source/common/secret/secret_provider_impl.h"

#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/thread_factory_for_test.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Secret {
namespace {

class ThreadLocalGenericSecretProviderTest : public testing::Test {
public:
  ThreadLocalGenericSecretProviderPtr
  makeProvider(const envoy::extensions::transport_sockets::tls::v3::GenericSecret& secret) {
    auto provider_or_error = ThreadLocalGenericSecretProvider::create(
        std::make_shared<GenericSecretConfigProviderImpl>(secret), tls_, *api_, *dispatcher_);
    EXPECT_TRUE(provider_or_error.ok());
    return std::move(provider_or_error.value());
  }

  NiceMock<ThreadLocal::MockInstance> tls_;
  Api::ApiPtr api_{Api::createApiForTest()};
  Event::DispatcherPtr dispatcher_{api_->allocateDispatcher("test_main_thread")};
};

// A single-value secret is returned by the no-argument accessor and exposes no named entries.
TEST_F(ThreadLocalGenericSecretProviderTest, SingleValueSecret) {
  envoy::extensions::transport_sockets::tls::v3::GenericSecret secret;
  secret.mutable_secret()->set_inline_string("single-value");

  auto provider = makeProvider(secret);

  EXPECT_EQ("single-value", provider->secret());
  EXPECT_EQ("", provider->secret("private_key"));
}

// A multi-entry secret exposes each entry by name, and the no-argument accessor is empty.
TEST_F(ThreadLocalGenericSecretProviderTest, MultiEntrySecret) {
  envoy::extensions::transport_sockets::tls::v3::GenericSecret secret;
  (*secret.mutable_secrets())["private_key"].set_inline_string("pem-data");
  (*secret.mutable_secrets())["key_id"].set_inline_string("my-key-id");

  auto provider = makeProvider(secret);

  EXPECT_EQ("", provider->secret());
  EXPECT_EQ("pem-data", provider->secret("private_key"));
  EXPECT_EQ("my-key-id", provider->secret("key_id"));
}

// Looking up an entry the secret does not carry returns an empty string.
TEST_F(ThreadLocalGenericSecretProviderTest, MissingNamedEntry) {
  envoy::extensions::transport_sockets::tls::v3::GenericSecret secret;
  (*secret.mutable_secrets())["private_key"].set_inline_string("pem-data");

  auto provider = makeProvider(secret);

  EXPECT_EQ("", provider->secret("key_id"));
  EXPECT_EQ("", provider->secret("absent"));
}

// An empty secret yields empty values for both accessors.
TEST_F(ThreadLocalGenericSecretProviderTest, EmptySecret) {
  envoy::extensions::transport_sockets::tls::v3::GenericSecret secret;

  auto provider = makeProvider(secret);

  EXPECT_EQ("", provider->secret());
  EXPECT_EQ("", provider->secret("private_key"));
}

// A named entry whose data source cannot be read fails provider creation.
TEST_F(ThreadLocalGenericSecretProviderTest, UnreadableNamedEntry) {
  envoy::extensions::transport_sockets::tls::v3::GenericSecret secret;
  (*secret.mutable_secrets())["private_key"].set_filename("/does/not/exist");

  auto provider_or_error = ThreadLocalGenericSecretProvider::create(
      std::make_shared<GenericSecretConfigProviderImpl>(secret), tls_, *api_, *dispatcher_);

  EXPECT_FALSE(provider_or_error.ok());
}

TEST_F(ThreadLocalGenericSecretProviderTest, ReleasedOnWorkerThreadIsDestroyedOnMainThread) {
  envoy::extensions::transport_sockets::tls::v3::GenericSecret secret;
  secret.mutable_secret()->set_inline_string("single-value");

  auto config_provider = std::make_shared<GenericSecretConfigProviderImpl>(secret);
  std::weak_ptr<GenericSecretConfigProviderImpl> config_provider_tracker = config_provider;

  auto provider_or_error = ThreadLocalGenericSecretProvider::create(std::move(config_provider),
                                                                    tls_, *api_, *dispatcher_);
  ASSERT_TRUE(provider_or_error.ok());
  auto provider = std::move(provider_or_error.value());

  // Pin the dispatcher's thread so that a release from any other thread is not thread safe.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  Thread::ThreadPtr worker =
      Thread::threadFactoryForTest().createThread([&provider]() { provider.reset(); });
  worker->join();

  // The provider outlives the worker thread: its destruction is queued on the main dispatcher.
  EXPECT_FALSE(config_provider_tracker.expired());
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_TRUE(config_provider_tracker.expired());
}

} // namespace
} // namespace Secret
} // namespace Envoy
