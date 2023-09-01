#include "extensions/transport_sockets/tls/ssl_handshaker/custom_ssl_handshaker.h"

#include <cstdint>
#include <string>
#include <vector>

#include "extensions/transport_sockets/tls/ssl_handshaker.h"

#include "envoy/network/transport_socket.h"
#include "envoy/ssl/handshaker.h"

#include "envoy/stats/scope.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/http/headers.h"
#include "absl/container/node_hash_set.h"
#include "absl/synchronization/notification.h"

#include "absl/strings/str_replace.h"
#include "openssl/err.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CustomSslHandshaker {

CustomSslHandshakerConfig::CustomSslHandshakerConfig(
    const envoy::extensions::transport_sockets::ssl_handshaker::v3::Config& proto_config,
    Server::Configuration::TransportSocketFactoryContext& factory_context)
    : config_(proto_config), fetch_cert_timeout_(PROTOBUF_GET_MS_OR_DEFAULT(
                                 proto_config, fetch_cert_timeout, FetchCertTimeoutMs)),
      select_cert_name_timeout_(PROTOBUF_GET_MS_OR_DEFAULT(proto_config, select_cert_name_timeout,
                                                           SelectCertNameTimeoutMs)),
      transport_factory_context_(
          dynamic_cast<Server::Configuration::TransportSocketFactoryContextImpl&>(
              factory_context)){};

void CustomSslHandshakerConfig::init() {
  for (int i = 1; i <= config_.prefetch_names_size(); i++) {
    auto sds_config_name = std::string(config_.prefetch_names(i - 1));
    auto provider =
        std::make_shared<DynamicTlsContextProvider>(sds_config_name, shared_from_this());
    dynamic_tls_context_providers_[sds_config_name] = provider;
  }
}

CustomSslHandshakerImpl::CustomSslHandshakerImpl(bssl::UniquePtr<SSL> ssl_ptr,
                                                 int ssl_extended_socket_info_index,
                                                 Ssl::HandshakeCallbacks* handshake_callbacks,
                                                 CustomSslHandshakerConfigSharedPtr config)
    : SslHandshakerImpl(std::move(ssl_ptr), ssl_extended_socket_info_index, handshake_callbacks),
      dynamicLib_(Dso::DsoInstanceManager::getDsoInstanceByID(config->so_id())), config_(config) {

  RELEASE_ASSERT(dynamicLib_ != nullptr,
                 fmt::format("not found dynamic so library: '{}'", config->so_id()));
  // no connection in handshake_callbacks now,
  // since setTransportSocketCallbacks is not invoked yet.
  ENVOY_LOG(debug, "setting cert_cb in custom handshaker");
  SSL_set_cert_cb(
      ssl(), [](SSL* ssl, void* arg) -> int { return setCertCb(ssl, arg); }, this);
}

CustomSslHandshakerImpl::~CustomSslHandshakerImpl() {
  disableTimeoutTimer();
  if (fetch_callback_handle_ != nullptr) {
    // it's safe to remove even the callback is already removed,
    // since we will check if it's already removed again before erase from std list.
    config_->removeFetchCallback(sds_config_name_, std::move(fetch_callback_handle_));
  }
};

void CustomSslHandshakerImpl::fetchCertificateCallback() {
  ASSERT(isThreadSafe());
  ASSERT(state_ == HandshakeState::FetchCert);

  ENVOY_CONN_LOG(debug, "running fetch certificate callback in work thread, server name: {}",
                 connection(), server_name_);
  disableTimeoutTimer();

  // it's safe to assgin to null, since callback must have been removed.
  fetch_callback_handle_ = nullptr;

  auto ctx = config_->getTlsContext(sds_config_name_);
  if (ctx == nullptr) {
    return handleFailure(FailureReason::NoTlsContext);
  }

  ENVOY_CONN_LOG(debug, "setting ssl ctx after fetched certficate, for setting certificate and key",
                 connection());
  RELEASE_ASSERT(SSL_set_SSL_CTX(ssl(), ctx->ssl_ctx_.get()) != nullptr, "");

  // Resume handshake.
  Network::PostIoAction action = doHandshake();
  if (action == Network::PostIoAction::Close) {
    handleFailure(FailureReason::ResumeFetchCertFailed);
  }

#ifdef HACK_TESTING
  config_->removeProvider(sds_config_name_);
#endif
}

