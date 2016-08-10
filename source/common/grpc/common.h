#pragma once

#include "envoy/http/header_map.h"
#include "envoy/stats/stats.h"

namespace Grpc {

class Common {
public:
  /**
   * Charge a success/failure stat to a cluster/service/method.
   * @param store supplies the stats store.
   * @param cluster supplies the target cluster.
   * @param grpc_service supplies the service name.
   * @param grpc_method supplies the method name.
   * @param success supplies whether the call succeeded.
   */
  static void chargeStat(Stats::Store& store, const std::string& cluster,
                         const std::string& grpc_service, const std::string& grpc_method,
                         bool success);

  static const std::string GRPC_CONTENT_TYPE;
  static const Http::LowerCaseString GRPC_MESSAGE_HEADER;
  static const Http::LowerCaseString GRPC_STATUS_HEADER;
};

} // Grpc
