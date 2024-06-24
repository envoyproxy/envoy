#pragma once

#include "envoy/api/api.h"
#include "envoy/event/dispatcher.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/private_key/private_key_config.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/c_smart_ptr.h"
#include "source/common/common/logger.h"

#include "contrib/cryptomb/private_key_providers/source/cryptomb_stats.h"
#include "contrib/cryptomb/private_key_providers/source/ipp_crypto.h"
#include "contrib/envoy/extensions/private_key_providers/cryptomb/v3alpha/cryptomb.pb.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace CryptoMb {

namespace {
void dontFreeBN(const BIGNUM*) {}
} // namespace
using BIGNUMConstPtr = CSmartPtr<const BIGNUM, dontFreeBN>;

enum class RequestStatus { Retry, Success, Error };
enum class KeyType { Rsa, Ec };

// CryptoMbContext holds the actual data to be signed or encrypted. It also has a
// reference to the worker thread dispatcher for communicating that it has
// has ran the `AVX-512` code and the result is ready to be used.
class CryptoMbContext {
public:
  static constexpr ssize_t MAX_SIGNATURE_SIZE = 512;

  CryptoMbContext(Event::Dispatcher& dispatcher, Ssl::PrivateKeyConnectionCallbacks& cb);
  virtual ~CryptoMbContext() = default;

  void setStatus(RequestStatus status) { status_ = status; }
  enum RequestStatus getStatus() { return status_; }
  void scheduleCallback(enum RequestStatus status);

  // Incoming data buffer.
  std::unique_ptr<uint8_t[]> in_buf_;

private:
  // Whether the decryption / signing is ready.
  enum RequestStatus status_ {};

  Event::Dispatcher& dispatcher_;
  Ssl::PrivateKeyConnectionCallbacks& cb_;
  // For scheduling the callback to the next dispatcher cycle.
  Event::SchedulableCallbackPtr schedulable_{};
};

// CryptoMbEcdsaContext is a CryptoMbContext which holds the extra ECDSA parameters and has
// custom initialization function.
class CryptoMbEcdsaContext : public CryptoMbContext {
public:
  CryptoMbEcdsaContext(bssl::UniquePtr<EC_KEY> ec_key, Event::Dispatcher& dispatcher,
                       Ssl::PrivateKeyConnectionCallbacks& cb)
      : CryptoMbContext(dispatcher, cb), ec_key_(std::move(ec_key)) {}
  bool ecdsaInit(const uint8_t* in, size_t in_len);

  // ECDSA key.
  bssl::UniquePtr<EC_KEY> ec_key_{};
  // ECDSA context to create the ephemeral key k_.
  bssl::UniquePtr<BN_CTX> ctx_{};
  BIGNUM* k_{};
  // ECDSA parameters, which will contain values whose memory is managed within
  // BoringSSL ECDSA key structure, so not wrapped in smart pointers.
  const BIGNUM* priv_key_{};
  size_t sig_len_{};

  // ECDSA signature.
  uint8_t sig_r_[32]{};
  uint8_t sig_s_[32]{};
};

// CryptoMbRsaContext is a CryptoMbContext which holds the extra RSA parameters and has
// custom initialization function. It also has a separate buffer for RSA result
// verification.
class CryptoMbRsaContext : public CryptoMbContext {
public:
  CryptoMbRsaContext(bssl::UniquePtr<EVP_PKEY> pkey, Event::Dispatcher& dispatcher,
                     Ssl::PrivateKeyConnectionCallbacks& cb)
      : CryptoMbContext(dispatcher, cb), rsa_(EVP_PKEY_get1_RSA(pkey.get())) {}
  bool rsaInit(const uint8_t* in, size_t in_len);

  // RSA key.
  bssl::UniquePtr<RSA> rsa_{};
  // RSA parameters. Const pointers, which will contain values whose memory is
  // managed within BoringSSL RSA key structure, so not wrapped in smart
  // pointers.
  const BIGNUM* d_{};
  const BIGNUM* e_{};
  const BIGNUM* n_{};
  const BIGNUM* p_{};
  const BIGNUM* q_{};
  const BIGNUM* dmp1_{};
  const BIGNUM* dmq1_{};
  const BIGNUM* iqmp_{};

  // Buffer for `Lenstra` check.
  unsigned char lenstra_to_[MAX_SIGNATURE_SIZE];

  // Buffer length is the same as the max signature length (4096 bits = 512 bytes)
  unsigned char out_buf_[MAX_SIGNATURE_SIZE];
  // The real length of the signature.
  size_t out_len_{};
};

using CryptoMbContextSharedPtr = std::shared_ptr<CryptoMbContext>;
using CryptoMbEcdsaContextSharedPtr = std::shared_ptr<CryptoMbEcdsaContext>;
using CryptoMbRsaContextSharedPtr = std::shared_ptr<CryptoMbRsaContext>;