void CustomSslHandshakerImpl::disableTimeoutTimer() {
  // Notice: can not call isThreadSafe here, since will segfault when invoked from destruct method.
  if (fetch_timeout_timer_) {
    fetch_timeout_timer_->disableTimer();
    fetch_timeout_timer_.reset();
  }
  if (select_cert_timeout_timer_) {
    select_cert_timeout_timer_->disableTimer();
    select_cert_timeout_timer_.reset();
  }
}

void CustomSslHandshakerImpl::handleFailure(FailureReason reason) {
  ASSERT(isThreadSafe());
  disableTimeoutTimer();

  switch (reason) {
  case FailureReason::ResumeFetchCertFailed:
    ENVOY_CONN_LOG(
        debug, "resume handshake completion error after fetched certificate for server name: '{}'",
        connection(), server_name_);
    break;
  case FailureReason::ResumeSelectCertFailed:
    ENVOY_CONN_LOG(
        debug,
        "resume handshake completion error after select certificate name for server name: '{}'",
        connection(), server_name_);
    break;
  case FailureReason::NoTlsContext:
    ENVOY_CONN_LOG(error, "no tls context for server name: '{}'", connection(), server_name_);
    break;
  case FailureReason::FetchTimedout:
    ENVOY_CONN_LOG(error, "fetch sds('{}') timed out for server name: '{}'", connection(),
                   sds_config_name_, server_name_);
    break;
  case FailureReason::SelectTimedout:
    ENVOY_CONN_LOG(error, "select certificate name timed out for server name: '{}'", connection(),
                   server_name_);
    break;
  }
  // TODO: stats?
  connection().close(Network::ConnectionCloseType::FlushWrite);
}

bool CustomSslHandshakerImpl::fetchCertAsync() {
  ASSERT(isThreadSafe());
  if (state_ == HandshakeState::FetchCert) {
    ENVOY_CONN_LOG(debug, "reenter fetchCertAsync, skipping", connection());
    return true;
  } else if (state_ == HandshakeState::SelectCertName) {
    ENVOY_CONN_LOG(debug, "waiting cert name from go, skipping", connection());
    return true;
  }
  ASSERT(state_ == HandshakeState::GotCertName);
  if (sds_config_name_.empty()) {
    // return empty string from go, means close connection.
    ENVOY_CONN_LOG(error, "no selected certificate, will close connection", connection());
    return false;
  }

  state_ = HandshakeState::FetchCert;
  auto weak_this = weak_from_this();
  fetch_timeout_timer_ = dispatcher().createTimer([this, weak_this]() -> void {
    if (weak_this.expired()) {
      ENVOY_CONN_LOG(debug,
                     "handshaker instance is gone while fetch timeout timer triggered, skipping",
                     connection());
      return;
    }
    handleFailure(FailureReason::FetchTimedout);
  });
  fetch_timeout_timer_->enableTimer(config_->fetchCertTimeout());

  fetch_callback_handle_ =
      config_->addFetchCertificateCallback(sds_config_name_, [this, weak_this](bool from_main) {
        ASSERT(from_main ? Thread::MainThread::isMainThread() : isThreadSafe());
        if (weak_this.expired()) {
          ENVOY_CONN_LOG(debug,
                         "handshaker instance is gone while in fetch certficate callback, from "
                         "main thread: {}, skipping",
                         connection(), from_main);
          return;
        }
        ENVOY_CONN_LOG(debug, "running fetch certificate callback in main thread: {}", connection(),
                       from_main);
        if (from_main) {
          dispatcher().post([this, weak_this] {
            if (!weak_this.expired()) {
              fetchCertificateCallback();
            } else {
              ENVOY_CONN_LOG(debug,
                             "handshaker instance is gone while running fetch certificate callback "
                             "in work thread",
                             connection());
            }
          });
        } else {
          fetchCertificateCallback();
        }
      });

  return true;
}

int CustomSslHandshakerImpl::setCertCb(SSL*, void* arg) {
  auto instance = static_cast<CustomSslHandshakerImpl*>(arg);
  return instance->setCertCbInternal();
}

