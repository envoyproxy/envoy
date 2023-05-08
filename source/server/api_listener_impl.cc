#include "source/server/api_listener_impl.h"

#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/http/conn_manager_impl.h"
#include "source/common/network/resolver_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/network/http_connection_manager/config.h"

namespace Envoy {
namespace Server {

bool isQuic(const envoy::config::listener::v3::Listener& config) {
  return config.has_udp_listener_config() && config.udp_listener_config().has_quic_options();
}

ApiListenerImplBase::ApiListenerImplBase(const envoy::config::listener::v3::Listener& config,
                                         Server::Instance& server, const std::string& name)
    : config_(config), name_(name),
      address_(Network::Address::resolveProtoAddress(config.address())),
      global_scope_(server.stats().createScope("")),
      listener_scope_(server.stats().createScope(fmt::format("listener.api.{}.", name_))),
      factory_context_(server, config_, *this, *global_scope_, *listener_scope_, isQuic(config)) {}

void ApiListenerImplBase::SyntheticReadCallbacks::SyntheticConnection::raiseConnectionEvent(
    Network::ConnectionEvent event) {
  for (Network::ConnectionCallbacks* callback : callbacks_) {
    callback->onEvent(event);
  }
}

class HttpApiListener::HttpConnectionManagerState : public ThreadLocal::ThreadLocalObject {
public:
  explicit HttpConnectionManagerState(
      std::unique_ptr<SyntheticReadCallbacks> read_callbacks,
      std::function<Http::ApiListenerPtr(Network::ReadFilterCallbacks&)>
          http_connection_manager_factory)
      : read_callbacks_(std::move(read_callbacks)),
        http_connection_manager_factory_(http_connection_manager_factory) {}

  ~HttpConnectionManagerState() override {
    // The Http::ConnectionManagerImpl is a callback target for the
    // read_callback_.connection_. By raising connection closure,
    // Http::ConnectionManagerImpl::onEvent is fired. In that case the
    // Http::ConnectionManagerImpl will reset any ActiveStreams it has.
    read_callbacks_->connection_.raiseConnectionEvent(Network::ConnectionEvent::RemoteClose);
  }

  Http::ApiListener& httpApiListener() {
    if (http_api_listener_ == nullptr) {
      // We need it initialize this lazily as this function depends on other
      // thread-local state already being set up.
      http_api_listener_ = http_connection_manager_factory_(*read_callbacks_);
    }
    return *http_api_listener_;
  }

  SyntheticReadCallbacks& readCallbacks() { return *read_callbacks_; }

private:
  std::unique_ptr<SyntheticReadCallbacks> read_callbacks_;

  // Need to store the factory due to the shared_ptrs that need to be kept
  // alive: date provider, route config manager, scoped route config manager.
  std::function<Http::ApiListenerPtr(Network::ReadFilterCallbacks&)>
      http_connection_manager_factory_;

  // Http::ServerConnectionCallbacks is the API surface that this class provides
  // via its handle().
  std::unique_ptr<Http::ApiListener> http_api_listener_;
};

HttpApiListener::HttpApiListener(const envoy::config::listener::v3::Listener& config,
                                 Server::Instance& server, const std::string& name)
    : ApiListenerImplBase(config, server, name) {
  std::function<Http::ApiListenerPtr(Network::ReadFilterCallbacks&)>
      http_connection_manager_factory;

  if (config.api_listener().api_listener().type_url() ==
      absl::StrCat("type.googleapis.com/",
                   envoy::extensions::filters::network::http_connection_manager::v3::
                       EnvoyMobileHttpConnectionManager::descriptor()
                           ->full_name())) {
    auto typed_config = MessageUtil::anyConvertAndValidate<
        envoy::extensions::filters::network::http_connection_manager::v3::
            EnvoyMobileHttpConnectionManager>(config.api_listener().api_listener(),
                                              factory_context_.messageValidationVisitor());

    http_connection_manager_factory = Envoy::Extensions::NetworkFilters::HttpConnectionManager::
        HttpConnectionManagerFactory::createHttpConnectionManagerFactoryFromProto(
            typed_config.config(), factory_context_, false);
  } else {
    auto typed_config = MessageUtil::anyConvertAndValidate<
        envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager>(
        config.api_listener().api_listener(), factory_context_.messageValidationVisitor());

    http_connection_manager_factory =
        Envoy::Extensions::NetworkFilters::HttpConnectionManager::HttpConnectionManagerFactory::
            createHttpConnectionManagerFactoryFromProto(typed_config, factory_context_, true);
  }

  tls_ = ThreadLocal::TypedSlot<HttpConnectionManagerState>::makeUnique(server.threadLocal());
  tls_->set([this, http_connection_manager_factory](
                Event::Dispatcher& dispatcher) -> std::shared_ptr<HttpConnectionManagerState> {
    auto read_callbacks = std::make_unique<SyntheticReadCallbacks>(*this, dispatcher);
    return std::make_shared<HttpConnectionManagerState>(std::move(read_callbacks),
                                                        http_connection_manager_factory);
  });
}

Http::ApiListenerOptRef HttpApiListener::http() {
  OptRef<HttpConnectionManagerState> http_connection_manager_state = tls_->get();
  return Http::ApiListenerOptRef(std::ref(http_connection_manager_state->httpApiListener()));
}

void HttpApiListener::shutdown() { tls_.reset(); }

Network::ReadFilterCallbacks& HttpApiListener::readCallbacksForTest() {
  OptRef<HttpConnectionManagerState> http_connection_manager_state = tls_->get();
  return http_connection_manager_state->readCallbacks();
}

} // namespace Server
} // namespace Envoy
