#include <memory>
#include <string>
#include <vector>

#include "source/extensions/transport_sockets/tls/private_key/private_key_manager_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
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

constexpr absl::string_view kStatsPrefix = "cryptomb";
constexpr absl::string_view kQueueSizeStatPrefix = "rsa_queue_size_";

class TestCallbacks : public Envoy::Ssl::PrivateKeyConnectionCallbacks {
public:
  void onPrivateKeyMethodComplete() override{

  };
};

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

TEST(CryptoMbProviderTest, TestEcdsaSigning) {
  Event::SimulatedTimeSystem time_system;
  Stats::TestUtil::TestStore server_stats_store;
  Api::ApiPtr api = Api::createApiForTest(server_stats_store, time_system);
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("test_thread"));
  bssl::UniquePtr<EVP_PKEY> pkey = makeEcdsaKey();
  std::shared_ptr<FakeIppCryptoImpl> fakeIpp = std::make_shared<FakeIppCryptoImpl>(true);
  CryptoMbStats stats(server_stats_store, CryptoMbQueue::MULTIBUFF_BATCH, kStatsPrefix,
                      kQueueSizeStatPrefix);

  CryptoMbQueue queue(std::chrono::milliseconds(200), KeyType::Ec, 256, fakeIpp, *dispatcher,
                      stats);

  size_t in_len = 32;
  uint8_t in[32] = {0x7f};
  size_t out_len = 0;
  uint8_t out[128] = {0};

  ssl_private_key_result_t res;
  TestCallbacks cbs;

  // First request
  CryptoMbPrivateKeyConnection op(cbs, *dispatcher, bssl::UpRef(pkey), queue);
  res = ecdsaPrivateKeySignForTest(&op, out, &out_len, 128, SSL_SIGN_ECDSA_SECP256R1_SHA256, in,
                                   in_len);
  EXPECT_EQ(res, ssl_private_key_success);
}

