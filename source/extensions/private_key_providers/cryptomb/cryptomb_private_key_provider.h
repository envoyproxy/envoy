#pragma once

#include "envoy/api/api.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/private_key_providers/cryptomb/v3/cryptomb.pb.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/private_key/private_key_config.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/logger.h"
#include "source/extensions/private_key_providers/cryptomb/ipp_crypto.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace CryptoMb {

enum class RequestStatus { Retry, Success, Error };
enum class KeyType { Rsa, Ec };

// CryptoMbContext holds the actual data to be signed or encrypted. It also has a
// reference to the worker thread dispatcher for communicating that it has
// has ran the AVX code and the result is ready to be used.
class CryptoMbContext {
public:
  static constexpr ssize_t MAX_SIGNATURE_SIZE = 512;

  CryptoMbContext(Event::Dispatcher& dispatcher, Ssl::PrivateKeyConnectionCallbacks& cb);
  ~CryptoMbContext() = default;

  enum RequestStatus getStatus() { return status_; }
  void scheduleCallback(enum RequestStatus status);

  // Buffer length is the same as the max signature length (4096 bits = 512 bytes)
  unsigned char out_buf_[MAX_SIGNATURE_SIZE];
  // The real length of the signature.
  size_t out_len_{};
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

// CryptoMbEcdsaContext is a CryptoMbContext which holds the extra EC parameters and has
// custom initialization function.
class CryptoMbEcdsaContext : public CryptoMbContext {
public:
  CryptoMbEcdsaContext(Event::Dispatcher& dispatcher, Ssl::PrivateKeyConnectionCallbacks& cb)
      : CryptoMbContext(dispatcher, cb) {}
  bool ecdsaInit(EC_KEY* ec, const uint8_t* in, size_t in_len);

  // EC parameters.
  const BIGNUM* s_{}; // secret key
  BIGNUM k_{};        // integer, chosen by HMAC of s and msg
  size_t ecdsa_sig_size_{};
};

// CryptoMbEcdsaContext is a CryptoMbContext which holds the extra RSA parameters and has
// custom initialization function. It also has a separate buffer for RSA result
// verification.
class CryptoMbRsaContext : public CryptoMbContext {
public:
  CryptoMbRsaContext(Event::Dispatcher& dispatcher, Ssl::PrivateKeyConnectionCallbacks& cb)
      : CryptoMbContext(dispatcher, cb) {}
  bool rsaInit(RSA* rsa, const uint8_t* in, size_t in_len);

  // RSA parameters.
  const BIGNUM* d_{};
  const BIGNUM* e_{};
  const BIGNUM* n_{};
  const BIGNUM* p_{};
  const BIGNUM* q_{};
  const BIGNUM* dmp1_{};
  const BIGNUM* dmq1_{};
  const BIGNUM* iqmp_{};

  // Buffer for Lenstra check.
  unsigned char lenstra_to_[MAX_SIGNATURE_SIZE];
};

using CryptoMbContextSharedPtr = std::shared_ptr<CryptoMbContext>;
using CryptoMbRsaContextSharedPtr = std::shared_ptr<CryptoMbRsaContext>;
using CryptoMbEcdsaContextSharedPtr = std::shared_ptr<CryptoMbEcdsaContext>;

// CryptoMbQueue maintains the request queue and is able to process it.
class CryptoMbQueue : public Logger::Loggable<Logger::Id::connection> {
public:
  static constexpr uint32_t MULTIBUFF_BATCH = 8;

  CryptoMbQueue(std::chrono::milliseconds poll_delay, enum KeyType type, int keysize,
                IppCryptoSharedPtr ipp, Event::Dispatcher& d);
  void addAndProcessEightRequests(CryptoMbContextSharedPtr mb_ctx);

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
};

// CryptoMbPrivateKeyConnection maintains the data needed by a given SSL
// connection.
class CryptoMbPrivateKeyConnection : public Logger::Loggable<Logger::Id::connection> {
public:
  CryptoMbPrivateKeyConnection(Ssl::PrivateKeyConnectionCallbacks& cb,
                               Event::Dispatcher& dispatcher, bssl::UniquePtr<EVP_PKEY> pkey,
                               CryptoMbQueue& queue);
  virtual ~CryptoMbPrivateKeyConnection() = default;

  EVP_PKEY* getPrivateKey() { return pkey_.get(); };
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
      const envoy::extensions::private_key_providers::cryptomb::v3::CryptoMbPrivateKeyMethodConfig&
          config,
      Server::Configuration::TransportSocketFactoryContext& private_key_provider_context,
      IppCryptoSharedPtr ipp);

  // Ssl::PrivateKeyMethodProvider
  void registerPrivateKeyMethod(SSL* ssl, Ssl::PrivateKeyConnectionCallbacks& cb,
                                Event::Dispatcher& dispatcher) override;
  void unregisterPrivateKeyMethod(SSL* ssl) override;
  bool checkFips() override;
  Ssl::BoringSslPrivateKeyMethodSharedPtr getBoringSslPrivateKeyMethod() override;

  static int connectionIndex();

private:
  // Thread local data containing a single queue per worker thread.
  struct ThreadLocalData : public ThreadLocal::ThreadLocalObject {
    ThreadLocalData(std::chrono::milliseconds poll_delay, enum KeyType type, int keysize,
                    IppCryptoSharedPtr ipp, Event::Dispatcher& d)
        : queue_(poll_delay, type, keysize, ipp, d){};
    CryptoMbQueue queue_;
  };

  Ssl::BoringSslPrivateKeyMethodSharedPtr method_{};
  Api::Api& api_;
  bssl::UniquePtr<EVP_PKEY> pkey_;

  ThreadLocal::TypedSlotPtr<ThreadLocalData> tls_;
};

} // namespace CryptoMb
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
