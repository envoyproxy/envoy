#include <memory>
#include <set>
#include <string>
#include <vector>

#include "source/extensions/transport_sockets/tls/private_key/private_key_manager_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/server/transport_socket_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "contrib/qat/private_key_providers/source/qat_private_key_provider.h"
#include "fake_factory.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace Qat {

// Testing interface
ssl_private_key_result_t privateKeySignForTest(QatPrivateKeyConnection* ops, uint8_t* out,
                                               size_t* out_len, size_t max_out,
                                               uint16_t signature_algorithm, const uint8_t* in,
                                               size_t in_len);
ssl_private_key_result_t privateKeyDecryptForTest(QatPrivateKeyConnection* ops, uint8_t* out,
                                                  size_t* out_len, size_t max_out,
                                                  const uint8_t* in, size_t in_len);
ssl_private_key_result_t privateKeyCompleteForTest(QatPrivateKeyConnection* ops,
                                                   QatContext* qat_ctx, uint8_t* out,
                                                   size_t* out_len, size_t max_out);

namespace {

class TestCallbacks : public Envoy::Ssl::PrivateKeyConnectionCallbacks {
public:
  void onPrivateKeyMethodComplete() override{

  };
};

class FakeSingletonManager : public Singleton::Manager {
public:
  FakeSingletonManager(LibQatCryptoSharedPtr libqat) : libqat_(libqat) {}
  Singleton::InstanceSharedPtr get(const std::string&, Singleton::SingletonFactoryCb) override {
    return std::make_shared<QatManager>(libqat_);
  }

private:
  LibQatCryptoSharedPtr libqat_;
};

class QatProviderTest : public testing::Test {
protected:
  QatProviderTest()
      : api_(Api::createApiForTest(store_, time_system_)),
        dispatcher_(api_->allocateDispatcher("test_thread")),
        libqat_(std::make_shared<FakeLibQatCryptoImpl>()), fsm_(libqat_) {
    handle_.setLibqat(libqat_);
    ON_CALL(factory_context_.server_context_, api()).WillByDefault(testing::ReturnRef(*api_));
    ON_CALL(factory_context_.server_context_, singletonManager())
        .WillByDefault(testing::ReturnRef(fsm_));
  }

  Stats::TestUtil::TestStore store_;
  Api::ApiPtr api_;
  Event::SimulatedTimeSystem time_system_;
  Event::DispatcherPtr dispatcher_;
  QatHandle handle_;
  std::shared_ptr<FakeLibQatCryptoImpl> libqat_;
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

class QatProviderRsaTest : public QatProviderTest {
public:
  bssl::UniquePtr<EVP_PKEY> makeRsaKey() {
    std::string file = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
        "{{ test_rundir }}/contrib/qat/private_key_providers/test/test_data/rsa-2048.pem"));
    bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(file.data(), file.size()));

    bssl::UniquePtr<EVP_PKEY> key(EVP_PKEY_new());

    RSA* rsa = PEM_read_bio_RSAPrivateKey(bio.get(), nullptr, nullptr, nullptr);
    RELEASE_ASSERT(rsa != nullptr, "PEM_read_bio_RSAPrivateKey failed.");
    RELEASE_ASSERT(1 == EVP_PKEY_assign_RSA(key.get(), rsa), "EVP_PKEY_assign_RSA failed.");
    return key;
  }

protected:
  QatProviderRsaTest() : pkey_(makeRsaKey()) {
    rsa_ = EVP_PKEY_get0_RSA(pkey_.get());
    libqat_->setRsaKey(rsa_);
  }
  bssl::UniquePtr<EVP_PKEY> pkey_{};
  RSA* rsa_{};
};

TEST_F(QatProviderRsaTest, TestRsaPkcs1Signing) {
  // PKCS #1 v1.5.
  TestCallbacks cb;
  QatPrivateKeyConnection op(cb, *dispatcher_, handle_, bssl::UpRef(pkey_));

  res_ = privateKeySignForTest(&op, nullptr, nullptr, max_out_len_, SSL_SIGN_RSA_PKCS1_SHA256, in_,
                               in_len_);
  EXPECT_EQ(res_, ssl_private_key_retry);

  // When we called the sign operation, QAT context was registered. Ask it back so we can provide it
  // to complete() function.
  QatContext* ctx = static_cast<QatContext*>(libqat_->getQatContextPointer());
  EXPECT_NE(ctx, nullptr);

  ctx->setOpStatus(CPA_STATUS_RETRY);
  res_ = privateKeyCompleteForTest(&op, ctx, nullptr, nullptr, max_out_len_);
  EXPECT_EQ(res_, ssl_private_key_retry);

  libqat_->triggerDecrypt();
  ctx->setOpStatus(CPA_STATUS_SUCCESS);

  res_ = privateKeyCompleteForTest(&op, ctx, out_, &out_len_, max_out_len_);
  EXPECT_EQ(res_, ssl_private_key_success);
  EXPECT_NE(out_len_, 0);

  // Check the signature in out_.
  RSA* rsa = EVP_PKEY_get0_RSA(pkey_.get());

  uint8_t buf[max_out_len_] = {0};
  size_t buf_len = 0;
  EXPECT_EQ(RSA_verify_raw(rsa, &buf_len, buf, max_out_len_, out_, out_len_, RSA_PKCS1_PADDING), 1);
}