TEST(CryptoMbProviderTest, TestRsaPkcs1Signing) {
  Event::SimulatedTimeSystem time_system;
  Stats::TestUtil::TestStore server_stats_store;
  Api::ApiPtr api = Api::createApiForTest(server_stats_store, time_system);
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("test_thread"));
  bssl::UniquePtr<EVP_PKEY> pkey = makeRsaKey();
  std::shared_ptr<FakeIppCryptoImpl> fakeIpp = std::make_shared<FakeIppCryptoImpl>(true);
  RSA* rsa = EVP_PKEY_get0_RSA(pkey.get());
  const BIGNUM *e, *n, *d;
  RSA_get0_key(rsa, &n, &e, &d);
  fakeIpp->setRsaKey(n, e, d);
  CryptoMbStats stats(server_stats_store, CryptoMbQueue::MULTIBUFF_BATCH, kStatsPrefix,
                      kQueueSizeStatPrefix);

  CryptoMbQueue queue(std::chrono::milliseconds(200), KeyType::Rsa, 1024, fakeIpp, *dispatcher,
                      stats);

  size_t in_len = 32;
  uint8_t in[32] = {0x7f};

  ssl_private_key_result_t res;
  TestCallbacks cbs[8];

  // First request
  CryptoMbPrivateKeyConnection op0(cbs[0], *dispatcher, bssl::UpRef(pkey), queue);
  res =
      rsaPrivateKeySignForTest(&op0, nullptr, nullptr, 128, SSL_SIGN_RSA_PKCS1_SHA256, in, in_len);
  EXPECT_EQ(res, ssl_private_key_retry);

  res = privateKeyCompleteForTest(&op0, nullptr, nullptr, 128);
  // No processing done yet after first request
  EXPECT_EQ(res, ssl_private_key_retry);

  // Second request
  CryptoMbPrivateKeyConnection op1(cbs[1], *dispatcher, bssl::UpRef(pkey), queue);
  res =
      rsaPrivateKeySignForTest(&op1, nullptr, nullptr, 128, SSL_SIGN_RSA_PKCS1_SHA256, in, in_len);
  EXPECT_EQ(res, ssl_private_key_retry);

  res = privateKeyCompleteForTest(&op1, nullptr, nullptr, 128);
  // No processing done yet after second request
  EXPECT_EQ(res, ssl_private_key_retry);

  // Six more requests
  CryptoMbPrivateKeyConnection op2(cbs[2], *dispatcher, bssl::UpRef(pkey), queue);
  CryptoMbPrivateKeyConnection op3(cbs[3], *dispatcher, bssl::UpRef(pkey), queue);
  CryptoMbPrivateKeyConnection op4(cbs[4], *dispatcher, bssl::UpRef(pkey), queue);
  CryptoMbPrivateKeyConnection op5(cbs[5], *dispatcher, bssl::UpRef(pkey), queue);
  CryptoMbPrivateKeyConnection op6(cbs[6], *dispatcher, bssl::UpRef(pkey), queue);
  CryptoMbPrivateKeyConnection op7(cbs[7], *dispatcher, bssl::UpRef(pkey), queue);
  res =
      rsaPrivateKeySignForTest(&op2, nullptr, nullptr, 128, SSL_SIGN_RSA_PKCS1_SHA256, in, in_len);
  EXPECT_EQ(res, ssl_private_key_retry);
  res =
      rsaPrivateKeySignForTest(&op3, nullptr, nullptr, 128, SSL_SIGN_RSA_PKCS1_SHA256, in, in_len);
  EXPECT_EQ(res, ssl_private_key_retry);
  res =
      rsaPrivateKeySignForTest(&op4, nullptr, nullptr, 128, SSL_SIGN_RSA_PKCS1_SHA256, in, in_len);
  EXPECT_EQ(res, ssl_private_key_retry);
  res =
      rsaPrivateKeySignForTest(&op5, nullptr, nullptr, 128, SSL_SIGN_RSA_PKCS1_SHA256, in, in_len);
  EXPECT_EQ(res, ssl_private_key_retry);
  res =
      rsaPrivateKeySignForTest(&op6, nullptr, nullptr, 128, SSL_SIGN_RSA_PKCS1_SHA256, in, in_len);
  EXPECT_EQ(res, ssl_private_key_retry);
  res =
      rsaPrivateKeySignForTest(&op7, nullptr, nullptr, 128, SSL_SIGN_RSA_PKCS1_SHA256, in, in_len);
  EXPECT_EQ(res, ssl_private_key_retry);

  size_t out_len = 0;
  uint8_t out[128] = {0};

  res = privateKeyCompleteForTest(&op0, out, &out_len, 128);
  // Since the status is set only from the event loop (which is not run) this should be still
  // "retry". The cryptographic result is present anyway.
  EXPECT_EQ(res, ssl_private_key_retry);

  op0.mb_ctx_->setStatus(RequestStatus::Success);
  res = privateKeyCompleteForTest(&op0, out, &out_len, 128);
  EXPECT_EQ(res, ssl_private_key_success);
  EXPECT_NE(out_len, 0);
}

