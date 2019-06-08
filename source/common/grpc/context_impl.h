#pragma once

#include <cstdint>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/grpc/context.h"
#include "envoy/grpc/status.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/http/message.h"

#include "common/common/hash.h"
#include "common/grpc/status.h"
#include "common/protobuf/protobuf.h"
#include "common/stats/symbol_table_impl.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Grpc {

class Exception : public EnvoyException {
public:
  Exception(const absl::optional<uint64_t>& grpc_status, const std::string& message)
      : EnvoyException(message), grpc_status_(grpc_status) {}

  const absl::optional<uint64_t> grpc_status_;
};

struct Context::RequestNames {
  Stats::StatName service_; // supplies the service name.
  Stats::StatName method_;  // supplies the method name.
};

// TODO(jmarantz): Rename 'Common' to 'ContextImpl'.
class Common : public Context {
public:
  explicit Common(Stats::SymbolTable& symbol_table);

  /**
   * @param headers the headers to parse.
   * @return bool indicating whether content-type is gRPC.
   */
  static bool hasGrpcContentType(const Http::HeaderMap& headers);

  /**
   * @param headers the headers to parse.
   * @param bool indicating whether the header is at end_stream.
   * @return bool indicating whether the header is a gRPC response header
   */
  static bool isGrpcResponseHeader(const Http::HeaderMap& headers, bool end_stream);

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
   * Parse gRPC header 'grpc-timeout' value to a duration in milliseconds.
   * @param request_headers the header map from which to extract the value of 'grpc-timeout' header.
   *        If this header is missing the timeout corresponds to infinity. The header is encoded in
   *        maximum of 8 decimal digits and a char for the unit.
   * @return std::chrono::milliseconds the duration in milliseconds. A zero value corresponding to
   *         infinity is returned if 'grpc-timeout' is missing or malformed.
   */
  static std::chrono::milliseconds getGrpcTimeout(Http::HeaderMap& request_headers);

  /**
   * Encode 'timeout' into 'grpc-timeout' format.
   * @param timeout the duration in std::chrono::milliseconds.
   * @param value the HeaderString onto which format the timeout in 'grpc-timeout' format, up to
   *        8 decimal digits and a letter indicating the unit.
   */
  static void toGrpcTimeout(const std::chrono::milliseconds& timeout, Http::HeaderString& value);

  // Context
  void chargeStat(const Upstream::ClusterInfo& cluster, Protocol protocol,
                  const RequestNames& request_names, const Http::HeaderEntry* grpc_status) override;
  void chargeStat(const Upstream::ClusterInfo& cluster, Protocol protocol,
                  const RequestNames& request_names, bool success) override;
  void chargeStat(const Upstream::ClusterInfo& cluster, const RequestNames& request_names,
                  bool success) override;

  /**
   * Resolve the gRPC service and method from the HTTP2 :path header.
   * @param path supplies the :path header.
   * @param service supplies the output pointer of the gRPC service.
   * @param method supplies the output pointer of the gRPC method.
   * @return bool true if both gRPC serve and method have been resolved successfully.
   */
  absl::optional<RequestNames> resolveServiceAndMethod(const Http::HeaderEntry* path) override;

  /**
   * Serialize protobuf message with gRPC frame header.
   */
  static Buffer::InstancePtr serializeToGrpcFrame(const Protobuf::Message& message);

  /**
   * Serialize protobuf message. Without grpc header.
   */
  static Buffer::InstancePtr serializeMessage(const Protobuf::Message& message);

  /**
   * Prepare headers for protobuf service.
   */
  static Http::MessagePtr prepareHeaders(const std::string& upstream_cluster,
                                         const std::string& service_full_name,
                                         const std::string& method_name,
                                         const absl::optional<std::chrono::milliseconds>& timeout);

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

  /**
   * Prepend a gRPC frame header to a Buffer::Instance containing a single gRPC frame.
   * @param buffer containing the frame data which will be modified.
   */
  static void prependGrpcFrameHeader(Buffer::Instance& buffer);

  Stats::StatName successStatName(bool success) const { return success ? success_ : failure_; }
  Stats::StatName protocolStatName(Protocol protocol) const {
    return protocol == Context::Protocol::Grpc ? grpc_ : grpc_web_;
  }

  /**
   * Parse a Buffer::Instance into a Protobuf::Message.
   * @param buffer containing the data to be parsed.
   * @param proto the parsed proto.
   * @return bool true if the parse was successful.
   */
  static bool parseBufferInstance(Buffer::InstancePtr&& buffer, Protobuf::Message& proto);

private:
  static void checkForHeaderOnlyError(Http::Message& http_response);

  // Makes a stat name from a string, if we don't already have one for it.
  // This always takes a lock on mutex_, and if we haven't seen the name
  // before, it also takes a lock on the symbol table.
  //
  // TODO(jmarantz): See https://github.com/envoyproxy/envoy/pull/7008 for
  // a lock-free approach to creating dynamic stat-names based on requests.
  Stats::StatName makeDynamicStatName(absl::string_view name);

  Stats::SymbolTable& symbol_table_;
  mutable Thread::MutexBasicLockable mutex_;
  Stats::StatNamePool stat_name_pool_ GUARDED_BY(mutex_);
  StringMap<Stats::StatName> stat_name_map_ GUARDED_BY(mutex_);
  const Stats::StatName grpc_;
  const Stats::StatName grpc_web_;
  const Stats::StatName success_;
  const Stats::StatName failure_;
  const Stats::StatName total_;
  const Stats::StatName zero_;
};

} // namespace Grpc
} // namespace Envoy