void CustomSslHandshakerImpl::selectCert() {
  state_ = HandshakeState::SelectCertName;

  auto holder = new CustomSslHandshakerImplWeakPtrHolder(weak_from_this());
  auto ptr_holder = reinterpret_cast<unsigned long long>(holder);

  auto server_name_ptr = reinterpret_cast<unsigned long long>(server_name_.data());
  auto server_name_len = server_name_.length();

  // Notice: may invoke envoyTlsConnectionSelectCert before this call returned,
  // and it will changed the state to GotCertName,
  // when selected a name sync in go side.
  dynamicLib_->envoyOnTlsHandshakerSelectCert(ptr_holder, server_name_ptr, server_name_len);
}

void CustomSslHandshakerImpl::selectCertCb(bool safe_thread, const char* name_data, int name_len) {
  ENVOY_CONN_LOG(debug, "selectCert callback with name_len: {}, in safe thread: {}", connection(),
                 name_len, safe_thread);

  // back in the caller work thread, mark GotCertName.
  if (safe_thread) {
    disableTimeoutTimer();
    state_ = HandshakeState::GotCertName;
    sds_config_name_ = std::string(name_data, name_len);
    return;
  }

  // deep copy a new string.
  auto tmp_name = new std::string(name_data, name_len);
  auto weak_ptr = weak_from_this();
  dispatcher().post([this, weak_ptr, tmp_name] {
    ASSERT(isThreadSafe());
    if (!weak_ptr.expired()) {
      selectCertCb(true, tmp_name->data(), tmp_name->length());

      // Resume handshake.
      Network::PostIoAction action = doHandshake();
      if (action == Network::PostIoAction::Close) {
        handleFailure(FailureReason::ResumeSelectCertFailed);
      }
    }
    delete tmp_name;
  });
}

int CustomSslHandshakerImpl::setCertCbInternal() {
  ASSERT(isThreadSafe());
  ENVOY_CONN_LOG(debug, "setCertCb in state: {}", connection(), state_);

  if (state_ == HandshakeState::Init) {
    server_name_ = SSL_get_servername(ssl(), TLSEXT_NAMETYPE_host_name);
    if (!server_name_.empty()) {
      ENVOY_CONN_LOG(debug, "get TLS server name in set cert callback: {}", connection(),
                     server_name_);
    } else {
      ENVOY_CONN_LOG(debug, "TLS server name is empty in set cert callback", connection());
    }

    selectCert();
  }

  // the state may changed in selectCert, should not use "else if".
  if (state_ == HandshakeState::GotCertName) {
    if (!sds_config_name_.empty()) {
      auto ctx = config_->getTlsContext(sds_config_name_);
      if (ctx != nullptr) {
        // Notice: this is the fast path in most cases: named certificate is already existing.
        ENVOY_CONN_LOG(debug,
                       "setting ssl ctx(for certificate) during after got cert name "
                       "and the certificate is existing, server name: {}",
                       connection(), server_name_);
        RELEASE_ASSERT(SSL_set_SSL_CTX(ssl(), ctx->ssl_ctx_.get()) != nullptr, "");
        return 1;
      }
      // else, the named certificate is not existing, will invoke fetchCertAsync in doHandshake
    }
    // else, no selected certificate, will close connection in doHandshake

  } else if (!select_cert_timeout_timer_) {
    auto weak_this = weak_from_this();
    select_cert_timeout_timer_ = dispatcher().createTimer([this, weak_this]() -> void {
      if (weak_this.expired()) {
        ENVOY_CONN_LOG(debug,
                       "handshaker instance is gone while select timeout timer triggered, skipping",
                       connection());
        return;
      }
      handleFailure(FailureReason::SelectTimedout);
    });
    select_cert_timeout_timer_->enableTimer(config_->selectCertNameTimeout());
  }

  return -1;
}

