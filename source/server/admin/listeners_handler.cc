#include "server/admin/listeners_handler.h"

#include "envoy/admin/v3/listeners.pb.h"

#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/network/utility.h"

#include "server/admin/utils.h"

namespace Envoy {
namespace Server {

ListenersHandler::ListenersHandler(Server::Instance& server) : HandlerContextBase(server) {}

Http::Code ListenersHandler::handlerDrainListeners(absl::string_view url, Http::ResponseHeaderMap&,
                                                   Buffer::Instance& response, AdminStream&) {
  const Http::Utility::QueryParams params = Http::Utility::parseQueryString(url);

  ListenerManager::StopListenersType stop_listeners_type =
      params.find("inboundonly") != params.end() ? ListenerManager::StopListenersType::InboundOnly
                                                 : ListenerManager::StopListenersType::All;

  const bool graceful = params.find("graceful") != params.end();
  if (graceful) {
    // Ignore calls to /drain_listeners?graceful if the drain sequence has
    // already started.
    if (!server_.drainManager().draining()) {
      server_.drainManager().startDrainSequence([this, stop_listeners_type]() {
        server_.listenerManager().stopListeners(stop_listeners_type);
      });
    }
  } else {
    server_.listenerManager().stopListeners(stop_listeners_type);
  }

  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code ListenersHandler::handlerListenerInfo(absl::string_view url,
                                                 Http::ResponseHeaderMap& response_headers,
                                                 Buffer::Instance& response, AdminStream&) {
  const Http::Utility::QueryParams query_params = Http::Utility::parseQueryString(url);
  const auto format_value = Utility::formatParam(query_params);

  if (format_value.has_value() && format_value.value() == "json") {
    writeListenersAsJson(response);
    response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);
  } else {
    writeListenersAsText(response);
  }
  return Http::Code::OK;
}

void ListenersHandler::writeListenersAsJson(Buffer::Instance& response) {
  envoy::admin::v3::Listeners listeners;
  for (const auto& listener : server_.listenerManager().listeners()) {
    envoy::admin::v3::ListenerStatus& listener_status = *listeners.add_listener_statuses();
    listener_status.set_name(listener.get().name());
    Network::Utility::addressToProtobufAddress(*listener.get().listenSocketFactory().localAddress(),
                                               *listener_status.mutable_local_address());
  }
  response.add(MessageUtil::getJsonStringFromMessage(listeners, true)); // pretty-print
}

void ListenersHandler::writeListenersAsText(Buffer::Instance& response) {
  for (const auto& listener : server_.listenerManager().listeners()) {
    response.add(fmt::format("{}::{}\n", listener.get().name(),
                             listener.get().listenSocketFactory().localAddress()->asString()));
  }
}

} // namespace Server
} // namespace Envoy
