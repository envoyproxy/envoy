#include <memory>
#include <string>
#include <vector>

#include "source/extensions/transport_sockets/tls/private_key/private_key_manager_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "contrib/cryptomb/private_key_providers/source/cryptomb_private_key_provider.h"
#include "fake_factory.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace CryptoMb {

// Testing interface
ssl_private_key_result_t privateKeyCompleteForTest(CryptoMbPrivateKeyConnection* ops, uint8_t* out,
                                                   size_t* out_len, size_t max_out);
ssl_private_key_result_t ecdsaPrivateKeySignForTest(CryptoMbPrivateKeyConnection* ops, uint8_t* out,
                                                    size_t* out_len, size_t max_out,
                                                    uint16_t signature_algorithm, const uint8_t* in,
                                                    size_t in_len);
ssl_private_key_result_t rsaPrivateKeySignForTest(CryptoMbPrivateKeyConnection* ops, uint8_t* out,
                                                  size_t* out_len, size_t max_out,
                                                  uint16_t signature_algorithm, const uint8_t* in,
                                                  size_t in_len);
ssl_private_key_result_t rsaPrivateKeyDecryptForTest(CryptoMbPrivateKeyConnection* ops,
                                                     uint8_t* out, size_t* out_len, size_t max_out,
                                                     const uint8_t* in, size_t in_len);

namespace {

class TestCallbacks : public Envoy::Ssl::PrivateKeyConnectionCallbacks {
public:
  void onPrivateKeyMethodComplete() override{

  };
};

class CryptoMbProviderTest : public testing::Test {
protected:
  CryptoMbProviderTest()
      : api_(Api::createApiForTest(store_, time_system_)),
        dispatcher_(api_->allocateDispatcher("test_thread")),
        fakeIpp_(std::make_shared<FakeIppCryptoImpl>(true)),
        stats_(generateCryptoMbStats("cryptomb", store_)) {}

  bssl::UniquePtr<EVP_PKEY> makeRsaKey() {
    std::string file = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
        "{{ test_rundir }}/contrib/cryptomb/private_key_providers/test/test_data/rsa-1024.pem"));
    bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(file.data(), file.size()));

    bssl::UniquePtr<EVP_PKEY> key(EVP_PKEY_new());

    RSA* rsa = PEM_read_bio_RSAPrivateKey(bio.get(), nullptr, nullptr, nullptr);
    RELEASE_ASSERT(rsa != nullptr, "PEM_read_bio_RSAPrivateKey failed.");
    RELEASE_ASSERT(1 == EVP_PKEY_assign_RSA(key.get(), rsa), "EVP_PKEY_assign_RSA failed.");
    return key;
  }

  bssl::UniquePtr<EVP_PKEY> makeEcdsaKey() {
    std::string file = TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(
        "{{ test_rundir "
        "}}/contrib/cryptomb/private_key_providers/test/test_data/ecdsa-p256.pem"));
    bssl::UniquePtr<BIO> bio(BIO_new_mem_buf(file.data(), file.size()));

    bssl::UniquePtr<EVP_PKEY> key(EVP_PKEY_new());

    EC_KEY* ec = PEM_read_bio_ECPrivateKey(bio.get(), nullptr, nullptr, nullptr);

    RELEASE_ASSERT(ec != nullptr, "PEM_read_bio_ECPrivateKey failed.");
    RELEASE_ASSERT(1 == EVP_PKEY_assign_EC_KEY(key.get(), ec), "EVP_PKEY_assign_EC_KEY failed.");
    return key;
  }

  Stats::TestUtil::TestStore store_;
  Api::ApiPtr api_;
  Event::SimulatedTimeSystem time_system_;
  Event::DispatcherPtr dispatcher_;
  std::shared_ptr<FakeIppCryptoImpl> fakeIpp_;
  CryptoMbStats stats_;

  // Result of an operation.
  ssl_private_key_result_t res_;

  // A size for signing and decryption operation input chosen for tests.
  static constexpr size_t IN_LEN = 32;
  // Test input bytes for signing and decryption chosen for tests.
  static constexpr uint8_t IN[IN_LEN] = {0x7f};

  // Maximum size of out_ in all tests cases.
  static constexpr size_t MAX_OUT_LEN = 128;
  uint8_t out_[MAX_OUT_LEN] = {0};

  // Size of output in out_ from an operation.
  size_t out_len_ = 0;

  const std::string QUEUE_SIZE_HISTOGRAM_NAME = "cryptomb.rsa_queue_sizes";
};

