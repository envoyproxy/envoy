#pragma once

#include <cstdint>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/common/platform.h"
#include "envoy/grpc/status.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/http/message.h"

#include "common/common/hash.h"
#include "common/grpc/status.h"
#include "common/protobuf/protobuf.h"

#include "absl/types/optional.h"
#include "google/rpc/status.pb.h"

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
   * @param headers the headers to parse.
   * @return bool indicating whether content-type is gRPC.
   */
  static bool hasGrpcContentType(const Http::RequestOrResponseHeaderMap& headers);

  /**
   * @param headers the headers to parse.
   * @return bool indicating whether the header is a gRPC request header.
   * Currently headers are considered gRPC request headers if they have the gRPC
   * content type, and have a path header.
   */
  static bool isGrpcRequestHeaders(const Http::RequestHeaderMap& headers);

  /**
   * @param headers the headers to parse.
   * @param bool indicating whether the header is at end_stream.
   * @return bool indicating whether the header is a gRPC response header
   */
  static bool isGrpcResponseHeaders(const Http::ResponseHeaderMap& headers, bool end_stream);

  /**
   * Returns the GrpcStatus code from a given set of trailers, if present.
   * @param trailers the trailers to parse.
   * @param allow_user_status whether allow user defined grpc status.
   *        if this value is false, custom grpc status is regarded as invalid status
   * @return absl::optional<Status::GrpcStatus> the parsed status code or InvalidCode if no valid
   * status is found.
   */
  static absl::optional<Status::GrpcStatus>
  getGrpcStatus(const Http::ResponseHeaderOrTrailerMap& trailers, bool allow_user_defined = false);

  /**
   * Returns the GrpcStatus code from the set of trailers, headers, and StreamInfo, if present.
   * @param trailers the trailers to parse for a status code
   * @param headers the headers to parse if no status code was found in the trailers
   * @param info the StreamInfo to check for HTTP response code if no code was found in the trailers
   * or headers
   * @return absl::optional<Status::GrpcStatus> the parsed status code or absl::nullopt if no status
   * is found
   */
  static absl::optional<Status::GrpcStatus> getGrpcStatus(const Http::ResponseTrailerMap& trailers,
                                                          const Http::ResponseHeaderMap& headers,
                                                          const StreamInfo::StreamInfo& info,
                                                          bool allow_user_defined = false);

  /**
   * Returns the grpc-message from a given set of trailers, if present.
   * @param trailers the trailers to parse.
   * @return std::string the gRPC status message or empty string if grpc-message is not present in
   *         trailers.
   */
  static std::string getGrpcMessage(const Http::ResponseHeaderOrTrailerMap& trailers);

  /**
   * Returns the decoded google.rpc.Status message from a given set of trailers, if present.
   * @param trailers the trailers to parse.
   * @return std::unique_ptr<google::rpc::Status> the gRPC status message or empty pointer if no
   *         grpc-status-details-bin trailer found or it was invalid.
   */
  static absl::optional<google::rpc::Status>
  getGrpcStatusDetailsBin(const Http::HeaderMap& trailers);

  /**
   * Parse gRPC header 'grpc-timeout' value to a duration in milliseconds.
   * @param request_headers the header map from which to extract the value of 'grpc-timeout' header.
   *        If this header is missing the timeout corresponds to infinity. The header is encoded in
   *        maximum of 8 decimal digits and a char for the unit.
   * @return std::chrono::milliseconds the duration in milliseconds. A zero value corresponding to
   *         infinity is returned if 'grpc-timeout' is missing or malformed.
   */
  static std::chrono::milliseconds getGrpcTimeout(const Http::RequestHeaderMap& request_headers);

  /**
   * Encode 'timeout' into 'grpc-timeout' format in the grpc-timeout header.
   * @param timeout the duration in std::chrono::milliseconds.
   * @param headers the HeaderMap in which the grpc-timeout header will be set with the timeout in
   * 'grpc-timeout' format, up to 8 decimal digits and a letter indicating the unit.
   */
  static void toGrpcTimeout(const std::chrono::milliseconds& timeout,
                            Http::RequestHeaderMap& headers);

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
  static Http::RequestMessagePtr
  prepareHeaders(const std::string& upstream_cluster, const std::string& service_full_name,
                 const std::string& method_name,
                 const absl::optional<std::chrono::milliseconds>& timeout);

  /**
   * Basic validation of gRPC response, @throws Grpc::Exception in case of non successful response.
   */
  static void validateResponse(Http::ResponseMessage& http_response);

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

  /**
   * Parse a Buffer::Instance into a Protobuf::Message.
   * @param buffer containing the data to be parsed.
   * @param proto the parsed proto.
   * @return bool true if the parse was successful.
   */
  static bool parseBufferInstance(Buffer::InstancePtr&& buffer, Protobuf::Message& proto);

  struct RequestNames {
    absl::string_view service_;
    absl::string_view method_;
  };

  /**
   * Resolve the gRPC service and method from the HTTP2 :path header.
   * @param path supplies the :path header.
   * @return if both gRPC serve and method have been resolved successfully returns
   *   a populated RequestNames, otherwise returns an empty optional.
   * @note The return value is only valid as long as `path` is still valid and unmodified.
   */
  static absl::optional<RequestNames> resolveServiceAndMethod(const Http::HeaderEntry* path);

private:
  static void checkForHeaderOnlyError(Http::ResponseMessage& http_response);
};

} // namespace Grpc
} // namespace Envoy
