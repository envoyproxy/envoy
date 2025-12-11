#include <cstddef>
#include <cstdlib>
#include <memory>
#include <string>

#include "source/common/tls/private_key/private_key_manager_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "contrib/kae/private_key_providers/source/kae_private_key_provider.h"
#include "fake_factory.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace Kae {
// Testing interface
ssl_private_key_result_t privateKeySignForTest(KaePrivateKeyConnection* ops, uint8_t* out,
                                               size_t* out_len, size_t max_out,
                                               uint16_t signature_algorithm, const uint8_t* in,
                                               size_t in_len);
ssl_private_key_result_t privateKeyDecryptForTest(KaePrivateKeyConnection* ops, uint8_t* out,
                                                  size_t* out_len, size_t max_out,
                                                  const uint8_t* in, size_t in_len);
ssl_private_key_result_t privateKeyCompleteForTest(KaePrivateKeyConnection* ops,
                                                   KaeContext* kae_ctx, uint8_t* out,
                                                   size_t* out_len, size_t max_out);

namespace {
class TestCallbacks : public Envoy::Ssl::PrivateKeyConnectionCallbacks {
public:
  void onPrivateKeyMethodComplete() override { is_completed_ = true; };

  bool is_completed_{false};
};

class FakeSingletonManager : public Singleton::Manager {
public:
  FakeSingletonManager(LibUadkCryptoSharedPtr libuadk) : libuadk_(libuadk) {}
  Singleton::InstanceSharedPtr get(const std::string&, Singleton::SingletonFactoryCb,
                                   bool) override {
    return std::make_shared<KaeManager>(libuadk_);
  }

private:
  LibUadkCryptoSharedPtr libuadk_;
};

class KaeProviderTest : public testing::Test {
protected:
  KaeProviderTest()
      : api_(Api::createApiForTest(store_, time_system_)),
        dispatcher_(api_->allocateDispatcher("test_thread")),
        libuadk_(std::make_shared<FakeLibUadkCryptoImpl>()), fsm_(libuadk_) {
    handle_.setLibUadk(libuadk_);
    handle_.initKaeInstance(libuadk_);

    ON_CALL(factory_context_.server_context_, api()).WillByDefault(testing::ReturnRef(*api_));
    ON_CALL(factory_context_.server_context_, singletonManager())
        .WillByDefault(testing::ReturnRef(fsm_));
  }

  Stats::TestUtil::TestStore store_;
  Api::ApiPtr api_;
  Event::SimulatedTimeSystem time_system_;
  Event::DispatcherPtr dispatcher_;
  KaeHandle handle_;
  std::shared_ptr<FakeLibUadkCryptoImpl> libuadk_;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context_;
  FakeSingletonManager fsm_;

  // Result of an operation.
  ssl_private_key_result_t res_;

  // A size for signing and decryption operation input chosen for tests.
  static constexpr size_t in_len_ = 32;
  // Test input bytes for signing and decryption chosen for tests.
  static constexpr uint8_t in_[in_len_] = {0x7f};

  // Maximum size of out_ in all test cases.
  static constexpr size_t max_out_len_ = 256;
  uint8_t out_[max_out_len_] = {0};

  // Size of output in out_ from an operation.
  size_t out_len_ = 0;
};

class KaeProviderRsaTest : public KaeProviderTest {
public:
  bssl::UniquePtr<EVP_PKEY> makeRsaKey() {
    std::string file = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
        "{{ test_rundir }}/contrib/kae/private_key_providers/test/test_data/rsa-2048.pem"));
    bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(file.data(), file.size()));

    bssl::UniquePtr<EVP_PKEY> key(EVP_PKEY_new());

    RSA* rsa = PEM_read_bio_RSAPrivateKey(bio.get(), nullptr, nullptr, nullptr);
    RELEASE_ASSERT(rsa != nullptr, "PEM_read_bio_RSAPrivateKey failed.");
    RELEASE_ASSERT(1 == EVP_PKEY_assign_RSA(key.get(), rsa), "EVP_PKEY_assign_RSA failed.");
    return key;
  }

protected:
  KaeProviderRsaTest() : pkey_(makeRsaKey()) {
    rsa_ = EVP_PKEY_get0_RSA(pkey_.get());
    libuadk_->setRsaKey(rsa_);
  }
  bssl::UniquePtr<EVP_PKEY> pkey_;
  RSA* rsa_{};
};

TEST_F(KaeProviderRsaTest, TestRsaPkcs1Signing) {
  // PKCS #1 v1.5.
  TestCallbacks cb;
  KaePrivateKeyConnection op(cb, *dispatcher_, handle_, bssl::UpRef(pkey_));

  res_ = privateKeySignForTest(&op, nullptr, nullptr, max_out_len_, SSL_SIGN_RSA_PKCS1_SHA256, in_,
                               in_len_);
  EXPECT_EQ(res_, ssl_private_key_retry);

  // When we called the sign operation, KAE context was registered. Ask it back so we can provide it
  // to complete() function.
  KaeContext* ctx = static_cast<KaeContext*>(libuadk_->getKaeContextPointer());
  EXPECT_NE(ctx, nullptr);
  op.registerCallback(ctx);

  ctx->setOpStatus(WD_STATUS_BUSY);
  res_ = privateKeyCompleteForTest(&op, ctx, nullptr, nullptr, max_out_len_);
  EXPECT_EQ(res_, ssl_private_key_retry);

  libuadk_->triggerDecrypt();
  ctx->setOpStatus(WD_SUCCESS);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_TRUE(cb.is_completed_);

  res_ = privateKeyCompleteForTest(&op, ctx, out_, &out_len_, max_out_len_);
  EXPECT_EQ(res_, ssl_private_key_success);
  EXPECT_NE(out_len_, 0);

  // Check the signature in out_.
  RSA* rsa = EVP_PKEY_get0_RSA(pkey_.get());

  uint8_t buf[max_out_len_] = {0};
  size_t buf_len = 0;
  EXPECT_EQ(RSA_verify_raw(rsa, &buf_len, buf, max_out_len_, out_, out_len_, RSA_PKCS1_PADDING), 1);
}

TEST_F(KaeProviderRsaTest, TestRsaPSSSigning) {
  // RSA-PSS
  TestCallbacks cb;
  KaePrivateKeyConnection op(cb, *dispatcher_, handle_, bssl::UpRef(pkey_));

  res_ = privateKeySignForTest(&op, nullptr, nullptr, max_out_len_, SSL_SIGN_RSA_PSS_SHA256, in_,
                               in_len_);
  EXPECT_EQ(res_, ssl_private_key_retry);

  // When we called the sign operation, KAE context was registered. Ask it back so we can provide it
  // to complete() function.
  KaeContext* ctx = static_cast<KaeContext*>(libuadk_->getKaeContextPointer());
  EXPECT_NE(ctx, nullptr);
  op.registerCallback(ctx);

  ctx->setOpStatus(WD_STATUS_BUSY);
  res_ = privateKeyCompleteForTest(&op, ctx, nullptr, nullptr, max_out_len_);
  EXPECT_EQ(res_, ssl_private_key_retry);

  libuadk_->triggerDecrypt();
  ctx->setOpStatus(WD_SUCCESS);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_TRUE(cb.is_completed_);

  res_ = privateKeyCompleteForTest(&op, ctx, out_, &out_len_, max_out_len_);
  EXPECT_EQ(res_, ssl_private_key_success);
  EXPECT_NE(out_len_, 0);

  // Check the signature in out_.
  RSA* rsa = EVP_PKEY_get0_RSA(pkey_.get());

  uint8_t buf[max_out_len_] = {0};
  unsigned int buf_len = 0;
  const EVP_MD* md = SSL_get_signature_algorithm_digest(SSL_SIGN_RSA_PSS_SHA256);
  EXPECT_NE(md, nullptr);
  bssl::ScopedEVP_MD_CTX md_ctx;
  // Calculate the message digest (so that we can be sure that it has been signed).
  EXPECT_EQ(EVP_DigestInit_ex(md_ctx.get(), md, nullptr), 1);
  EXPECT_EQ(EVP_DigestUpdate(md_ctx.get(), in_, in_len_), 1);
  EXPECT_EQ(EVP_DigestFinal_ex(md_ctx.get(), buf, &buf_len), 1);

  EXPECT_EQ(RSA_verify_pss_mgf1(rsa, buf, buf_len, md, nullptr, -1, out_, out_len_), 1);
}

