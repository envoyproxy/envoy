#pragma once

#include "envoy/common/exception.h"
#include "envoy/common/optional.h"
#include "envoy/http/header_map.h"
#include "envoy/http/message.h"
#include "envoy/stats/stats.h"

#include "google/protobuf/message.h"

namespace Grpc {

class Exception : public EnvoyException {
public:
  Exception(const Optional<uint64_t>& grpc_status, const std::string& message)
      : EnvoyException(message), grpc_status_(grpc_status) {}

  const Optional<uint64_t> grpc_status_;
};

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
  /**
   * Serialize protobuf message.
   */
  static Buffer::InstancePtr serializeBody(const google::protobuf::Message& message);

  /**
   * Prepare headers for protobuf service.
   */
  static Http::MessagePtr prepareHeaders(const std::string& upstream_cluster,
                                         const std::string& service_full_name,
                                         const std::string& method_name);

  /**
   * Basic validation of gRPC response, @throws Grpc::Exception in case of non successful response.
   */
  static void validateResponse(Http::Message& http_response);

  static const std::string GRPC_CONTENT_TYPE;

private:
  static void checkForHeaderOnlyError(Http::Message& http_response);
};

} // Grpc