TEST(CryptoMbProviderTest, TestRsaPssSigning) {
  Event::SimulatedTimeSystem time_system;
  Stats::TestUtil::TestStore server_stats_store;
  Api::ApiPtr api = Api::createApiForTest(server_stats_store, time_system);
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("test_thread"));
  bssl::UniquePtr<EVP_PKEY> pkey = makeRsaKey();
  std::shared_ptr<FakeIppCryptoImpl> fakeIpp = std::make_shared<FakeIppCryptoImpl>(true);
  RSA* rsa = EVP_PKEY_get0_RSA(pkey.get());
  const BIGNUM *e, *n, *d;
  RSA_get0_key(rsa, &n, &e, &d);
  fakeIpp->setRsaKey(n, e, d);
  CryptoMbStats stats(server_stats_store, CryptoMbQueue::MULTIBUFF_BATCH, kStatsPrefix,
                      kQueueSizeStatPrefix);

  CryptoMbQueue queue(std::chrono::milliseconds(200), KeyType::Rsa, 1024, fakeIpp, *dispatcher,
                      stats);

  size_t in_len = 32;
  uint8_t in[32] = {0x7f};

  ssl_private_key_result_t res;
  TestCallbacks cbs[8];

  // First request
  CryptoMbPrivateKeyConnection op0(cbs[0], *dispatcher, bssl::UpRef(pkey), queue);
  res = rsaPrivateKeySignForTest(&op0, nullptr, nullptr, 128, SSL_SIGN_RSA_PSS_SHA256, in, in_len);
  EXPECT_EQ(res, ssl_private_key_retry);

  res = privateKeyCompleteForTest(&op0, nullptr, nullptr, 128);
  // No processing done yet after first request
  EXPECT_EQ(res, ssl_private_key_retry);

  // Second request
  CryptoMbPrivateKeyConnection op1(cbs[1], *dispatcher, bssl::UpRef(pkey), queue);
  res = rsaPrivateKeySignForTest(&op1, nullptr, nullptr, 128, SSL_SIGN_RSA_PSS_SHA256, in, in_len);
  EXPECT_EQ(res, ssl_private_key_retry);

  res = privateKeyCompleteForTest(&op1, nullptr, nullptr, 128);
  // No processing done yet after second request
  EXPECT_EQ(res, ssl_private_key_retry);

  // Six more requests
  CryptoMbPrivateKeyConnection op2(cbs[2], *dispatcher, bssl::UpRef(pkey), queue);
  CryptoMbPrivateKeyConnection op3(cbs[3], *dispatcher, bssl::UpRef(pkey), queue);
  CryptoMbPrivateKeyConnection op4(cbs[4], *dispatcher, bssl::UpRef(pkey), queue);
  CryptoMbPrivateKeyConnection op5(cbs[5], *dispatcher, bssl::UpRef(pkey), queue);
  CryptoMbPrivateKeyConnection op6(cbs[6], *dispatcher, bssl::UpRef(pkey), queue);
  CryptoMbPrivateKeyConnection op7(cbs[7], *dispatcher, bssl::UpRef(pkey), queue);
  res = rsaPrivateKeySignForTest(&op2, nullptr, nullptr, 128, SSL_SIGN_RSA_PSS_SHA256, in, in_len);
  EXPECT_EQ(res, ssl_private_key_retry);
  res = rsaPrivateKeySignForTest(&op3, nullptr, nullptr, 128, SSL_SIGN_RSA_PSS_SHA256, in, in_len);
  EXPECT_EQ(res, ssl_private_key_retry);
  res = rsaPrivateKeySignForTest(&op4, nullptr, nullptr, 128, SSL_SIGN_RSA_PSS_SHA256, in, in_len);
  EXPECT_EQ(res, ssl_private_key_retry);
  res = rsaPrivateKeySignForTest(&op5, nullptr, nullptr, 128, SSL_SIGN_RSA_PSS_SHA256, in, in_len);
  EXPECT_EQ(res, ssl_private_key_retry);
  res = rsaPrivateKeySignForTest(&op6, nullptr, nullptr, 128, SSL_SIGN_RSA_PSS_SHA256, in, in_len);
  EXPECT_EQ(res, ssl_private_key_retry);
  res = rsaPrivateKeySignForTest(&op7, nullptr, nullptr, 128, SSL_SIGN_RSA_PSS_SHA256, in, in_len);
  EXPECT_EQ(res, ssl_private_key_retry);

  size_t out_len = 0;
  uint8_t out[128] = {0};

  res = privateKeyCompleteForTest(&op0, out, &out_len, 128);
  // Since the status is set only from the event loop (which is not run) this should be still
  // "retry". The cryptographic result is present anyway.
  EXPECT_EQ(res, ssl_private_key_retry);

  op0.mb_ctx_->setStatus(RequestStatus::Success);
  res = privateKeyCompleteForTest(&op0, out, &out_len, 128);
  EXPECT_EQ(res, ssl_private_key_success);
  EXPECT_NE(out_len, 0);
}

