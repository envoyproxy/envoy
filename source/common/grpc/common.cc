#include "common.h"

namespace Grpc {

const std::string Common::GRPC_CONTENT_TYPE{"application/grpc"};
const Http::LowerCaseString Common::GRPC_MESSAGE_HEADER{"grpc-message"};
const Http::LowerCaseString Common::GRPC_STATUS_HEADER{"grpc-status"};

void Common::chargeStat(Stats::Store& store, const std::string& cluster,
                        const std::string& grpc_service, const std::string& grpc_method,
                        bool success) {
  store.counter(fmt::format("cluster.{}.grpc.{}.{}.{}", cluster, grpc_service, grpc_method,
                            success ? "success" : "failure")).inc();
  store.counter(fmt::format("cluster.{}.grpc.{}.{}.total", cluster, grpc_service, grpc_method))
      .inc();
}

} // Grpc
