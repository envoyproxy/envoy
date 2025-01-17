#include "contrib/sxg/filters/http/source/filter_config.h"

#include <string>

#include "envoy/http/codes.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"

#include "source/common/common/utility.h"
#include "source/common/http/headers.h"
#include "source/common/stats/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SXG {

template <class T>
const std::vector<std::string> initializeHeaderPrefixFilters(const T& filters_proto) {
  std::vector<std::string> filters;
  filters.reserve(filters_proto.size());

  for (const auto& filter : filters_proto) {
    filters.emplace_back(filter);
  }

  return filters;
}

FilterConfig::FilterConfig(const envoy::extensions::filters::http::sxg::v3alpha::SXG& proto_config,
                           TimeSource& time_source, std::shared_ptr<SecretReader> secret_reader,
                           const std::string& stat_prefix, Stats::Scope& scope)
    : stats_(generateStats(stat_prefix + "sxg.", scope)),
      duration_(proto_config.has_duration() ? proto_config.duration().seconds() : 604800UL),
      cbor_url_(proto_config.cbor_url()), validity_url_(proto_config.validity_url()),
      mi_record_size_(proto_config.mi_record_size() ? proto_config.mi_record_size() : 4096L),
      client_can_accept_sxg_header_(proto_config.client_can_accept_sxg_header().length() > 0
                                        ? proto_config.client_can_accept_sxg_header()
                                        : "x-client-can-accept-sxg"),
      should_encode_sxg_header_(proto_config.should_encode_sxg_header().length() > 0
                                    ? proto_config.should_encode_sxg_header()
                                    : "x-should-encode-sxg"),
      header_prefix_filters_(initializeHeaderPrefixFilters(proto_config.header_prefix_filters())),
      time_source_(time_source), secret_reader_(secret_reader) {}

} // namespace SXG
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