TEST_F(KaeProviderRsaTest, TestRsaDecryption) {
  TestCallbacks cb;
  KaePrivateKeyConnection op(cb, *dispatcher_, handle_, bssl::UpRef(pkey_));
  RSA* rsa = EVP_PKEY_get0_RSA(pkey_.get());
  uint8_t encrypt_buf[256] = {0x0};                                              // RSA_size()
  uint8_t in_buf[128] = {'l', 'o', 'r', 'e', 'm', ' ', 'i', 'p', 's', 'u', 'm'}; // RSA_size() / 2

  int ret = RSA_public_encrypt(128, in_buf, encrypt_buf, rsa, RSA_PKCS1_PADDING);
  EXPECT_EQ(ret, RSA_size(rsa));

  res_ = privateKeyDecryptForTest(&op, nullptr, nullptr, max_out_len_, encrypt_buf, RSA_size(rsa));
  EXPECT_EQ(res_, ssl_private_key_retry);

  KaeContext* ctx = static_cast<KaeContext*>(libuadk_->getKaeContextPointer());
  EXPECT_NE(ctx, nullptr);
  op.registerCallback(ctx);

  ctx->setOpStatus(WD_STATUS_BUSY);
  res_ = privateKeyCompleteForTest(&op, ctx, nullptr, nullptr, max_out_len_);
  EXPECT_EQ(res_, ssl_private_key_retry);

  libuadk_->triggerDecrypt();
  ctx->setOpStatus(WD_SUCCESS);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_TRUE(cb.is_completed_);

  res_ = privateKeyCompleteForTest(&op, ctx, out_, &out_len_, max_out_len_);
  EXPECT_EQ(res_, ssl_private_key_success);
  EXPECT_EQ(out_len_, 256);

  // The padding is for the first 128 bytes and the data starts after that.
  for (size_t i = 0; i < 128; i++) {
    EXPECT_EQ(in_buf[i], out_[i + 128]);
  }
}

TEST_F(KaeProviderRsaTest, TestFailedSigning) {
  TestCallbacks cb;
  KaePrivateKeyConnection op(cb, *dispatcher_, handle_, bssl::UpRef(pkey_));
  libuadk_->kaeDoRSA_return_value_ = WD_STATUS_FAILED; // "fail" the decryption

  res_ = privateKeySignForTest(&op, nullptr, nullptr, max_out_len_, SSL_SIGN_RSA_PSS_SHA256, in_,
                               in_len_);
  EXPECT_EQ(res_, ssl_private_key_failure);
}

TEST_F(KaeProviderRsaTest, TestKaeDeviceInit) {
  std::string* key_file = new std::string(TestEnvironment::substitute(
      "{{ test_rundir }}/contrib/kae/private_key_providers/test/test_data/rsa-2048.pem"));
  envoy::config::core::v3::DataSource* private_key = new envoy::config::core::v3::DataSource();
  private_key->set_allocated_filename(key_file);

  envoy::extensions::private_key_providers::kae::v3alpha::KaePrivateKeyMethodConfig conf;
  conf.set_allocated_private_key(private_key);

  // no device found
  libuadk_->kaeGetNumInstances_return_value_ = WD_STATUS_FAILED;
  Ssl::PrivateKeyMethodProviderSharedPtr provider =
      std::make_shared<KaePrivateKeyMethodProvider>(conf, factory_context_, libuadk_);
  EXPECT_EQ(provider->isAvailable(), false);
}

} // namespace
} // namespace Kae
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