TEST(CryptoMbProviderTest, TestRsaDecrypt) {
  Event::SimulatedTimeSystem time_system;
  Stats::TestUtil::TestStore server_stats_store;
  Api::ApiPtr api = Api::createApiForTest(server_stats_store, time_system);
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("test_thread"));
  bssl::UniquePtr<EVP_PKEY> pkey = makeRsaKey();
  std::shared_ptr<FakeIppCryptoImpl> fakeIpp = std::make_shared<FakeIppCryptoImpl>(true);
  RSA* rsa = EVP_PKEY_get0_RSA(pkey.get());
  const BIGNUM *e, *n, *d;
  RSA_get0_key(rsa, &n, &e, &d);
  fakeIpp->setRsaKey(n, e, d);
  CryptoMbStats stats(server_stats_store, CryptoMbQueue::MULTIBUFF_BATCH, kStatsPrefix,
                      kQueueSizeStatPrefix);

  CryptoMbQueue queue(std::chrono::milliseconds(200), KeyType::Rsa, 1024, fakeIpp, *dispatcher,
                      stats);

  size_t in_len = 32;
  uint8_t in[32] = {0x7f};

  ssl_private_key_result_t res;
  TestCallbacks cbs[8];

  // First request
  CryptoMbPrivateKeyConnection op0(cbs[0], *dispatcher, bssl::UpRef(pkey), queue);
  res = rsaPrivateKeyDecryptForTest(&op0, nullptr, nullptr, 128, in, in_len);
  EXPECT_EQ(res, ssl_private_key_retry);

  res = privateKeyCompleteForTest(&op0, nullptr, nullptr, 128);
  // No processing done yet after first request
  EXPECT_EQ(res, ssl_private_key_retry);

  // Second request
  CryptoMbPrivateKeyConnection op1(cbs[1], *dispatcher, bssl::UpRef(pkey), queue);
  res = rsaPrivateKeyDecryptForTest(&op1, nullptr, nullptr, 128, in, in_len);
  EXPECT_EQ(res, ssl_private_key_retry);

  res = privateKeyCompleteForTest(&op1, nullptr, nullptr, 128);
  // No processing done yet after second request
  EXPECT_EQ(res, ssl_private_key_retry);

  // Six more requests
  CryptoMbPrivateKeyConnection op2(cbs[2], *dispatcher, bssl::UpRef(pkey), queue);
  CryptoMbPrivateKeyConnection op3(cbs[3], *dispatcher, bssl::UpRef(pkey), queue);
  CryptoMbPrivateKeyConnection op4(cbs[4], *dispatcher, bssl::UpRef(pkey), queue);
  CryptoMbPrivateKeyConnection op5(cbs[5], *dispatcher, bssl::UpRef(pkey), queue);
  CryptoMbPrivateKeyConnection op6(cbs[6], *dispatcher, bssl::UpRef(pkey), queue);
  CryptoMbPrivateKeyConnection op7(cbs[7], *dispatcher, bssl::UpRef(pkey), queue);
  res = rsaPrivateKeyDecryptForTest(&op2, nullptr, nullptr, 128, in, in_len);
  EXPECT_EQ(res, ssl_private_key_retry);
  res = rsaPrivateKeyDecryptForTest(&op3, nullptr, nullptr, 128, in, in_len);
  EXPECT_EQ(res, ssl_private_key_retry);
  res = rsaPrivateKeyDecryptForTest(&op4, nullptr, nullptr, 128, in, in_len);
  EXPECT_EQ(res, ssl_private_key_retry);
  res = rsaPrivateKeyDecryptForTest(&op5, nullptr, nullptr, 128, in, in_len);
  EXPECT_EQ(res, ssl_private_key_retry);
  res = rsaPrivateKeyDecryptForTest(&op6, nullptr, nullptr, 128, in, in_len);
  EXPECT_EQ(res, ssl_private_key_retry);
  res = rsaPrivateKeyDecryptForTest(&op7, nullptr, nullptr, 128, in, in_len);
  EXPECT_EQ(res, ssl_private_key_retry);

  size_t out_len = 0;
  uint8_t out[128] = {0};

  res = privateKeyCompleteForTest(&op0, out, &out_len, 128);
  // Since the status is set only from the event loop (which is not run) this should be still
  // "retry". The cryptographic result is present anyway.
  EXPECT_EQ(res, ssl_private_key_retry);

  op0.mb_ctx_->setStatus(RequestStatus::Success);
  res = privateKeyCompleteForTest(&op0, out, &out_len, 128);
  EXPECT_EQ(res, ssl_private_key_success);
  EXPECT_NE(out_len, 0);
}

