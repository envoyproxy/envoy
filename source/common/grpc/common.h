#pragma once

#include <cstdint>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/grpc/status.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/http/message.h"
#include "envoy/stats/stats.h"

#include "common/protobuf/protobuf.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Grpc {

class Exception : public EnvoyException {
public:
  Exception(const absl::optional<uint64_t>& grpc_status, const std::string& message)
      : EnvoyException(message), grpc_status_(grpc_status) {}

  const absl::optional<uint64_t> grpc_status_;
};

class Common {
public:
  /**
   * Returns the GrpcStatus code from a given set of trailers, if present.
   * @param trailers the trailers to parse.
   * @return absl::optional<Status::GrpcStatus> the parsed status code or InvalidCode if no valid
   * status is found.
   */
  static absl::optional<Status::GrpcStatus> getGrpcStatus(const Http::HeaderMap& trailers);

  /**
   * Returns the grpc-message from a given set of trailers, if present.
   * @param trailers the trailers to parse.
   * @return std::string the gRPC status message or empty string if grpc-message is not present in
   *         trailers.
   */
  static std::string getGrpcMessage(const Http::HeaderMap& trailers);

  /**
   * @param encode_headers supplies the function to encode response headers.
   * @param grpc_status gRPC status for grpc-status header.
   * @param message gRPC message for grpc-message header.
   */
  static void
  sendLocalReply(std::function<void(Http::HeaderMapPtr&& headers, bool end_stream)> encode_headers,
                 Status::GrpcStatus grpc_status, const std::string& message);

  /**
   * Charge a success/failure stat to a cluster/service/method.
   * @param cluster supplies the target cluster.
   * @param protocol supplies the downstream protocol in use, either gRPC or gRPC-Web.
   * @param grpc_service supplies the service name.
   * @param grpc_method supplies the method name.
   * @param grpc_status supplies the gRPC status.
   */
  static void chargeStat(const Upstream::ClusterInfo& cluster, const std::string& protocol,
                         const std::string& grpc_service, const std::string& grpc_method,
                         const Http::HeaderEntry* grpc_status);

  /**
   * Charge a success/failure stat to a cluster/service/method.
   * @param cluster supplies the target cluster.
   * @param protocol supplies the downstream protocol in use, either "grpc" or "grpc-web".
   * @param grpc_service supplies the service name.
   * @param grpc_method supplies the method name.
   * @param success supplies whether the call succeeded.
   */
  static void chargeStat(const Upstream::ClusterInfo& cluster, const std::string& protocol,
                         const std::string& grpc_service, const std::string& grpc_method,
                         bool success);

  /**
   * Charge a success/failure stat to a cluster/service/method.
   * @param cluster supplies the target cluster.
   * @param grpc_service supplies the service name.
   * @param grpc_method supplies the method name.
   * @param success supplies whether the call succeeded.
   */
  static void chargeStat(const Upstream::ClusterInfo& cluster, const std::string& grpc_service,
                         const std::string& grpc_method, bool success);

  /**
   * Resolve the gRPC service and method from the HTTP2 :path header.
   * @param path supplies the :path header.
   * @param service supplies the output pointer of the gRPC service.
   * @param method supplies the output pointer of the gRPC method.
   * @return bool true if both gRPC serve and method have been resolved successfully.
   */
  static bool resolveServiceAndMethod(const Http::HeaderEntry* path, std::string* service,
                                      std::string* method);

  /**
   * Serialize protobuf message.
   */
  static Buffer::InstancePtr serializeBody(const Protobuf::Message& message);

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

  /**
   * @return const std::string& type URL prefix.
   */
  static const std::string& typeUrlPrefix();

  /**
   * Prefix type URL to a qualified name.
   * @param qualified_name packagename.messagename.
   * @return qualified_name prefixed with typeUrlPrefix + "/".
   */
  static std::string typeUrl(const std::string& qualified_name);

private:
  static void checkForHeaderOnlyError(Http::Message& http_response);
};

} // namespace Grpc
} // namespace Envoy