class CryptoMbProviderRsaTest : public CryptoMbProviderTest {
protected:
  CryptoMbProviderRsaTest()
      : queue_(std::chrono::milliseconds(200), KeyType::Rsa, 1024, fakeIpp_, *dispatcher_, stats_),
        pkey_(makeRsaKey()) {
    RSA* rsa = EVP_PKEY_get0_RSA(pkey_.get());
    fakeIpp_->setRsaKey(rsa);
  }
  CryptoMbQueue queue_;
  bssl::UniquePtr<EVP_PKEY> pkey_;
};

class CryptoMbProviderEcdsaTest : public CryptoMbProviderTest {
protected:
  CryptoMbProviderEcdsaTest()
      : queue_(std::chrono::milliseconds(200), KeyType::Ec, 256, fakeIpp_, *dispatcher_, stats_),
        pkey_(makeEcdsaKey()) {}
  CryptoMbQueue queue_;
  bssl::UniquePtr<EVP_PKEY> pkey_;
};

TEST_F(CryptoMbProviderEcdsaTest, TestEcdsaSigning) {
  TestCallbacks cb;
  CryptoMbPrivateKeyConnection op(cb, *dispatcher_, bssl::UpRef(pkey_), queue_);
  res_ = ecdsaPrivateKeySignForTest(&op, out_, &out_len_, MAX_OUT_LEN,
                                    SSL_SIGN_ECDSA_SECP256R1_SHA256, IN, IN_LEN);
  EXPECT_EQ(res_, ssl_private_key_success);
}

TEST_F(CryptoMbProviderRsaTest, TestRsaPkcs1Signing) {
  // Initialize connections.
  TestCallbacks cbs[CryptoMbQueue::MULTIBUFF_BATCH];
  std::vector<std::unique_ptr<CryptoMbPrivateKeyConnection>> connections;
  for (auto& cb : cbs) {
    connections.push_back(std::make_unique<CryptoMbPrivateKeyConnection>(
        cb, *dispatcher_, bssl::UpRef(pkey_), queue_));
  }

  // Create MULTIBUFF_BATCH amount of signing operations.
  for (uint32_t i = 0; i < CryptoMbQueue::MULTIBUFF_BATCH; i++) {
    // Create request.
    res_ = rsaPrivateKeySignForTest(connections[i].get(), nullptr, nullptr, MAX_OUT_LEN,
                                    SSL_SIGN_RSA_PKCS1_SHA256, IN, IN_LEN);
    EXPECT_EQ(res_, ssl_private_key_retry);

    // No processing done after first requests.
    // After the last request, the status is set only from the event loop which is not run. This
    // should still be "retry", the cryptographic result is present anyway.
    res_ = privateKeyCompleteForTest(connections[i].get(), nullptr, nullptr, MAX_OUT_LEN);
    EXPECT_EQ(res_, ssl_private_key_retry);
  }

  // Timeout does not have to be triggered when queue is at maximum size.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  res_ = privateKeyCompleteForTest(connections[0].get(), out_, &out_len_, MAX_OUT_LEN);
  EXPECT_EQ(res_, ssl_private_key_success);
  EXPECT_NE(out_len_, 0);
}