TEST(CryptoMbProviderTest, TestErrors) {
  Event::SimulatedTimeSystem time_system;
  Stats::TestUtil::TestStore server_stats_store;
  Api::ApiPtr api = Api::createApiForTest(server_stats_store, time_system);
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("test_thread"));
  bssl::UniquePtr<EVP_PKEY> pkey = makeEcdsaKey();
  bssl::UniquePtr<EVP_PKEY> rsa_pkey = makeRsaKey();
  std::shared_ptr<FakeIppCryptoImpl> fakeIpp = std::make_shared<FakeIppCryptoImpl>(true);
  CryptoMbStats stats(server_stats_store, CryptoMbQueue::MULTIBUFF_BATCH, kStatsPrefix,
                      kQueueSizeStatPrefix);

  CryptoMbQueue ec_queue(std::chrono::milliseconds(200), KeyType::Ec, 256, fakeIpp, *dispatcher,
                         stats);
  CryptoMbQueue rsa_queue(std::chrono::milliseconds(200), KeyType::Rsa, 1024, fakeIpp, *dispatcher,
                          stats);

  size_t in_len = 32;
  uint8_t in[32] = {0x7f};

  ssl_private_key_result_t res;
  TestCallbacks cb;

  CryptoMbPrivateKeyConnection op_ec(cb, *dispatcher, bssl::UpRef(pkey), ec_queue);
  CryptoMbPrivateKeyConnection op_rsa(cb, *dispatcher, bssl::UpRef(rsa_pkey), rsa_queue);

  // no operation defined
  res = ecdsaPrivateKeySignForTest(nullptr, nullptr, nullptr, 128, SSL_SIGN_ECDSA_SECP256R1_SHA256,
                                   in, in_len);
  EXPECT_EQ(res, ssl_private_key_failure);
  res =
      rsaPrivateKeySignForTest(nullptr, nullptr, nullptr, 128, SSL_SIGN_RSA_PSS_SHA256, in, in_len);
  EXPECT_EQ(res, ssl_private_key_failure);
  res = rsaPrivateKeyDecryptForTest(nullptr, nullptr, nullptr, 128, in, in_len);
  EXPECT_EQ(res, ssl_private_key_failure);

  // Unknown signature algorithm
  res = ecdsaPrivateKeySignForTest(&op_ec, nullptr, nullptr, 128, 1234, in, in_len);
  EXPECT_EQ(res, ssl_private_key_failure);
  res = rsaPrivateKeySignForTest(&op_rsa, nullptr, nullptr, 128, 1234, in, in_len);
  EXPECT_EQ(res, ssl_private_key_failure);

  // Wrong signature algorithm
  res = ecdsaPrivateKeySignForTest(&op_ec, nullptr, nullptr, 128, SSL_SIGN_RSA_PSS_SHA256, in,
                                   in_len);
  EXPECT_EQ(res, ssl_private_key_failure);
  res = rsaPrivateKeySignForTest(&op_rsa, nullptr, nullptr, 128, SSL_SIGN_ECDSA_SECP256R1_SHA256,
                                 in, in_len);
  EXPECT_EQ(res, ssl_private_key_failure);

  // Wrong operation type
  res = ecdsaPrivateKeySignForTest(&op_rsa, nullptr, nullptr, 128, SSL_SIGN_ECDSA_SECP256R1_SHA256,
                                   in, in_len);
  EXPECT_EQ(res, ssl_private_key_failure);
  res =
      rsaPrivateKeySignForTest(&op_ec, nullptr, nullptr, 128, SSL_SIGN_RSA_PSS_SHA256, in, in_len);
  EXPECT_EQ(res, ssl_private_key_failure);
  res = rsaPrivateKeyDecryptForTest(&op_ec, nullptr, nullptr, 128, in, in_len);
  EXPECT_EQ(res, ssl_private_key_failure);
}

