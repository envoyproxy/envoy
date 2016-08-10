#include "envoy/network/connection.h"
#include "envoy/server/instance.h"

#include "common/json/json_loader.h"
#include "common/mongo/proxy.h"
#include "server/configuration_impl.h"

namespace Server {
namespace Configuration {

/**
 * Config registration for the mongo proxy filter. @see NetworkFilterConfigFactory.
 */
class MongoProxyFilterConfigFactory : public NetworkFilterConfigFactory {
public:
  // NetworkFilterConfigFactory
  NetworkFilterFactoryCb tryCreateFilterFactory(NetworkFilterType type, const std::string& name,
                                                const Json::Object& config,
                                                Server::Instance& server) {
    if (type != NetworkFilterType::Both || name != "mongo_proxy") {
      return nullptr;
    }

    std::string stat_prefix = "mongo." + config.getString("stat_prefix") + ".";
    Mongo::AccessLogPtr access_log;
    if (config.hasObject("access_log")) {
      access_log.reset(new Mongo::AccessLog(server.api(), config.getString("access_log"),
                                            server.dispatcher(), server.accessLogLock(),
                                            server.stats()));
      server.accessLogManager().registerAccessLog(access_log);
    }

    return [stat_prefix, &server, access_log](Network::Connection& connection) -> void {
      connection.addFilter(Network::FilterPtr{
          new Mongo::ProdProxyFilter(stat_prefix, server.stats(), server.runtime(), access_log)});
    };
  }
};

/**
 * Static registration for the tcp_proxy filter. @see RegisterNetworkFilterConfigFactory.
 */
static RegisterNetworkFilterConfigFactory<MongoProxyFilterConfigFactory> registered_;

} // Configuration
} // Server