TEST_F(CryptoMbProviderRsaTest, TestRsaPssSigning) {
  // Initialize connections.
  TestCallbacks cbs[CryptoMbQueue::MULTIBUFF_BATCH];
  std::vector<std::unique_ptr<CryptoMbPrivateKeyConnection>> connections;
  for (auto& cb : cbs) {
    connections.push_back(std::make_unique<CryptoMbPrivateKeyConnection>(
        cb, *dispatcher_, bssl::UpRef(pkey_), queue_));
  }

  // Create MULTIBUFF_BATCH amount of signing operations.
  for (uint32_t i = 0; i < CryptoMbQueue::MULTIBUFF_BATCH; i++) {
    // Create request.
    res_ = rsaPrivateKeySignForTest(connections[i].get(), nullptr, nullptr, MAX_OUT_LEN,
                                    SSL_SIGN_RSA_PSS_SHA256, IN, IN_LEN);
    EXPECT_EQ(res_, ssl_private_key_retry);

    // No processing done after first requests.
    // After the last request, the status is set only from the event loop which is not run. This
    // should still be "retry", the cryptographic result is present anyway.
    res_ = privateKeyCompleteForTest(connections[i].get(), nullptr, nullptr, MAX_OUT_LEN);
    EXPECT_EQ(res_, ssl_private_key_retry);
  }

  // Timeout does not have to be triggered when queue is at maximum size.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  res_ = privateKeyCompleteForTest(connections[0].get(), out_, &out_len_, MAX_OUT_LEN);
  EXPECT_EQ(res_, ssl_private_key_success);
  EXPECT_NE(out_len_, 0);
}

TEST_F(CryptoMbProviderRsaTest, TestRsaDecrypt) {
  // Initialize connections.
  TestCallbacks cbs[CryptoMbQueue::MULTIBUFF_BATCH];
  std::vector<std::unique_ptr<CryptoMbPrivateKeyConnection>> connections;
  for (auto& cb : cbs) {
    connections.push_back(std::make_unique<CryptoMbPrivateKeyConnection>(
        cb, *dispatcher_, bssl::UpRef(pkey_), queue_));
  }

  // Create MULTIBUFF_BATCH amount of decryption operations.
  for (uint32_t i = 0; i < CryptoMbQueue::MULTIBUFF_BATCH; i++) {
    // Create request.
    res_ = rsaPrivateKeyDecryptForTest(connections[i].get(), nullptr, nullptr, MAX_OUT_LEN, IN,
                                       IN_LEN);
    EXPECT_EQ(res_, ssl_private_key_retry);

    // No processing done after first requests.
    // After the last request, the status is set only from the event loop which is not run. This
    // should still be "retry", the cryptographic result is present anyway.
    res_ = privateKeyCompleteForTest(connections[i].get(), nullptr, nullptr, MAX_OUT_LEN);
    EXPECT_EQ(res_, ssl_private_key_retry);
  }

  // Timeout does not have to be triggered when queue is at maximum size.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  res_ = privateKeyCompleteForTest(connections[0].get(), out_, &out_len_, MAX_OUT_LEN);
  EXPECT_EQ(res_, ssl_private_key_success);
  EXPECT_NE(out_len_, 0);
}

