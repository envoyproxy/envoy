
#pragma once

#include "envoy/admin/v3/init_dump.pb.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/server/admin.h"
#include "envoy/server/instance.h"

#include "source/server/admin/handler_ctx.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

class InitDumpHandler : public HandlerContextBase {

public:
  InitDumpHandler(Server::Instance& server);

  Http::Code handlerInitDump(Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
                             AdminStream&) const;

private:
  /**
   * Helper methods for the /init_dump url handler to add unready targets information.
   */
  std::unique_ptr<envoy::admin::v3::UnreadyTargetsDumps>
  dumpUnreadyTargets(const absl::optional<std::string>& target) const;

  /**
   * Helper methods for the /init_dump url handler to add unready targets config of listeners.
   */
  void
  dumpListenerUnreadyTargets(envoy::admin::v3::UnreadyTargetsDumps& unready_targets_dumps) const;
};

} // namespace Server
} // namespace Envoy