Network::PostIoAction CustomSslHandshakerImpl::doHandshake() {
  RELEASE_ASSERT(state() != Ssl::SocketState::HandshakeComplete &&
                     state() != Ssl::SocketState::ShutdownSent,
                 "Handshaker state was either complete or sent.");

  ENVOY_CONN_LOG(debug, "doing custom handshake, connection state: {}, handshaker state: {}",
                 connection(), state(), state_);

  int rc = SSL_do_handshake(ssl());
  if (rc == 1) {
    setState(Ssl::SocketState::HandshakeComplete);
    handshakeCallbacks()->onSuccess(ssl());

    // It's possible that we closed during the handshake callback.
    return connection().state() == Network::Connection::State::Open
               ? Network::PostIoAction::KeepOpen
               : Network::PostIoAction::Close;
  } else {
    auto err = SSL_get_error(ssl(), rc);
    ENVOY_CONN_LOG(trace, "ssl error occurred while read: {}", connection(),
                   Utility::getErrorDescription(err));
    switch (err) {
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
      return Network::PostIoAction::KeepOpen;
    case SSL_ERROR_WANT_X509_LOOKUP:
      if (fetchCertAsync()) {
        return Network::PostIoAction::KeepOpen;
      }
      return Network::PostIoAction::Close;
    default:
      handshakeCallbacks()->onFailure();
      return Network::PostIoAction::Close;
    }
  }
}

std::shared_ptr<TlsContext>
CustomSslHandshakerConfig::getTlsContext(const std::string& sds_config_name) {
  DynamicTlsContextProviderSharedPtr provider;
  {
    absl::ReaderMutexLock lock(&provider_mutex_);
    auto it = dynamic_tls_context_providers_.find(sds_config_name);
    if (it != dynamic_tls_context_providers_.end()) {
      provider = it->second;
    }
  }
  return provider != nullptr ? provider->getTlsContext() : nullptr;
}

#ifdef HACK_TESTING
void CustomSslHandshakerConfig::removeProvider(const std::string& sds_config_name) {
  absl::WriterMutexLock lock(&provider_mutex_);
  auto it = dynamic_tls_context_providers_.find(sds_config_name);
  if (it != dynamic_tls_context_providers_.end()) {
    dynamic_tls_context_providers_.erase(it);
  }
}
#endif

DynamicTlsContextProvider::DynamicTlsContextProvider(const std::string& sds_config_name,
                                                     CustomSslHandshakerConfigSharedPtr config)
    : sds_config_name_(sds_config_name), config_(config),
      main_dispatcher_(config->factoryContext().dispatcher()) {

  // the factory context is saved from the main thread.
  config->factoryContext().dispatcher().post([this, config] {
    // Notice: "this" must existing since config ref it, and config is refered in this lambda.
    ASSERT(Thread::MainThread::isMainThread());
    ENVOY_LOG(trace, "creating tls provider in main thread, sds_config_name: {}", sds_config_name_);
    try {
      tls_provider_ = config->factoryContext().secretManager().findOrCreateTlsCertificateProvider(
          config->sdsConfig(), sds_config_name_, config->factoryContext());

      sds_update_callback_ = tls_provider_->addUpdateCallback([this] {
        ENVOY_LOG(trace, "running sds update callback in main thread, sds_config_name: {}",
                  sds_config_name_);
        sdsUpdateCallback();
      });
    } catch (const EnvoyException& e) {
      // we don't need to close connection, it will be closed when fetch timedout.
      ENVOY_LOG(critical, "create Tls Certificate Provider failed: {}, sds_config_name: {}",
                e.what(), sds_config_name_);
    } catch (...) {
      ENVOY_LOG(critical, "create Tls Certificate Provider, unknown exception. sds_config_name: {}",
                sds_config_name_);
    }
  });
}

// should make sure tls provider is derefered in the main thread,
// otherwise, the sds client will unsubscrible(~GrpcSubscriptionImpl) in the work thread,
// that will hit assert.
DynamicTlsContextProvider::~DynamicTlsContextProvider() {
  auto main_thread = Thread::MainThread::isMainThread();
  ENVOY_LOG(debug, "destruct DynamicTlsContextProvider, sds_config_name: {}, main_thread: {}",
            sds_config_name_, main_thread);

  absl::Notification* notification = nullptr;
  if (!main_thread) {
    auto provider = std::move(tls_provider_);
    notification = new absl::Notification();
    main_dispatcher_.post([provider, notification]() {
      // wait a notification from work thead, to make sure it's unref in work thread,
      // also, it is a cold path, wait notification is accept.
      ASSERT(Thread::MainThread::isMainThread());
      ENVOY_LOG(debug, "tls provider use_count: {}", provider.use_count());

      notification->WaitForNotification();
      delete notification;
    });
  }
  if (notification != nullptr) {
    // tls provider is not ref in work thead now.
    notification->Notify();
  }
}