TEST_F(CryptoMbProviderTest, TestErrors) {
  bssl::UniquePtr<EVP_PKEY> pkey = makeEcdsaKey();
  bssl::UniquePtr<EVP_PKEY> rsa_pkey = makeRsaKey();

  CryptoMbQueue ec_queue(std::chrono::milliseconds(200), KeyType::Ec, 256, fakeIpp_, *dispatcher_,
                         stats_);
  CryptoMbQueue rsa_queue(std::chrono::milliseconds(200), KeyType::Rsa, 1024, fakeIpp_,
                          *dispatcher_, stats_);

  TestCallbacks cb;

  CryptoMbPrivateKeyConnection op_ec(cb, *dispatcher_, bssl::UpRef(pkey), ec_queue);
  CryptoMbPrivateKeyConnection op_rsa(cb, *dispatcher_, bssl::UpRef(rsa_pkey), rsa_queue);

  // no operation defined
  res_ = ecdsaPrivateKeySignForTest(nullptr, nullptr, nullptr, MAX_OUT_LEN,
                                    SSL_SIGN_ECDSA_SECP256R1_SHA256, IN, IN_LEN);
  EXPECT_EQ(res_, ssl_private_key_failure);
  res_ = rsaPrivateKeySignForTest(nullptr, nullptr, nullptr, MAX_OUT_LEN, SSL_SIGN_RSA_PSS_SHA256,
                                  IN, IN_LEN);
  EXPECT_EQ(res_, ssl_private_key_failure);
  res_ = rsaPrivateKeyDecryptForTest(nullptr, nullptr, nullptr, MAX_OUT_LEN, IN, IN_LEN);
  EXPECT_EQ(res_, ssl_private_key_failure);

  // Unknown signature algorithm
  res_ = ecdsaPrivateKeySignForTest(&op_ec, nullptr, nullptr, MAX_OUT_LEN, 1234, IN, IN_LEN);
  EXPECT_EQ(res_, ssl_private_key_failure);
  res_ = rsaPrivateKeySignForTest(&op_rsa, nullptr, nullptr, MAX_OUT_LEN, 1234, IN, IN_LEN);
  EXPECT_EQ(res_, ssl_private_key_failure);

  // Wrong signature algorithm
  res_ = ecdsaPrivateKeySignForTest(&op_ec, nullptr, nullptr, MAX_OUT_LEN, SSL_SIGN_RSA_PSS_SHA256,
                                    IN, IN_LEN);
  EXPECT_EQ(res_, ssl_private_key_failure);
  res_ = rsaPrivateKeySignForTest(&op_rsa, nullptr, nullptr, MAX_OUT_LEN,
                                  SSL_SIGN_ECDSA_SECP256R1_SHA256, IN, IN_LEN);
  EXPECT_EQ(res_, ssl_private_key_failure);

  // Wrong operation type
  res_ = ecdsaPrivateKeySignForTest(&op_rsa, nullptr, nullptr, MAX_OUT_LEN,
                                    SSL_SIGN_ECDSA_SECP256R1_SHA256, IN, IN_LEN);
  EXPECT_EQ(res_, ssl_private_key_failure);
  res_ = rsaPrivateKeySignForTest(&op_ec, nullptr, nullptr, MAX_OUT_LEN, SSL_SIGN_RSA_PSS_SHA256,
                                  IN, IN_LEN);
  EXPECT_EQ(res_, ssl_private_key_failure);
  res_ = rsaPrivateKeyDecryptForTest(&op_ec, nullptr, nullptr, MAX_OUT_LEN, IN, IN_LEN);
  EXPECT_EQ(res_, ssl_private_key_failure);
}

TEST_F(CryptoMbProviderRsaTest, TestRSATimer) {
  TestCallbacks cbs[2];

  // Successful operation with timer.
  CryptoMbPrivateKeyConnection op0(cbs[0], *dispatcher_, bssl::UpRef(pkey_), queue_);
  res_ = rsaPrivateKeySignForTest(&op0, nullptr, nullptr, MAX_OUT_LEN, SSL_SIGN_RSA_PKCS1_SHA256,
                                  IN, IN_LEN);
  EXPECT_EQ(res_, ssl_private_key_retry);

  res_ = privateKeyCompleteForTest(&op0, nullptr, nullptr, MAX_OUT_LEN);
  // No processing done yet after first request
  EXPECT_EQ(res_, ssl_private_key_retry);

  time_system_.advanceTimeAndRun(std::chrono::seconds(1), *dispatcher_,
                                 Event::Dispatcher::RunType::NonBlock);

  res_ = privateKeyCompleteForTest(&op0, out_, &out_len_, MAX_OUT_LEN);
  EXPECT_EQ(res_, ssl_private_key_success);
  EXPECT_NE(out_len_, 0);

  // Unsuccessful operation with timer.
  // Add crypto library errors
  fakeIpp_->injectErrors(true);

  CryptoMbPrivateKeyConnection op1(cbs[1], *dispatcher_, bssl::UpRef(pkey_), queue_);

  res_ = rsaPrivateKeySignForTest(&op1, nullptr, nullptr, MAX_OUT_LEN, SSL_SIGN_RSA_PKCS1_SHA256,
                                  IN, IN_LEN);
  EXPECT_EQ(res_, ssl_private_key_retry);

  res_ = privateKeyCompleteForTest(&op1, nullptr, nullptr, MAX_OUT_LEN);
  // No processing done yet after first request
  EXPECT_EQ(res_, ssl_private_key_retry);

  time_system_.advanceTimeAndRun(std::chrono::seconds(1), *dispatcher_,
                                 Event::Dispatcher::RunType::NonBlock);

  res_ = privateKeyCompleteForTest(&op1, out_, &out_len_, MAX_OUT_LEN);
  EXPECT_EQ(res_, ssl_private_key_failure);
}

