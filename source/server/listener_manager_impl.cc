#include "server/listener_manager_impl.h"

#include "envoy/registry/registry.h"

#include "common/common/assert.h"
#include "common/json/config_schemas.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/utility.h"
#include "common/ssl/context_config_impl.h"

#include "server/configuration_impl.h"

namespace Envoy {
namespace Server {

std::list<Configuration::NetworkFilterFactoryCb>
ProdListenerComponentFactory::createFilterFactoryList_(
    const std::vector<Json::ObjectSharedPtr>& filters, Server::Instance& server,
    Configuration::FactoryContext& context) {
  std::list<Configuration::NetworkFilterFactoryCb> ret;
  for (size_t i = 0; i < filters.size(); i++) {
    std::string string_type = filters[i]->getString("type");
    std::string string_name = filters[i]->getString("name");
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

Network::ListenSocketPtr
ProdListenerComponentFactory::createListenSocket(Network::Address::InstanceConstSharedPtr address,
                                                 bool bind_to_port) {
  // For each listener config we share a single TcpListenSocket among all threaded listeners.
  // UdsListenerSockets are not managed and do not participate in hot restart as they are only
  // used for testing. First we try to get the socket from our parent if applicable.
  ASSERT(address->type() == Network::Address::Type::Ip);
  std::string addr = fmt::format("tcp://{}", address->asString());
  const int fd = server_.hotRestart().duplicateParentListenSocket(addr);
  if (fd != -1) {
    ENVOY_LOG(info, "obtained socket for address {} from parent", addr);
    return Network::ListenSocketPtr{new Network::TcpListenSocket(fd, address)};
  } else {
    return Network::ListenSocketPtr{new Network::TcpListenSocket(address, bind_to_port)};
  }
}

ListenerImpl::ListenerImpl(Instance& server, ListenerComponentFactory& factory,
                           const Json::Object& json)
    : Json::Validator(json, Json::Schema::LISTENER_SCHEMA), server_(server),
      address_(Network::Utility::resolveUrl(json.getString("address"))),
      global_scope_(server.stats().createScope("")),
      bind_to_port_(json.getBoolean("bind_to_port", true)),
      use_proxy_proto_(json.getBoolean("use_proxy_proto", false)),
      use_original_dst_(json.getBoolean("use_original_dst", false)),
      per_connection_buffer_limit_bytes_(
          json.getInteger("per_connection_buffer_limit_bytes", 1024 * 1024)) {

  // ':' is a reserved char in statsd. Do the translation here to avoid costly inline translations
  // later.
  std::string final_stat_name = fmt::format("listener.{}.", address_->asString());
  std::replace(final_stat_name.begin(), final_stat_name.end(), ':', '_');

  listener_scope_ = server.stats().createScope(final_stat_name);
  ENVOY_LOG(info, "  address={}", address_->asString());

  if (json.hasObject("ssl_context")) {
    Ssl::ContextConfigImpl context_config(*json.getObject("ssl_context"));
    ssl_context_ =
        server.sslContextManager().createSslServerContext(*listener_scope_, context_config);
  }

  filter_factories_ = factory.createFilterFactoryList(json.getObjectArray("filters"), *this);
  socket_ = factory.createListenSocket(address_, bind_to_port_);
}

bool ListenerImpl::createFilterChain(Network::Connection& connection) {
  return Configuration::FilterChainUtility::buildFilterChain(connection, filter_factories_);
}

void ListenerManagerImpl::addListener(const Json::Object& json) {
  listeners_.emplace_back(new ListenerImpl(server_, factory_, json));
}

std::list<std::reference_wrapper<Listener>> ListenerManagerImpl::listeners() {
  std::list<std::reference_wrapper<Listener>> ret;
  for (const auto& listener : listeners_) {
    ret.emplace_back(*listener);
  }
  return ret;
}

} // Server
} // Envoy