std::shared_ptr<TlsContext> DynamicTlsContextProvider::getTlsContext() {
  absl::ReaderMutexLock lock(&tls_context_mutex_);
  return tls_context_;
}

ABSL_MUST_USE_RESULT
FetchCallbackHandle
DynamicTlsContextProvider::addFetchCallback(std::function<void(bool)> callback) {
  Thread::LockGuard callback_lock(fetch_callback_lock_);
  // check tls context again to avoid race.
  absl::ReaderMutexLock lock(&tls_context_mutex_);
  if (tls_context_ != nullptr) {
    callback(false); // false: still in work thread.
    return nullptr;
  }
  auto cb_handle = std::make_unique<FetchCallbackHolder>(callback);
  // it's safe to use the raw point here,
  // since we will make sure it's deleted before unref in ~CustomSslHandshakerImpl.
  fetch_callbacks_.emplace_back(cb_handle.get());
  // Get the list iterator of added callback handle
  auto it = (--fetch_callbacks_.end());
  cb_handle->it = it;

  return cb_handle;
}

void DynamicTlsContextProvider::removeFetchCallback(FetchCallbackHandle cb_handle) {
  // Take ownership of the callbacks under the fetch_callback_lock_.
  // no need lock for cb_handle, since it's always protected by fetch_callback_lock_.
  Thread::LockGuard lock(fetch_callback_lock_);
  if (!cb_handle->removed) {
    fetch_callbacks_.erase(cb_handle->it);
  }
}

void DynamicTlsContextProvider::sdsUpdateCallback() {
  ASSERT(Thread::MainThread::isMainThread());
  // if config is gone, means all handshaker impl are gone.
  auto config = config_.lock();
  if (config == nullptr) {
    return;
  }

  // load Tls context before run fetch callbacks.
  try {
    loadTlsContext(config->factoryContext().api());
  } catch (const EnvoyException& e) {
    // we don't need to close connection, it will be closed with NoTlsContext error.
    ENVOY_LOG(error, "load Tls context from Certificate config failed: {}, sds_config_name: {}",
              e.what(), sds_config_name_);
  } catch (...) {
    ENVOY_LOG(error,
              "load Tls context from Certificate config, unknown exception. sds_config_name: {}",
              sds_config_name_);
  }

  {
    // Take ownership of the callbacks under the fetch_callback_lock_.
    Thread::LockGuard lock(fetch_callback_lock_);
    while (!fetch_callbacks_.empty()) {
      // Run the callback, true means in main thread.
      auto cb_handle = fetch_callbacks_.front();
      cb_handle->cb(true);
      cb_handle->removed = true;
      // Pop the front.
      fetch_callbacks_.pop_front();
    }
  }
}

