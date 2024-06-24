#pragma once

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/buffer/buffer.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/server/admin.h"
#include "envoy/server/instance.h"

#include "source/server/admin/config_tracker_impl.h"
#include "source/server/admin/handler_ctx.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

class ConfigDumpHandler : public HandlerContextBase {

public:
  ConfigDumpHandler(ConfigTracker& config_tracker, Server::Instance& server);

  Http::Code handlerConfigDump(Http::ResponseHeaderMap& response_headers,
                               Buffer::Instance& response, AdminStream&) const;

private:
  absl::optional<std::pair<Http::Code, std::string>>
  addAllConfigToDump(envoy::admin::v3::ConfigDump& dump, const absl::optional<std::string>& mask,
                     const Matchers::StringMatcher& name_matcher, bool include_eds) const;
  /**
   * Add the config matching the passed resource to the passed config dump.
   * @return absl::nullopt on success, else the Http::Code and an error message that should be added
   * to the admin response.
   */
  absl::optional<std::pair<Http::Code, std::string>>
  addResourceToDump(envoy::admin::v3::ConfigDump& dump, const absl::optional<std::string>& mask,
                    const std::string& resource, const Matchers::StringMatcher& name_matcher,
                    bool include_eds) const;

  /**
   * Helper methods to add endpoints config
   */
  void addLbEndpoint(const Upstream::HostSharedPtr& host,
                     envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoint) const;

  ProtobufTypes::MessagePtr dumpEndpointConfigs(const Matchers::StringMatcher& name_matcher) const;

  ConfigTracker& config_tracker_;
};

} // namespace Server
} // namespace Envoy