// CryptoMbQueue maintains the request queue and is able to process it.
class CryptoMbQueue : public Logger::Loggable<Logger::Id::connection> {
public:
  static constexpr uint32_t MULTIBUFF_BATCH = 8;

  CryptoMbQueue(std::chrono::milliseconds poll_delay, enum KeyType type, int keysize,
                IppCryptoSharedPtr ipp, Event::Dispatcher& d, CryptoMbStats& stats);
  void addAndProcessEightRequests(CryptoMbContextSharedPtr mb_ctx);
  const std::chrono::microseconds& getPollDelayForTest() const { return us_; }

private:
  void processRequests();
  void processRsaRequests();
  void processEcdsaRequests();
  void startTimer();
  void stopTimer();

  // Polling delay.
  std::chrono::microseconds us_{};

  // Queue for the requests.
  std::vector<CryptoMbContextSharedPtr> request_queue_;

  // Key size and key type allowed for this particular queue.
  const enum KeyType type_;
  int key_size_{};

  // Thread local data slot.
  ThreadLocal::SlotPtr slot_{};

  // Crypto operations library interface.
  IppCryptoSharedPtr ipp_{};

  // Timer to trigger queue processing if eight requests are not received in time.
  Event::TimerPtr timer_{};

  CryptoMbStats& stats_;
};

// CryptoMbPrivateKeyConnection maintains the data needed by a given SSL
// connection.
class CryptoMbPrivateKeyConnection : public Logger::Loggable<Logger::Id::connection> {
public:
  CryptoMbPrivateKeyConnection(Ssl::PrivateKeyConnectionCallbacks& cb,
                               Event::Dispatcher& dispatcher, bssl::UniquePtr<EVP_PKEY> pkey,
                               CryptoMbQueue& queue);
  virtual ~CryptoMbPrivateKeyConnection() = default;

  bssl::UniquePtr<EVP_PKEY> getPrivateKey() { return bssl::UpRef(pkey_); };
  void logDebugMsg(std::string msg) { ENVOY_LOG(debug, "CryptoMb: {}", msg); }
  void logWarnMsg(std::string msg) { ENVOY_LOG(warn, "CryptoMb: {}", msg); }
  void addToQueue(CryptoMbContextSharedPtr mb_ctx);

  CryptoMbQueue& queue_;
  Event::Dispatcher& dispatcher_;
  Ssl::PrivateKeyConnectionCallbacks& cb_;
  CryptoMbContextSharedPtr mb_ctx_{};

private:
  Event::FileEventPtr ssl_async_event_{};
  bssl::UniquePtr<EVP_PKEY> pkey_;
};

// CryptoMbPrivateKeyMethodProvider handles the private key method operations for
// an SSL socket.
class CryptoMbPrivateKeyMethodProvider : public virtual Ssl::PrivateKeyMethodProvider,
                                         public Logger::Loggable<Logger::Id::connection> {
public:
  CryptoMbPrivateKeyMethodProvider(
      const envoy::extensions::private_key_providers::cryptomb::v3alpha::
          CryptoMbPrivateKeyMethodConfig& config,
      Server::Configuration::TransportSocketFactoryContext& private_key_provider_context,
      IppCryptoSharedPtr ipp);

  // Ssl::PrivateKeyMethodProvider
  void registerPrivateKeyMethod(SSL* ssl, Ssl::PrivateKeyConnectionCallbacks& cb,
                                Event::Dispatcher& dispatcher) override;
  void unregisterPrivateKeyMethod(SSL* ssl) override;
  bool checkFips() override;
  bool isAvailable() override;
  Ssl::BoringSslPrivateKeyMethodSharedPtr getBoringSslPrivateKeyMethod() override;

  static int connectionIndex();

  const std::chrono::microseconds& getPollDelayForTest() const {
    return tls_->get()->queue_.getPollDelayForTest();
  }

private:
  // Thread local data containing a single queue per worker thread.
  struct ThreadLocalData : public ThreadLocal::ThreadLocalObject {
    ThreadLocalData(std::chrono::milliseconds poll_delay, enum KeyType type, int keysize,
                    IppCryptoSharedPtr ipp, Event::Dispatcher& d, CryptoMbStats& stats)
        : queue_(poll_delay, type, keysize, ipp, d, stats){};
    CryptoMbQueue queue_;
  };

  Ssl::BoringSslPrivateKeyMethodSharedPtr method_{};
  Api::Api& api_;
  bssl::UniquePtr<EVP_PKEY> pkey_;
  enum KeyType key_type_;

  ThreadLocal::TypedSlotPtr<ThreadLocalData> tls_;

  CryptoMbStats stats_;

  bool initialized_{};
};

} // namespace CryptoMb
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