void DynamicTlsContextProvider::loadTlsContext(Api::Api& api) {
  ASSERT(Thread::MainThread::isMainThread());
  if (tls_provider_->secret() == nullptr) {
    ENVOY_LOG(error, "no secret in sds update callback, skipping");
    return;
  }
  Ssl::TlsCertificateConfigImpl tls_certificate(*tls_provider_->secret(), nullptr, api);
  auto ctx = std::make_shared<TlsContext>();

  ctx->ssl_ctx_.reset(SSL_CTX_new(TLS_method()));
  ctx->cert_chain_file_path_ = tls_certificate.certificateChainPath();
  bssl::UniquePtr<BIO> bio(
      BIO_new_mem_buf(const_cast<char*>(tls_certificate.certificateChain().data()),
                      tls_certificate.certificateChain().size()));
  RELEASE_ASSERT(bio != nullptr, "");
  ctx->cert_chain_.reset(PEM_read_bio_X509_AUX(bio.get(), nullptr, nullptr, nullptr));
  if (ctx->cert_chain_ == nullptr ||
      !SSL_CTX_use_certificate(ctx->ssl_ctx_.get(), ctx->cert_chain_.get())) {
    while (uint64_t err = ERR_get_error()) {
      ENVOY_LOG_MISC(debug, "SSL error: {}:{}:{}:{}", err,
                     absl::NullSafeStringView(ERR_lib_error_string(err)),
                     absl::NullSafeStringView(ERR_func_error_string(err)), ERR_GET_REASON(err),
                     absl::NullSafeStringView(ERR_reason_error_string(err)));
    }
    throw EnvoyException(
        absl::StrCat("Failed to load certificate chain from ", ctx->cert_chain_file_path_));
  }
  // Read rest of the certificate chain.
  while (true) {
    bssl::UniquePtr<X509> cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
    if (cert == nullptr) {
      break;
    }
    if (!SSL_CTX_add_extra_chain_cert(ctx->ssl_ctx_.get(), cert.get())) {
      throw EnvoyException(
          absl::StrCat("Failed to load certificate chain from ", ctx->cert_chain_file_path_));
    }
    // SSL_CTX_add_extra_chain_cert() takes ownership.
    cert.release();
  }
  // Check for EOF.
  const uint32_t err = ERR_peek_last_error();
  if (ERR_GET_LIB(err) == ERR_LIB_PEM && ERR_GET_REASON(err) == PEM_R_NO_START_LINE) {
    ERR_clear_error();
  } else {
    throw EnvoyException(
        absl::StrCat("Failed to load certificate chain from ", ctx->cert_chain_file_path_));
  }

  // The must staple extension means the certificate promises to carry
  // with it an OCSP staple. https://tools.ietf.org/html/rfc7633#section-6
  constexpr absl::string_view tls_feature_ext = "1.3.6.1.5.5.7.1.24";
  constexpr absl::string_view must_staple_ext_value = "\x30\x3\x02\x01\x05";
  auto must_staple = Utility::getCertificateExtensionValue(*ctx->cert_chain_, tls_feature_ext);
  if (must_staple == must_staple_ext_value) {
    ctx->is_must_staple_ = true;
  }

  bssl::UniquePtr<EVP_PKEY> public_key(X509_get_pubkey(ctx->cert_chain_.get()));
  const int pkey_id = EVP_PKEY_id(public_key.get());
  ctx->is_ecdsa_ = pkey_id == EVP_PKEY_EC;
  switch (pkey_id) {
  case EVP_PKEY_EC: {
    // We only support P-256 ECDSA today.
    const EC_KEY* ecdsa_public_key = EVP_PKEY_get0_EC_KEY(public_key.get());
    // Since we checked the key type above, this should be valid.
    ASSERT(ecdsa_public_key != nullptr);
    const EC_GROUP* ecdsa_group = EC_KEY_get0_group(ecdsa_public_key);
    if (ecdsa_group == nullptr || EC_GROUP_get_curve_name(ecdsa_group) != NID_X9_62_prime256v1) {
      throw EnvoyException(fmt::format("Failed to load certificate chain from {}, only P-256 "
                                       "ECDSA certificates are supported",
                                       ctx->cert_chain_file_path_));
    }
    ctx->is_ecdsa_ = true;
  } break;
  case EVP_PKEY_RSA: {
    // We require RSA certificates with 2048-bit or larger keys.
    const RSA* rsa_public_key = EVP_PKEY_get0_RSA(public_key.get());
    // Since we checked the key type above, this should be valid.
    ASSERT(rsa_public_key != nullptr);
    const unsigned rsa_key_length = RSA_size(rsa_public_key);
#ifdef BORINGSSL_FIPS
    if (rsa_key_length != 2048 / 8 && rsa_key_length != 3072 / 8 && rsa_key_length != 4096 / 8) {
      throw EnvoyException(
          fmt::format("Failed to load certificate chain from {}, only RSA certificates with "
                      "2048-bit, 3072-bit or 4096-bit keys are supported in FIPS mode",
                      ctx->cert_chain_file_path_));
    }
#else
    if (rsa_key_length < 2048 / 8) {
      throw EnvoyException(fmt::format("Failed to load certificate chain from {}, only RSA "
                                       "certificates with 2048-bit or larger keys are supported",
                                       ctx->cert_chain_file_path_));
    }
#endif
  } break;
#ifdef BORINGSSL_FIPS
  default:
    throw EnvoyException(fmt::format("Failed to load certificate chain from {}, only RSA and "
                                     "ECDSA certificates are supported in FIPS mode",
                                     ctx->cert_chain_file_path_));
#endif
  }

  Envoy::Ssl::PrivateKeyMethodProviderSharedPtr private_key_method_provider =
      tls_certificate.privateKeyMethod();
  // We either have a private key or a BoringSSL private key method provider.
  if (private_key_method_provider) {
    ctx->private_key_method_provider_ = private_key_method_provider;
    // The provider has a reference to the private key method for the context lifetime.
    Ssl::BoringSslPrivateKeyMethodSharedPtr private_key_method =
        private_key_method_provider->getBoringSslPrivateKeyMethod();
    if (private_key_method == nullptr) {
      throw EnvoyException(fmt::format("Failed to get BoringSSL private key method from provider"));
    }
#ifdef BORINGSSL_FIPS
    if (!ctx->private_key_method_provider_->checkFips()) {
      throw EnvoyException(
          fmt::format("Private key method doesn't support FIPS mode with current parameters"));
    }
#endif
    SSL_CTX_set_private_key_method(ctx->ssl_ctx_.get(), private_key_method.get());
  } else {
    // Load private key.
    bio.reset(BIO_new_mem_buf(const_cast<char*>(tls_certificate.privateKey().data()),
                              tls_certificate.privateKey().size()));
    RELEASE_ASSERT(bio != nullptr, "");
    bssl::UniquePtr<EVP_PKEY> pkey(PEM_read_bio_PrivateKey(
        bio.get(), nullptr, nullptr,
        !tls_certificate.password().empty() ? const_cast<char*>(tls_certificate.password().c_str())
                                            : nullptr));

    if (pkey == nullptr || !SSL_CTX_use_PrivateKey(ctx->ssl_ctx_.get(), pkey.get())) {
      throw EnvoyException(fmt::format("Failed to load private key from {}, Cause: {}",
                                       tls_certificate.privateKeyPath(),
                                       Utility::getLastCryptoError().value_or("unknown")));
    }

#ifdef BORINGSSL_FIPS
    // Verify that private keys are passing FIPS pairwise consistency tests.
    switch (pkey_id) {
    case EVP_PKEY_EC: {
      const EC_KEY* ecdsa_private_key = EVP_PKEY_get0_EC_KEY(pkey.get());
      if (!EC_KEY_check_fips(ecdsa_private_key)) {
        throw EnvoyException(fmt::format("Failed to load private key from {}, ECDSA key failed "
                                         "pairwise consistency test required in FIPS mode",
                                         tls_certificate.privateKeyPath()));
      }
    } break;
    case EVP_PKEY_RSA: {
      RSA* rsa_private_key = EVP_PKEY_get0_RSA(pkey.get());
      if (!RSA_check_fips(rsa_private_key)) {
        throw EnvoyException(fmt::format("Failed to load private key from {}, RSA key failed "
                                         "pairwise consistency test required in FIPS mode",
                                         tls_certificate.privateKeyPath()));
      }
    } break;
    }
#endif
  }

  {
    absl::WriterMutexLock lock(&tls_context_mutex_);
    tls_context_ = ctx;
  }
}

