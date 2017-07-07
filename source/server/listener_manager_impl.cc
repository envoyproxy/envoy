#include "server/listener_manager_impl.h"

#include "envoy/registry/registry.h"

#include "common/common/assert.h"
#include "common/json/config_schemas.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/utility.h"
#include "common/ssl/context_config_impl.h"

#include "server/configuration_impl.h" // TODO(mattklein123): Remove post 1.4.0

namespace Envoy {
namespace Server {

std::vector<Configuration::NetworkFilterFactoryCb>
ProdListenerComponentFactory::createFilterFactoryList_(
    const std::vector<Json::ObjectSharedPtr>& filters, Server::Instance& server,
    Configuration::FactoryContext& context) {
  std::vector<Configuration::NetworkFilterFactoryCb> ret;
  for (size_t i = 0; i < filters.size(); i++) {
    const std::string string_type = filters[i]->getString("type");
    const std::string string_name = filters[i]->getString("name");
    Json::ObjectSharedPtr config = filters[i]->getObject("config");
    ENVOY_LOG(info, "  filter #{}:", i);
    ENVOY_LOG(info, "    type: {}", string_type);
    ENVOY_LOG(info, "    name: {}", string_name);

    // Map filter type string to enum.
    Configuration::NetworkFilterType type;
    if (string_type == "read") {
      type = Configuration::NetworkFilterType::Read;
    } else if (string_type == "write") {
      type = Configuration::NetworkFilterType::Write;
    } else {
      ASSERT(string_type == "both");
      type = Configuration::NetworkFilterType::Both;
    }

    // Now see if there is a factory that will accept the config.
    Configuration::NamedNetworkFilterConfigFactory* factory =
        Registry::FactoryRegistry<Configuration::NamedNetworkFilterConfigFactory>::getFactory(
            string_name);
    if (factory != nullptr && factory->type() == type) {
      Configuration::NetworkFilterFactoryCb callback =
          factory->createFilterFactory(*config, context);
      ret.push_back(callback);
    } else {
      // DEPRECATED
      // This name wasn't found in the named map, so search in the deprecated list registry.
      bool found_filter = false;
      for (Configuration::NetworkFilterConfigFactory* config_factory :
           Configuration::MainImpl::filterConfigFactories()) {
        Configuration::NetworkFilterFactoryCb callback =
            config_factory->tryCreateFilterFactory(type, string_name, *config, server);
        if (callback) {
          ret.push_back(callback);
          found_filter = true;
          break;
        }
      }

      if (!found_filter) {
        throw EnvoyException(
            fmt::format("unable to create filter factory for '{}'/'{}'", string_name, string_type));
      }
    }
  }
  return ret;
}

Network::ListenSocketSharedPtr
ProdListenerComponentFactory::createListenSocket(Network::Address::InstanceConstSharedPtr address,
                                                 bool bind_to_port) {
  // For each listener config we share a single TcpListenSocket among all threaded listeners.
  // UdsListenerSockets are not managed and do not participate in hot restart as they are only
  // used for testing. First we try to get the socket from our parent if applicable.
  // TODO(mattklein123): UDS support.
  ASSERT(address->type() == Network::Address::Type::Ip);
  const std::string addr = fmt::format("tcp://{}", address->asString());
  const int fd = server_.hotRestart().duplicateParentListenSocket(addr);
  if (fd != -1) {
    ENVOY_LOG(info, "obtained socket for address {} from parent", addr);
    return std::make_shared<Network::TcpListenSocket>(fd, address);
  } else {
    return std::make_shared<Network::TcpListenSocket>(address, bind_to_port);
  }
}

ListenerImpl::ListenerImpl(const Json::Object& json, ListenerManagerImpl& parent,
                           const std::string& name, bool workers_started, uint64_t hash)
    : Json::Validator(json, Json::Schema::LISTENER_SCHEMA), parent_(parent),
      address_(Network::Utility::resolveUrl(json.getString("address"))),
      global_scope_(parent_.server_.stats().createScope("")),
      bind_to_port_(json.getBoolean("bind_to_port", true)),
      use_proxy_proto_(json.getBoolean("use_proxy_proto", false)),
      use_original_dst_(json.getBoolean("use_original_dst", false)),
      per_connection_buffer_limit_bytes_(
          json.getInteger("per_connection_buffer_limit_bytes", 1024 * 1024)),
      listener_tag_(parent_.factory_.nextListenerTag()), name_(name),
      workers_started_(workers_started), hash_(hash) {

  // ':' is a reserved char in statsd. Do the translation here to avoid costly inline translations
  // later.
  std::string final_stat_name = fmt::format("listener.{}.", address_->asString());
  std::replace(final_stat_name.begin(), final_stat_name.end(), ':', '_');
  listener_scope_ = parent_.server_.stats().createScope(final_stat_name);

  if (json.hasObject("ssl_context")) {
    Ssl::ContextConfigImpl context_config(*json.getObject("ssl_context"));
    ssl_context_ = parent_.server_.sslContextManager().createSslServerContext(*listener_scope_,
                                                                              context_config);
  }

  filter_factories_ =
      parent_.factory_.createFilterFactoryList(json.getObjectArray("filters"), *this);
}

ListenerImpl::~ListenerImpl() {
  // The filter factories may have pending initialize actions (like in the case of RDS). Those
  // actions will fire in the destructor to avoid blocking initial server startup. If we are using
  // a local init manager we should block the notification from trying to move us from warming to
  // active. This is done here explicitly by setting a boolean and then clearing the factory
  // vector for clarity.
  initialize_canceled_ = true;
  filter_factories_.clear();
}

bool ListenerImpl::createFilterChain(Network::Connection& connection) {
  return Configuration::FilterChainUtility::buildFilterChain(connection, filter_factories_);
}

void ListenerImpl::infoLog(const std::string& message) {
  ENVOY_LOG(info, "{}: name={}, hash={}, address={}", message, name_, hash_, address_->asString());
}

void ListenerImpl::initialize() {
  // If workers have already started, we shift from using the global init manager to using a local
  // per listener init manager. See ~ListenerImpl() for why we gate the onListenerWarmed() call
  // with initialize_canceled_.
  if (workers_started_) {
    dynamic_init_manager_.initialize([this]() -> void {
      if (!initialize_canceled_) {
        parent_.onListenerWarmed(*this);
      }
    });
  }
}

Init::Manager& ListenerImpl::initManager() {
  // See initialize() for why we choose different init managers to return.
  if (workers_started_) {
    return dynamic_init_manager_;
  } else {
    return parent_.server_.initManager();
  }
}

void ListenerImpl::setSocket(const Network::ListenSocketSharedPtr& socket) {
  ASSERT(!socket_);
  socket_ = socket;
}

ListenerManagerImpl::ListenerManagerImpl(Instance& server,
                                         ListenerComponentFactory& listener_factory,
                                         WorkerFactory& worker_factory)
    : server_(server), factory_(listener_factory) {
  for (uint32_t i = 0; i < std::max(1U, server.options().concurrency()); i++) {
    workers_.emplace_back(worker_factory.createWorker());
  }
}

bool ListenerManagerImpl::addOrUpdateListener(const Json::Object& json) {
  const std::string name = json.getString("name", server_.random().uuid());
  const uint64_t hash = json.hash();
  ENVOY_LOG(debug, "begin add/update listener: name={} hash={}", name, hash);

  auto existing_active_listener = getListenerByName(active_listeners_, name);
  auto existing_warming_listener = getListenerByName(warming_listeners_, name);

  // Do a quick hash check to see if we have a duplicate before going further. This check needs
  // to be done against both warming and active.
  // TODO(mattklein123): In v2 move away from hashes and just do an explicit proto equality check.
  if ((existing_warming_listener != warming_listeners_.end() &&
       (*existing_warming_listener)->hash() == hash) ||
      (existing_active_listener != active_listeners_.end() &&
       (*existing_active_listener)->hash() == hash)) {
    ENVOY_LOG(debug, "duplicate listener '{}'. no add/update", name);
    return false;
  }

  ListenerImplPtr new_listener(new ListenerImpl(json, *this, name, workers_started_, hash));
  ListenerImpl& new_listener_ref = *new_listener;

  // We mandate that a listener with the same name must have the same configured address. This
  // avoids confusion during updates and allows us to use the same bound address. Note that in
  // the case of port 0 binding, the new listener will implicitly use the same bound port from
  // the existing listener.
  if ((existing_warming_listener != warming_listeners_.end() &&
       *(*existing_warming_listener)->address() != *new_listener->address()) ||
      (existing_active_listener != active_listeners_.end() &&
       *(*existing_active_listener)->address() != *new_listener->address())) {
    const std::string message = fmt::format(
        "error updating listener: '{}' has a different address '{}' from existing listener", name,
        new_listener->address()->asString());
    ENVOY_LOG(warn, "{}", message);
    throw EnvoyException(message);
  }

  if (existing_warming_listener != warming_listeners_.end()) {
    // In this case we can just replace inline.
    ASSERT(workers_started_);
    new_listener->infoLog("update warming listener");
    new_listener->setSocket((*existing_warming_listener)->getSocket());
    *existing_warming_listener = std::move(new_listener);
  } else if (existing_active_listener != active_listeners_.end()) {
    // In this case we have no warming listener, so what we do depends on whether workers
    // have been started or not. Either way we get the socket from the existing listener.
    new_listener->setSocket((*existing_active_listener)->getSocket());
    if (workers_started_) {
      new_listener->infoLog("add warming listener");
      warming_listeners_.emplace_back(std::move(new_listener));
    } else {
      new_listener->infoLog("update active listener");
      *existing_active_listener = std::move(new_listener);
    }
  } else {
    // We have no warming or active listener so we need to make a new one. What we do depends on
    // whether workers have been started or not. Additionally, search through draining listeners
    // to see if there is a listener that has a socket bound to the address we are configured for.
    // This is an edge case, but may happen if a listener is removed and then added back with a same
    // or different name and intended to listen on the same address. This should work and not fail.
    Network::ListenSocketSharedPtr draining_listener_socket;
    {
      std::lock_guard<std::mutex> guard(draining_listeners_lock_);
      auto existing_draining_listener = std::find_if(
          draining_listeners_.cbegin(), draining_listeners_.cend(),
          [&new_listener](const DrainingListener& listener) {
            return *new_listener->address() == *listener.listener_->socket().localAddress();
          });
      if (existing_draining_listener != draining_listeners_.cend()) {
        draining_listener_socket = existing_draining_listener->listener_->getSocket();
      }
    }

    new_listener->setSocket(
        draining_listener_socket
            ? draining_listener_socket
            : factory_.createListenSocket(new_listener->address(), new_listener->bindToPort()));
    if (workers_started_) {
      new_listener->infoLog("add warming listener");
      warming_listeners_.emplace_back(std::move(new_listener));
    } else {
      new_listener->infoLog("add active listener");
      active_listeners_.emplace_back(std::move(new_listener));
    }
  }

  new_listener_ref.initialize();
  return true;
}

void ListenerManagerImpl::drainListener(ListenerImplPtr&& listener) {
  // TODO(mattklein123): Actually implement timed draining in a follow up. Currently we just
  // correctly synchronize removal across all workers.
  std::list<DrainingListener>::iterator draining_it;
  {
    std::lock_guard<std::mutex> guard(draining_listeners_lock_);
    draining_it = draining_listeners_.emplace(draining_listeners_.begin(), std::move(listener),
                                              workers_.size());
  }

  draining_it->listener_->infoLog("removing listener");
  for (const auto& worker : workers_) {
    worker->removeListener(*draining_it->listener_, [this, draining_it]() -> void {
      std::lock_guard<std::mutex> guard(draining_listeners_lock_);
      if (--draining_it->workers_pending_removal_ == 0) {
        draining_it->listener_->infoLog("listener removal complete");
        draining_listeners_.erase(draining_it);
      }
    });
  }
}

ListenerManagerImpl::ListenerList::iterator
ListenerManagerImpl::getListenerByName(ListenerList& listeners, const std::string& name) {
  auto ret = listeners.end();
  for (auto it = listeners.begin(); it != listeners.end(); ++it) {
    if ((*it)->name() == name) {
      // There should only ever be a single listener per name in the list. We could return faster
      // but take the opportunity to assert that fact.
      ASSERT(ret == listeners.end());
      ret = it;
    }
  }
  return ret;
}

std::vector<std::reference_wrapper<Listener>> ListenerManagerImpl::listeners() {
  std::vector<std::reference_wrapper<Listener>> ret;
  ret.reserve(active_listeners_.size());
  for (const auto& listener : active_listeners_) {
    ret.push_back(*listener);
  }
  return ret;
}

void ListenerManagerImpl::onListenerWarmed(ListenerImpl& listener) {
  auto existing_active_listener = getListenerByName(active_listeners_, listener.name());
  auto existing_warming_listener = getListenerByName(warming_listeners_, listener.name());
  (*existing_warming_listener)->infoLog("warm complete. updating active listener");
  if (existing_active_listener != active_listeners_.end()) {
    drainListener(std::move(*existing_active_listener));
    *existing_active_listener = std::move(*existing_warming_listener);
  } else {
    active_listeners_.emplace_back(std::move(*existing_warming_listener));
  }

  warming_listeners_.erase(existing_warming_listener);

  for (const auto& worker : workers_) {
    worker->addListener(listener);
  }
}

uint64_t ListenerManagerImpl::numConnections() {
  uint64_t num_connections = 0;
  for (const auto& worker : workers_) {
    num_connections += worker->numConnections();
  }

  return num_connections;
}

bool ListenerManagerImpl::removeListener(const std::string& name) {
  ENVOY_LOG(debug, "begin remove listener: name={}", name);

  auto existing_active_listener = getListenerByName(active_listeners_, name);
  auto existing_warming_listener = getListenerByName(warming_listeners_, name);
  if (existing_warming_listener == warming_listeners_.end() &&
      existing_active_listener == active_listeners_.end()) {
    ENVOY_LOG(debug, "unknown listener '{}'. no remove", name);
    return false;
  }

  // Destroy a warming listener directly.
  if (existing_warming_listener != warming_listeners_.end()) {
    (*existing_warming_listener)->infoLog("removing warming listener");
    warming_listeners_.erase(existing_warming_listener);
  }

  // If there is an active listener
  if (existing_active_listener != active_listeners_.end()) {
    drainListener(std::move(*existing_active_listener));
    active_listeners_.erase(existing_active_listener);
  }

  return true;
}

void ListenerManagerImpl::startWorkers(GuardDog& guard_dog) {
  ENVOY_LOG(warn, "all dependencies initialized. starting workers");
  ASSERT(!workers_started_);
  workers_started_ = true;
  for (const auto& worker : workers_) {
    ASSERT(warming_listeners_.empty());
    for (const auto& listener : active_listeners_) {
      worker->addListener(*listener);
    }
    worker->start(guard_dog);
  }
}

void ListenerManagerImpl::stopListeners() {
  for (const auto& worker : workers_) {
    worker->stopListeners();
  }
}

void ListenerManagerImpl::stopWorkers() {
  ASSERT(workers_started_);
  for (const auto& worker : workers_) {
    worker->stop();
  }
}

} // namespace Server
} // namespace Envoy
