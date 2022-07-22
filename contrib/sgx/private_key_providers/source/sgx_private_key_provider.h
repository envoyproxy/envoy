#pragma once

#include "envoy/api/api.h"
#include "envoy/event/dispatcher.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/private_key/private_key_config.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/logger.h"

#include "contrib/envoy/extensions/private_key_providers/sgx/v3alpha/sgx.pb.h"
#include "contrib/sgx/private_key_providers/source/sgx.h"
#include "contrib/sgx/private_key_providers/source/utility.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace Sgx {

using envoy::extensions::private_key_providers::sgx::v3alpha::SgxPrivateKeyMethodConfig;
using Server::Configuration::TransportSocketFactoryContext;

// ssl_private_key_result_t rsaSignWithSgx(SSL* ssl, uint8_t* out, size_t* out_len,
//                                         size_t max_out, uint16_t signature_algorithm,
//                                         const uint8_t* in, size_t in_len);

// ssl_private_key_result_t rsaDecryptWithSgx(SSL* ssl, uint8_t* out, size_t* out_len,
//                                            size_t max_out, const uint8_t* in, size_t in_len);

// ssl_private_key_result_t ecdsaSignWithSgx(SSL* ssl, uint8_t* out, size_t* out_len,
//                                           size_t max_out, uint16_t signature_algorithm,
//                                           const uint8_t* in, size_t in_len);

// ssl_private_key_result_t ecdsaDecryptWithSgx(SSL*, uint8_t*, size_t*, size_t, const uint8_t*,
//                                              size_t);

// ssl_private_key_result_t completeWithSgx(SSL*, uint8_t*, size_t*, size_t);

enum class RequestStatus { Retry, Success, Error };
enum class KeyType { Rsa, Ec };

using SgxContextSharedPtr = std::shared_ptr<SGXContext>;

// SgxPrivateKeyConnection maintains the data needed by a given SSL
// connection.
class SgxPrivateKeyConnection : public Logger::Loggable<Logger::Id::connection> {
public:
  SgxPrivateKeyConnection(Ssl::PrivateKeyConnectionCallbacks& cb, Event::Dispatcher& dispatcher,
                          SgxContextSharedPtr sgx_context, bssl::UniquePtr<EVP_PKEY> pkey,
                          CK_OBJECT_HANDLE private_key, CK_OBJECT_HANDLE public_key);

  virtual ~SgxPrivateKeyConnection() = default;

  Event::Dispatcher& dispatcher_;
  Ssl::PrivateKeyConnectionCallbacks& cb_;
  SgxContextSharedPtr sgx_context_;
  CK_OBJECT_HANDLE private_key_;
  CK_OBJECT_HANDLE public_key_;

private:
  bssl::UniquePtr<EVP_PKEY> pkey_;
};

// SgxPrivateKeyMethodProvider handles the private key method operations for
// an SSL socket.
class SgxPrivateKeyMethodProvider : public virtual Ssl::PrivateKeyMethodProvider,
                                    public Logger::Loggable<Logger::Id::connection> {
public:
  SgxPrivateKeyMethodProvider(const SgxPrivateKeyMethodConfig& config,
                              TransportSocketFactoryContext& private_key_provider_context,
                              const SgxSharedPtr& sgx);

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
    ThreadLocalData(std::chrono::milliseconds poll_delay, enum KeyType type, int key_size,
                    const SgxSharedPtr& sgx, Event::Dispatcher& d);
  };

  void initializeKeypair();

  void createCSR();

  void createQuote();

  void sendCSRandQuote();

  void initialize();

  Ssl::BoringSslPrivateKeyMethodSharedPtr method_{};
  Api::Api& api_;
  bssl::UniquePtr<EVP_PKEY> pkey_;

  ThreadLocal::TypedSlotPtr<ThreadLocalData> tls_;

  // Resources related to PKCS11 & SGX
  std::string sgx_library_;
  std::string usr_pin_;
  std::string so_pin_;
  std::string token_label_;
  std::string key_type_;
  std::string key_label_;

  // related to SGX
  SgxContextSharedPtr sgx_context_;

  // related to key pair
  CK_OBJECT_HANDLE private_key_;
  CK_OBJECT_HANDLE public_key_;
};

} // namespace Sgx
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