ABSL_MUST_USE_RESULT
FetchCallbackHandle
CustomSslHandshakerConfig::addFetchCertificateCallback(const std::string& sds_config_name,
                                                       std::function<void(bool)> callback) {

  DynamicTlsContextProviderSharedPtr provider;
  {
    // 1. try to find with read lock
    absl::ReaderMutexLock lock(&provider_mutex_);
    auto it = dynamic_tls_context_providers_.find(sds_config_name);
    if (it != dynamic_tls_context_providers_.end()) {
      provider = it->second;
    }
  }
  if (provider == nullptr) {
    // 2. initilize one with write lock, and try to find again first.
    absl::WriterMutexLock lock(&provider_mutex_);
    auto it = dynamic_tls_context_providers_.find(sds_config_name);
    if (it != dynamic_tls_context_providers_.end()) {
      provider = it->second;
    } else {
      provider = std::make_shared<DynamicTlsContextProvider>(sds_config_name, shared_from_this());
      dynamic_tls_context_providers_[sds_config_name] = provider;
    }
  }
  return provider->addFetchCallback(callback);
}

void CustomSslHandshakerConfig::removeFetchCallback(const std::string& sds_config_name,
                                                    FetchCallbackHandle cb_handle) {

  absl::ReaderMutexLock lock(&provider_mutex_);
  auto it = dynamic_tls_context_providers_.find(sds_config_name);
  if (it != dynamic_tls_context_providers_.end()) {
    auto provider = it->second;
    provider->removeFetchCallback(std::move(cb_handle));
  }
}

} // namespace CustomSslHandshaker
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy