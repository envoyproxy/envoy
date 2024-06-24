#include "source/server/admin/init_dump_handler.h"

#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/network/utility.h"
#include "source/server/admin/utils.h"

namespace Envoy {
namespace Server {

InitDumpHandler::InitDumpHandler(Server::Instance& server) : HandlerContextBase(server) {}

Http::Code InitDumpHandler::handlerInitDump(Http::ResponseHeaderMap& response_headers,
                                            Buffer::Instance& response,
                                            AdminStream& admin_stream) const {
  const absl::optional<std::string> mask =
      Utility::nonEmptyQueryParam(admin_stream.queryParams(), "mask");

  envoy::admin::v3::UnreadyTargetsDumps dump = *dumpUnreadyTargets(mask);
  MessageUtil::redact(dump);

  response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
  response.add(MessageUtil::getJsonStringFromMessageOrError(dump, true)); // pretty-print
  return Http::Code::OK;
}

std::unique_ptr<envoy::admin::v3::UnreadyTargetsDumps>
InitDumpHandler::dumpUnreadyTargets(const absl::optional<std::string>& component) const {
  auto unready_targets_dumps = std::make_unique<envoy::admin::v3::UnreadyTargetsDumps>();

  if (component.has_value()) {
    if (component.value() == "listener") {
      dumpListenerUnreadyTargets(*unready_targets_dumps);
    }
    // More options for unready targets config dump.
  } else {
    // Dump all possible information of unready targets.
    dumpListenerUnreadyTargets(*unready_targets_dumps);
    // More unready targets to add into config dump.
  }
  return unready_targets_dumps;
}

void InitDumpHandler::dumpListenerUnreadyTargets(
    envoy::admin::v3::UnreadyTargetsDumps& unready_targets_dumps) const {
  std::vector<std::reference_wrapper<Network::ListenerConfig>> listeners;
  if (server_.listenerManager().isWorkerStarted()) {
    listeners = server_.listenerManager().listeners(ListenerManager::WARMING);
  } else {
    listeners = server_.listenerManager().listeners(ListenerManager::ACTIVE);
  }

  for (const auto& listener_config : listeners) {
    auto& listener = listener_config.get();
    listener.initManager().dumpUnreadyTargets(unready_targets_dumps);
  }
}

} // namespace Server
} // namespace Envoy