TEST_F(QatProviderRsaTest, TestRsaPSSSigning) {
  // RSA-PSS
  TestCallbacks cb;
  QatPrivateKeyConnection op(cb, *dispatcher_, handle_, bssl::UpRef(pkey_));

  res_ = privateKeySignForTest(&op, nullptr, nullptr, max_out_len_, SSL_SIGN_RSA_PSS_SHA256, in_,
                               in_len_);
  EXPECT_EQ(res_, ssl_private_key_retry);

  // When we called the sign operation, QAT context was registered. Ask it back so we can provide it
  // to complete() function.
  QatContext* ctx = static_cast<QatContext*>(libqat_->getQatContextPointer());
  EXPECT_NE(ctx, nullptr);

  ctx->setOpStatus(CPA_STATUS_RETRY);
  res_ = privateKeyCompleteForTest(&op, ctx, nullptr, nullptr, max_out_len_);
  EXPECT_EQ(res_, ssl_private_key_retry);

  libqat_->triggerDecrypt();
  ctx->setOpStatus(CPA_STATUS_SUCCESS);

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

TEST_F(QatProviderRsaTest, TestRsaDecryption) {
  TestCallbacks cb;
  QatPrivateKeyConnection op(cb, *dispatcher_, handle_, bssl::UpRef(pkey_));
  RSA* rsa = EVP_PKEY_get0_RSA(pkey_.get());
  uint8_t encrypt_buf[256] = {0x0};                                              // RSA_size()
  uint8_t in_buf[128] = {'l', 'o', 'r', 'e', 'm', ' ', 'i', 'p', 's', 'u', 'm'}; // RSA_size() / 2

  int ret = RSA_public_encrypt(128, in_buf, encrypt_buf, rsa, RSA_PKCS1_PADDING);
  EXPECT_EQ(ret, RSA_size(rsa));

  res_ = privateKeyDecryptForTest(&op, nullptr, nullptr, max_out_len_, encrypt_buf, RSA_size(rsa));
  EXPECT_EQ(res_, ssl_private_key_retry);

  QatContext* ctx = static_cast<QatContext*>(libqat_->getQatContextPointer());
  EXPECT_NE(ctx, nullptr);

  ctx->setOpStatus(CPA_STATUS_RETRY);
  res_ = privateKeyCompleteForTest(&op, ctx, nullptr, nullptr, max_out_len_);
  EXPECT_EQ(res_, ssl_private_key_retry);

  libqat_->triggerDecrypt();
  ctx->setOpStatus(CPA_STATUS_SUCCESS);

  res_ = privateKeyCompleteForTest(&op, ctx, out_, &out_len_, max_out_len_);
  EXPECT_EQ(res_, ssl_private_key_success);
  EXPECT_EQ(out_len_, 256);

  // The padding is for the first 128 bytes and the data starts after that.
  for (size_t i = 0; i < 128; i++) {
    EXPECT_EQ(in_buf[i], out_[i + 128]);
  }
}

TEST_F(QatProviderRsaTest, TestFailedSigning) {
  TestCallbacks cb;
  QatPrivateKeyConnection op(cb, *dispatcher_, handle_, bssl::UpRef(pkey_));
  libqat_->cpaCyRsaDecrypt_return_value_ = CPA_STATUS_FAIL; // "fail" the decryption

  res_ = privateKeySignForTest(&op, nullptr, nullptr, max_out_len_, SSL_SIGN_RSA_PSS_SHA256, in_,
                               in_len_);
  EXPECT_EQ(res_, ssl_private_key_failure);
}

TEST_F(QatProviderRsaTest, TestQatDeviceInit) {
  std::string* key_file = new std::string(TestEnvironment::substitute(
      "{{ test_rundir }}/contrib/qat/private_key_providers/test/test_data/rsa-2048.pem"));
  envoy::config::core::v3::DataSource* private_key = new envoy::config::core::v3::DataSource();
  private_key->set_allocated_filename(key_file);

  envoy::extensions::private_key_providers::qat::v3alpha::QatPrivateKeyMethodConfig conf;
  conf.set_allocated_private_key(private_key);

  // no device found
  libqat_->icpSalUserStart_return_value_ = CPA_STATUS_FAIL;
  Ssl::PrivateKeyMethodProviderSharedPtr provider =
      std::make_shared<QatPrivateKeyMethodProvider>(conf, factory_context_, libqat_);
  EXPECT_EQ(provider->isAvailable(), false);
  delete private_key;
}

} // namespace
} // namespace Qat
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