TEST_F(CryptoMbProviderRsaTest, TestRSAQueueSizeStatistics) {
  // Initialize connections.
  TestCallbacks cbs[CryptoMbQueue::MULTIBUFF_BATCH];
  std::vector<std::unique_ptr<CryptoMbPrivateKeyConnection>> connections;
  for (auto& cb : cbs) {
    connections.push_back(std::make_unique<CryptoMbPrivateKeyConnection>(
        cb, *dispatcher_, bssl::UpRef(pkey_), queue_));
  }

  // Increment all but the last queue size once inside the loop.
  for (uint32_t i = 1; i < CryptoMbQueue::MULTIBUFF_BATCH; i++) {
    // Create correct amount of signing operations for current index.
    for (uint32_t j = 0; j < i; j++) {
      res_ = rsaPrivateKeySignForTest(connections[j].get(), nullptr, nullptr, MAX_OUT_LEN,
                                      SSL_SIGN_RSA_PKCS1_SHA256, IN, IN_LEN);
      EXPECT_EQ(res_, ssl_private_key_retry);
    }

    time_system_.advanceTimeAndRun(std::chrono::seconds(1), *dispatcher_,
                                   Event::Dispatcher::RunType::NonBlock);

    out_len_ = 0;
    res_ = privateKeyCompleteForTest(connections[0].get(), out_, &out_len_, MAX_OUT_LEN);
    EXPECT_EQ(res_, ssl_private_key_success);
    EXPECT_NE(out_len_, 0);

    // Check that current queue size is recorded.
    std::vector<uint64_t> histogram_values(store_.histogramValues(QUEUE_SIZE_HISTOGRAM_NAME, true));
    EXPECT_EQ(histogram_values.size(), 1);
    EXPECT_EQ(histogram_values[0], i);
  }

  // Increment last queue size once.
  // Create an amount of signing operations equal to maximum queue size.
  for (uint32_t j = 0; j < CryptoMbQueue::MULTIBUFF_BATCH; j++) {
    res_ = rsaPrivateKeySignForTest(connections[j].get(), nullptr, nullptr, MAX_OUT_LEN,
                                    SSL_SIGN_RSA_PKCS1_SHA256, IN, IN_LEN);
    EXPECT_EQ(res_, ssl_private_key_retry);
  }

  // Timeout does not have to be triggered when queue is at maximum size.
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  out_len_ = 0;
  res_ = privateKeyCompleteForTest(connections[0].get(), out_, &out_len_, MAX_OUT_LEN);
  EXPECT_EQ(res_, ssl_private_key_success);
  EXPECT_NE(out_len_, 0);

  // Check that last queue size is recorded.
  std::vector<uint64_t> histogram_values(store_.histogramValues(QUEUE_SIZE_HISTOGRAM_NAME, true));
  EXPECT_EQ(histogram_values.size(), 1);
  EXPECT_EQ(histogram_values[0], CryptoMbQueue::MULTIBUFF_BATCH);
}

} // namespace
} // namespace CryptoMb
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
