#include "server/admin/init_dump_handler.h"

#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/network/utility.h"

#include "server/admin/utils.h"

namespace Envoy {
namespace Server {

namespace {
// Helper method to get the mask parameter.
absl::optional<std::string> maskParam(const Http::Utility::QueryParams& params) {
  return Utility::queryParam(params, "mask");
}

} // namespace

InitDumpHandler::InitDumpHandler(Server::Instance& server) : HandlerContextBase(server) {}

Http::Code InitDumpHandler::handlerInitDump(absl::string_view url,
                                            Http::ResponseHeaderMap& response_headers,
                                            Buffer::Instance& response, AdminStream&) const {
  Http::Utility::QueryParams query_params = Http::Utility::parseAndDecodeQueryString(url);
  const auto mask = maskParam(query_params);

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