TEST(CryptoMbProviderTest, TestRSATimer) {
  Event::SimulatedTimeSystem time_system;
  Stats::TestUtil::TestStore server_stats_store;
  Api::ApiPtr api = Api::createApiForTest(server_stats_store, time_system);
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("test_thread"));
  bssl::UniquePtr<EVP_PKEY> pkey = makeRsaKey();
  std::shared_ptr<FakeIppCryptoImpl> fakeIpp = std::make_shared<FakeIppCryptoImpl>(true);
  RSA* rsa = EVP_PKEY_get0_RSA(pkey.get());
  const BIGNUM *e, *n, *d;
  RSA_get0_key(rsa, &n, &e, &d);
  fakeIpp->setRsaKey(n, e, d);
  CryptoMbStats stats(server_stats_store, CryptoMbQueue::MULTIBUFF_BATCH, kStatsPrefix,
                      kQueueSizeStatPrefix);

  CryptoMbQueue queue(std::chrono::milliseconds(200), KeyType::Rsa, 1024, fakeIpp, *dispatcher,
                      stats);

  size_t in_len = 32;
  uint8_t in[32] = {0x7f};

  ssl_private_key_result_t res;
  TestCallbacks cbs[8];

  // First request
  CryptoMbPrivateKeyConnection op0(cbs[0], *dispatcher, bssl::UpRef(pkey), queue);
  res =
      rsaPrivateKeySignForTest(&op0, nullptr, nullptr, 128, SSL_SIGN_RSA_PKCS1_SHA256, in, in_len);
  EXPECT_EQ(res, ssl_private_key_retry);

  res = privateKeyCompleteForTest(&op0, nullptr, nullptr, 128);
  // No processing done yet after first request
  EXPECT_EQ(res, ssl_private_key_retry);

  time_system.advanceTimeAndRun(std::chrono::seconds(1), *dispatcher,
                                Event::Dispatcher::RunType::NonBlock);

  size_t out_len = 0;
  uint8_t out[128] = {0};

  res = privateKeyCompleteForTest(&op0, out, &out_len, 128);
  EXPECT_EQ(res, ssl_private_key_success);
  EXPECT_NE(out_len, 0);

  // Add crypto library errors
  fakeIpp->injectErrors(true);

  CryptoMbPrivateKeyConnection op1(cbs[0], *dispatcher, bssl::UpRef(pkey), queue);
  res =
      rsaPrivateKeySignForTest(&op1, nullptr, nullptr, 128, SSL_SIGN_RSA_PKCS1_SHA256, in, in_len);
  EXPECT_EQ(res, ssl_private_key_retry);

  res = privateKeyCompleteForTest(&op1, nullptr, nullptr, 128);
  // No processing done yet after first request
  EXPECT_EQ(res, ssl_private_key_retry);

  time_system.advanceTimeAndRun(std::chrono::seconds(1), *dispatcher,
                                Event::Dispatcher::RunType::NonBlock);

  res = privateKeyCompleteForTest(&op1, out, &out_len, 128);
  EXPECT_EQ(res, ssl_private_key_failure);
}

