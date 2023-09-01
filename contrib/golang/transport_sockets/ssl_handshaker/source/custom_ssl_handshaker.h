#pragma once

#include "envoy/extensions/transport_sockets/ssl_handshaker/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/ssl_handshaker/v3/config.pb.validate.h"
#include "envoy/common/callback.h"

#include "extensions/filters/common/dso/dso.h"
#include "extensions/transport_sockets/tls/ssl_handshaker.h"
#include "extensions/transport_sockets/tls/context_impl.h"
#include "server/transport_socket_config_impl.h"
#include "common/ssl/tls_certificate_config_impl.h"
#include "common/common/callback_impl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CustomSslHandshaker {

class CustomSslHandshakerConfig;
using CustomSslHandshakerConfigSharedPtr = std::shared_ptr<CustomSslHandshakerConfig>;

// no need lock even may read/write fields in different threads,
// since read/write fields always protected by fetch_callback_lock_
struct FetchCallbackHolder {
  FetchCallbackHolder(std::function<void(bool)> cb) : cb(cb){};

  std::list<FetchCallbackHolder*>::iterator it;
  bool removed = false;
  std::function<void(bool)> cb;
};
using FetchCallbackHandle = std::unique_ptr<FetchCallbackHolder>;

class DynamicTlsContextProvider : public Logger::Loggable<Logger::Id::config>,
                                  public std::enable_shared_from_this<DynamicTlsContextProvider> {
public:
  DynamicTlsContextProvider(const std::string& sds_config_name,
                            CustomSslHandshakerConfigSharedPtr config);
  ~DynamicTlsContextProvider();

  std::shared_ptr<TlsContext> getTlsContext();
  ABSL_MUST_USE_RESULT FetchCallbackHandle addFetchCallback(std::function<void(bool)> callback);
  void removeFetchCallback(FetchCallbackHandle cb_handle);

private:
  void sdsUpdateCallback();
  void loadTlsContext(Api::Api& api);

  Event::Dispatcher& main_dispatcher_;
  Secret::TlsCertificateConfigProviderSharedPtr tls_provider_;
  Envoy::Common::CallbackHandlePtr sds_update_callback_;

  absl::Mutex tls_context_mutex_;
  std::shared_ptr<TlsContext> tls_context_ ABSL_GUARDED_BY(tls_context_mutex_);

  std::string sds_config_name_;

  // we don't need callback manager here, since the callback will check alive,
  // and we need to be concurrency safe here, but callback manager is not.
  Thread::MutexBasicLockable fetch_callback_lock_;
  std::list<FetchCallbackHolder*> fetch_callbacks_ ABSL_GUARDED_BY(fetch_callback_lock_);

  std::weak_ptr<CustomSslHandshakerConfig> config_;
};

using DynamicTlsContextProviderSharedPtr = std::shared_ptr<DynamicTlsContextProvider>;

class CustomSslHandshakerConfig : public Logger::Loggable<Logger::Id::config>,
                                  public std::enable_shared_from_this<CustomSslHandshakerConfig> {
public:
  CustomSslHandshakerConfig(
      const envoy::extensions::transport_sockets::ssl_handshaker::v3::Config& proto_config,
      Server::Configuration::TransportSocketFactoryContext& factory_context);

  void init();

  const std::string& so_id() const { return config_.so_id(); }
  const envoy::config::core::v3::ConfigSource& sdsConfig() const { return config_.sds_config(); }
  Server::Configuration::TransportSocketFactoryContext& factoryContext() {
    return transport_factory_context_;
  };

  std::shared_ptr<TlsContext> getTlsContext(const std::string& sds_config_name);

#ifdef HACK_TESTING
  void removeProvider(const std::string& sds_config_name);
#endif

  std::chrono::milliseconds fetchCertTimeout() { return fetch_cert_timeout_; }
  std::chrono::milliseconds selectCertNameTimeout() { return select_cert_name_timeout_; }

  ABSL_MUST_USE_RESULT FetchCallbackHandle addFetchCertificateCallback(
      const std::string& sds_config_name, std::function<void(bool)> callback);

  void removeFetchCallback(const std::string& sds_config_name, FetchCallbackHandle cb_handle);

private:
  envoy::extensions::transport_sockets::ssl_handshaker::v3::Config config_;
  Server::Configuration::TransportSocketFactoryContextImpl transport_factory_context_;

  /*
   * Notice: we don't delete item now,
   * should make sure the callbacks are all triggered in the provider,
   * if we remove item in the feature,
   * otherwise, may segfault while remove callback iter.
   */
  absl::Mutex provider_mutex_;
  absl::node_hash_map<const std::string, DynamicTlsContextProviderSharedPtr>
      dynamic_tls_context_providers_ ABSL_GUARDED_BY(provider_mutex_);
  ;

  std::chrono::milliseconds fetch_cert_timeout_;
  std::chrono::milliseconds select_cert_name_timeout_;

  static const uint64_t SelectCertNameTimeoutMs = 1000;
  static const uint64_t FetchCertTimeoutMs = 15 * 1000;
};

enum class FailureReason {
  ResumeFetchCertFailed,
  ResumeSelectCertFailed,
  NoTlsContext,
  FetchTimedout,
  SelectTimedout,
};

enum class HandshakeState {
  Init,
  SelectCertName,
  GotCertName,
  FetchCert,
};

class CustomSslHandshakerImpl : public SslHandshakerImpl,
                                public std::enable_shared_from_this<CustomSslHandshakerImpl> {

public:
  CustomSslHandshakerImpl(bssl::UniquePtr<SSL> ssl_ptr, int ssl_extended_socket_info_index,
                          Ssl::HandshakeCallbacks* handshake_callbacks,
                          CustomSslHandshakerConfigSharedPtr config);

  ~CustomSslHandshakerImpl();

  Network::PostIoAction doHandshake() override;

  void selectCertCb(bool safe_thread, const char* name, int len);
  void fetchCertificateCallback();

private:
  bool isThreadSafe() { return handshakeCallbacks()->connection().dispatcher().isThreadSafe(); }
  Network::Connection& connection() { return handshakeCallbacks()->connection(); }
  Event::Dispatcher& dispatcher() { return handshakeCallbacks()->connection().dispatcher(); }

  bool fetchCertAsync();
  static int setCertCb(SSL* ssl, void* arg);
  int setCertCbInternal();
  void selectCert();

  void handleFailure(FailureReason reason);
  void disableTimeoutTimer();

  FetchCallbackHandle fetch_callback_handle_;
  Event::TimerPtr fetch_timeout_timer_;
  Event::TimerPtr select_cert_timeout_timer_;

  Dso::DsoInstance* dynamicLib_;
  CustomSslHandshakerConfigSharedPtr config_;
  std::string server_name_;
  std::string sds_config_name_;

  HandshakeState state_ = HandshakeState::Init;
};

class CustomSslHandshakerImplWeakPtrHolder {
public:
  CustomSslHandshakerImplWeakPtrHolder(std::weak_ptr<CustomSslHandshakerImpl> ptr) : ptr_(ptr) {}
  ~CustomSslHandshakerImplWeakPtrHolder(){};
  std::weak_ptr<CustomSslHandshakerImpl>& get() { return ptr_; }

private:
  std::weak_ptr<CustomSslHandshakerImpl> ptr_{};
};

} // namespace CustomSslHandshaker
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
