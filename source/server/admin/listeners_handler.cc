#include "source/server/admin/listeners_handler.h"

#include "envoy/admin/v3/listeners.pb.h"

#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/network/utility.h"
#include "source/server/admin/utils.h"

namespace Envoy {
namespace Server {

ListenersHandler::ListenersHandler(Server::Instance& server) : HandlerContextBase(server) {}

Http::Code ListenersHandler::handlerDrainListeners(Http::ResponseHeaderMap&,
                                                   Buffer::Instance& response,
                                                   AdminStream& admin_query) {
  const Http::Utility::QueryParams params = admin_query.queryParams();

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

Http::Code ListenersHandler::handlerListenerInfo(Http::ResponseHeaderMap& response_headers,
                                                 Buffer::Instance& response,
                                                 AdminStream& admin_query) {
  const Http::Utility::QueryParams query_params = admin_query.queryParams();
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
    Network::Utility::addressToProtobufAddress(
        *listener.get().listenSocketFactories()[0]->localAddress(),
        *listener_status.mutable_local_address());
    for (std::vector<Network::ListenSocketFactoryPtr>::size_type i = 1;
         i < listener.get().listenSocketFactories().size(); i++) {
      auto address = listener_status.add_additional_local_addresses();
      Network::Utility::addressToProtobufAddress(
          *listener.get().listenSocketFactories()[i]->localAddress(), *address);
    }
  }
  response.add(MessageUtil::getJsonStringFromMessageOrError(listeners, true)); // pretty-print
}

void ListenersHandler::writeListenersAsText(Buffer::Instance& response) {
  for (const auto& listener : server_.listenerManager().listeners()) {
    for (auto& socket_factory : listener.get().listenSocketFactories()) {
      response.add(fmt::format("{}::{}\n", listener.get().name(),
                               socket_factory->localAddress()->asString()));
    }
  }
}

} // namespace Server
} // namespace Envoy
