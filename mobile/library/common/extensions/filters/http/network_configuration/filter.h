#pragma once

#include "envoy/http/filter.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "library/common/extensions/filters/http/network_configuration/filter.pb.h"
#include "library/common/network/configurator.h"
#include "library/common/stream_info/extra_stream_info.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace NetworkConfiguration {

/**
 * Filter to set upstream socket options based on network conditions.
 */
class NetworkConfigurationFilter final : public Http::PassThroughFilter,
                                         public Logger::Loggable<Logger::Id::filter> {
public:
  NetworkConfigurationFilter(Network::ConfiguratorSharedPtr network_configurator,
                             bool enable_interface_binding)
      : network_configurator_(network_configurator),
        extra_stream_info_(nullptr), // always set in setDecoderFilterCallbacks
        enable_interface_binding_(enable_interface_binding) {}

  // Http::StreamDecoderFilter
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;
  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap&, bool) override;
  // Http::StreamFilterBase
  Http::LocalErrorStatus onLocalReply(const LocalReplyData&) override;

private:
  Network::ConfiguratorSharedPtr network_configurator_;
  StreamInfo::ExtraStreamInfo* extra_stream_info_;
  bool enable_interface_binding_;
};

} // namespace NetworkConfiguration
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