TEST(CryptoMbProviderTest, TestRSAQueueSizeStatistics) {
  Event::SimulatedTimeSystem time_system;
  Stats::TestUtil::TestStore server_stats_store;
  Api::ApiPtr api = Api::createApiForTest(server_stats_store, time_system);
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("test_thread"));
  bssl::UniquePtr<EVP_PKEY> pkey = makeRsaKey();
  std::shared_ptr<FakeIppCryptoImpl> fakeIpp = std::make_shared<FakeIppCryptoImpl>(true);
  RSA* rsa = EVP_PKEY_get0_RSA(pkey.get());
  const BIGNUM *e, *n, *d;
  RSA_get0_key(rsa, &n, &e, &d);
  fakeIpp->setRsaKey(n, e, d);
  CryptoMbStats stats(server_stats_store, CryptoMbQueue::MULTIBUFF_BATCH, kStatsPrefix,
                      kQueueSizeStatPrefix);

  CryptoMbQueue queue(std::chrono::milliseconds(200), KeyType::Rsa, 1024, fakeIpp, *dispatcher,
                      stats);

  size_t in_len = 32;
  uint8_t in[32] = {0x7f};

  ssl_private_key_result_t res;
  TestCallbacks cbs[CryptoMbQueue::MULTIBUFF_BATCH];
  std::vector<std::unique_ptr<CryptoMbPrivateKeyConnection>> connections;

  // Initialize connections.
  for (auto& cb : cbs) {
    connections.push_back(
        std::make_unique<CryptoMbPrivateKeyConnection>(cb, *dispatcher, bssl::UpRef(pkey), queue));
  }

  // Check that all counters are at zero.
  for (uint32_t i = 1; i <= CryptoMbQueue::MULTIBUFF_BATCH; i++) {
    EXPECT_EQ(server_stats_store
                  .counterFromString(absl::StrCat(kStatsPrefix, ".", kQueueSizeStatPrefix, i))
                  .value(),
              0);
  }

  // Increment all but the last queue size counter once inside the loop.
  for (uint32_t i = 1; i < CryptoMbQueue::MULTIBUFF_BATCH; i++) {
    // Create correct amount of signing operations for current index.
    for (uint32_t j = 0; j < i; j++) {
      res = rsaPrivateKeySignForTest(connections[j].get(), nullptr, nullptr, 128,
                                     SSL_SIGN_RSA_PKCS1_SHA256, in, in_len);
      EXPECT_EQ(res, ssl_private_key_retry);
    }

    time_system.advanceTimeAndRun(std::chrono::seconds(1), *dispatcher,
                                  Event::Dispatcher::RunType::NonBlock);

    size_t out_len = 0;
    uint8_t out[128] = {0};
    res = privateKeyCompleteForTest(connections[0].get(), out, &out_len, 128);
    EXPECT_EQ(res, ssl_private_key_success);
    EXPECT_NE(out_len, 0);

    // Check that all thus far incremented queue size counters have been incremented by one.
    for (uint32_t j = 1; j <= i; j++) {
      EXPECT_EQ(server_stats_store
                    .counterFromString(absl::StrCat(kStatsPrefix, ".", kQueueSizeStatPrefix, j))
                    .value(),
                1);
    }
    // Check that all queue size counters that have not been incremented stay at zero.
    for (uint32_t j = i + 1; j <= CryptoMbQueue::MULTIBUFF_BATCH; j++) {
      EXPECT_EQ(server_stats_store
                    .counterFromString(absl::StrCat(kStatsPrefix, ".", kQueueSizeStatPrefix, j))
                    .value(),
                0);
    }
  }

  // Increment last queue size counter once.
  // Create an amount of signing operations equal to maximum queue size.
  for (uint32_t j = 0; j < CryptoMbQueue::MULTIBUFF_BATCH; j++) {
    res = rsaPrivateKeySignForTest(connections[j].get(), nullptr, nullptr, 128,
                                   SSL_SIGN_RSA_PKCS1_SHA256, in, in_len);
    EXPECT_EQ(res, ssl_private_key_retry);
  }

  // Timeout does not have to be triggered when queue is at maximum size.
  dispatcher->run(Event::Dispatcher::RunType::NonBlock);

  size_t out_len = 0;
  uint8_t out[128] = {0};
  res = privateKeyCompleteForTest(connections[0].get(), out, &out_len, 128);
  EXPECT_EQ(res, ssl_private_key_success);
  EXPECT_NE(out_len, 0);

  // Check that all queue size counters have been incremented by one.
  for (uint32_t j = 1; j <= CryptoMbQueue::MULTIBUFF_BATCH; j++) {
    EXPECT_EQ(server_stats_store
                  .counterFromString(absl::StrCat(kStatsPrefix, ".", kQueueSizeStatPrefix, j))
                  .value(),
              1);
  }
}

} // namespace
} // namespace CryptoMb
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
