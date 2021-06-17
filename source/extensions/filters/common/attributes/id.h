#include <atomic>
#include <chrono>
#include <iterator>
#include <map>
#include <memory>
#include <string>

#include "absl/strings/str_format.h"
#include "absl/types/variant.h"
#include "google/api/expr/v1alpha1/value.pb.h"

#include "envoy/access_log/access_log.h"
#include "envoy/buffer/buffer.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/stats/sink.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/network/filter.h"

#include "source/common/grpc/common.h"
#include "source/common/http/headers.h"
#include "source/common/protobuf/utility.h"
#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/common/fmt.h"
#include "source/common/common/lock_guard.h"
#include "source/common/http/header_map_impl.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Attributes {

// #define WASM_ROOT_TOKENS(_f)                                                                            \
//   _f(METADATA) _f(REQUEST) _f(RESPONSE) _f(CONNECTION) _f(UPSTREAM) _f(NODE) _f(SOURCE)            \
//       _f(DESTINATION) _f(LISTENER_DIRECTION) _f(LISTENER_METADATA) _f(CLUSTER_NAME)                \
//           _f(CLUSTER_METADATA) _f(ROUTE_NAME) _f(ROUTE_METADATA) _f(PLUGIN_NAME)                   \
//               _f(UPSTREAM_HOST_METADATA) _f(PLUGIN_ROOT_ID) _f(PLUGIN_VM_ID) _f(CONNECTION_ID)     \
//                   _f(FILTER_STATE)

#define ROOT_TOKENS(_f)                                                                            \
  _f(METADATA) _f(REQUEST) _f(RESPONSE) _f(CONNECTION) _f(UPSTREAM) _f(SOURCE) _f(DESTINATION)     \
      _f(FILTER_STATE)

#define REQUEST_TOKENS(_f)                                                                         \
  _f(PATH) _f(URL_PATH) _f(HOST) _f(SCHEME) _f(METHOD) _f(HEADERS) _f(REFERER) _f(USERAGENT)       \
      _f(TIME) _f(ID) _f(PROTOCOL) _f(DURATION) _f(SIZE) _f(TOTAL_SIZE)

#define RESPONSE_TOKENS(_f)                                                                        \
  _f(CODE) _f(CODE_DETAILS) _f(FLAGS) _f(GRPC_STATUS) _f(HEADERS) _f(TRAILERS) _f(SIZE)            \
      _f(TOTAL_SIZE)

#define SOURCE_TOKENS(_f) _f(ADDRESS) _f(PORT)

#define DESTINATION_TOKENS(_f) _f(ADDRESS) _f(PORT)

#define CONNECTION_TOKENS(_f)                                                                      \
  _f(ID) _f(MTLS) _f(REQUESTED_SERVER_NAME) _f(TLS_VERSION) _f(SUBJECT_LOCAL_CERTIFICATE)          \
      _f(SUBJECT_PEER_CERTIFICATE) _f(DNS_SAN_LOCAL_CERTIFICATE) _f(DNS_SAN_PEER_CERTIFICATE)      \
          _f(URI_SAN_LOCAL_CERTIFICATE) _f(URI_SAN_PEER_CERTIFICATE) _f(TERMINATION_DETAILS)

#define UPSTREAM_TOKENS(_f)                                                                        \
  _f(ADDRESS) _f(PORT) _f(TLS_VERSION) _f(SUBJECT_LOCAL_CERTIFICATE) _f(SUBJECT_PEER_CERTIFICATE)  \
      _f(DNS_SAN_LOCAL_CERTIFICATE) _f(DNS_SAN_PEER_CERTIFICATE) _f(URI_SAN_LOCAL_CERTIFICATE)     \
          _f(URI_SAN_PEER_CERTIFICATE) _f(LOCAL_ADDRESS) _f(TRANSPORT_FAILURE_REASON)

static inline absl::string_view downCase(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

#define _DECLARE(_t) _t,
enum class RootToken { ROOT_TOKENS(_DECLARE) };
enum class RequestToken { REQUEST_TOKENS(_DECLARE) };
enum class ResponseToken { RESPONSE_TOKENS(_DECLARE) };
enum class SourceToken { SOURCE_TOKENS(_DECLARE) };
enum class DestinationToken { DESTINATION_TOKENS(_DECLARE) };
enum class ConnectionToken { CONNECTION_TOKENS(_DECLARE) };
enum class UpstreamToken { UPSTREAM_TOKENS(_DECLARE) };
#undef _DECLARE

#define _PAIR(_t) {downCase(#_t), RootToken::_t},
static absl::flat_hash_map<absl::string_view, RootToken> root_tokens = {ROOT_TOKENS(_PAIR)};
#undef _PAIR

#define _PAIR(_t) {downCase(#_t), RequestToken::_t},
static absl::flat_hash_map<absl::string_view, RequestToken> request_tokens = {
    REQUEST_TOKENS(_PAIR)};
#undef _PAIR

#define _PAIR(_t) {downCase(#_t), ResponseToken::_t},
static absl::flat_hash_map<absl::string_view, ResponseToken> response_tokens = {
    RESPONSE_TOKENS(_PAIR)};
#undef _PAIR

#define _PAIR(_t) {downCase(#_t), SourceToken::_t},
static absl::flat_hash_map<absl::string_view, SourceToken> source_tokens = {SOURCE_TOKENS(_PAIR)};
#undef _PAIR

#define _PAIR(_t) {downCase(#_t), DestinationToken::_t},
static absl::flat_hash_map<absl::string_view, DestinationToken> destination_tokens = {
    DESTINATION_TOKENS(_PAIR)};
#undef _PAIR

#define _PAIR(_t) {downCase(#_t), ConnectionToken::_t},
static absl::flat_hash_map<absl::string_view, ConnectionToken> connection_tokens = {
    CONNECTION_TOKENS(_PAIR)};
#undef _PAIR

#define _PAIR(_t) {downCase(#_t), UpstreamToken::_t},
static absl::flat_hash_map<absl::string_view, UpstreamToken> upstream_tokens = {
    UPSTREAM_TOKENS(_PAIR)};
#undef _PAIR

using SubToken = absl::variant<RequestToken, ResponseToken, SourceToken, DestinationToken,
                               ConnectionToken, UpstreamToken>;

class AttributeId {
public:
  AttributeId(RootToken root, absl::optional<SubToken> sub) : root_token_(root), sub_token_(sub){};
  RootToken root() { return root_token_; };
  absl::optional<SubToken> sub() { return sub_token_; };
  bool sub(RequestToken& tok);
  bool sub(ResponseToken& tok);
  bool sub(SourceToken& tok);
  bool sub(DestinationToken& tok);
  bool sub(ConnectionToken& tok);
  bool sub(UpstreamToken& tok);

  static absl::optional<AttributeId> from_path(absl::string_view path);

private:
  const RootToken root_token_;
  const absl::optional<SubToken> sub_token_;
};

} // namespace Attributes
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy