#include "source/server/admin/listeners_handler.h"

#include "envoy/admin/v3/listeners.pb.h"

#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/network/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/server/admin/utils.h"

namespace Envoy {
namespace Server {

ListenersHandler::ListenersHandler(Server::Instance& server) : HandlerContextBase(server) {}

Http::Code ListenersHandler::handlerDrainListeners(Http::ResponseHeaderMap&,
                                                   Buffer::Instance& response,
                                                   AdminStream& admin_query) {
  const Http::Utility::QueryParamsMulti params = admin_query.queryParams();

  ListenerManager::StopListenersType stop_listeners_type =
      params.getFirstValue("inboundonly").has_value()
          ? ListenerManager::StopListenersType::InboundOnly
          : ListenerManager::StopListenersType::All;

  const bool graceful = params.getFirstValue("graceful").has_value();
  const bool skip_exit = params.getFirstValue("skip_exit").has_value();
  // A non-graceful drain used to do nothing but stop the listeners: no drain sequence was started
  // and the connections those listeners already owned were never notified that a drain had begun,
  // so nothing was actually drained, and skip_exit -- which left nothing at all to do once stopping
  // the listeners was skipped -- was rejected. With this guard enabled a non-graceful drain drains
  // those connections just as a graceful one does; it simply does not wait out a drain period
  // before stopping the listeners, and skip_exit becomes meaningful for it.
  //
  // TODO(wbpcode): once this guard is removed, the two branches below can be collapsed: every drain
  // then drains the connections the same way and `graceful`/`skip_exit` only decide when, or
  // whether, the listeners are stopped.
  const bool non_graceful_drain_notifies_connections = Runtime::runtimeFeatureEnabled(
      "envoy.reloadable_features.non_graceful_drain_notifies_connections");
  if (skip_exit && !graceful && !non_graceful_drain_notifies_connections) {
    response.add("skip_exit requires graceful\n");
    return Http::Code::BadRequest;
  }

  auto direction = Network::DrainDirection::All;
  if (stop_listeners_type == ListenerManager::StopListenersType::InboundOnly) {
    direction = Network::DrainDirection::InboundOnly;
  }

  if (graceful) {
    // If draining(direction) returns true, it means:
    // 1. we are already draining
    // 2. That drain includes the direction we're being asked to drain
    // We should just return a 200
    if (const bool duplicate_drain = server_.drainManager().draining(direction); duplicate_drain) {
      response.add("OK\n");
      return Http::Code::OK;
    }

    // The start time and strategy are captured once here so every notified connection shares a
    // single, consistent drain timeline. A future query parameter can override the strategy for
    // this drain without touching the server-wide default.
    server_.listenerManager().onServerDrainStart(
        direction, Network::ConnectionDrainEvent{server_.api().timeSource().monotonicTime(),
                                                 server_.options().drainStrategy()});
    // This means either we aren't draining or we still have to do some work
    // (e.g. we were draining inbound only but now we're being asked to drain all)
    server_.drainManager().startDrainSequence(direction, [this, stop_listeners_type, skip_exit]() {
      if (!skip_exit) {
        // Stop the listeners after the drain duration because for admin initiated drain, there is
        // no another versions of the listeners to take care of the new connections. So Envoy still
        // accepts new connections during the drain duration to reduce the errors.
        server_.listenerManager().stopListeners(stop_listeners_type, {});
      }
    });
  } else {
    if (non_graceful_drain_notifies_connections) {
      // Notify before stopping the listeners so that a connection accepted in the window between
      // the two is notified as well: the active listener replays the event to connections accepted
      // after the drain started.
      //
      // The strategy is the server-wide default, as for a graceful drain: `graceful` only decides
      // whether the listeners keep accepting for a drain period before they are stopped, which is
      // independent of how the existing connections are drained.
      server_.listenerManager().onServerDrainStart(
          direction, Network::ConnectionDrainEvent{server_.api().timeSource().monotonicTime(),
                                                   server_.options().drainStrategy()});
      // Also put the drain manager into the draining state, so that consumers that poll
      // DrainDecision::drainClose() (rather than reacting to the push notification above) see the
      // drain too. No completion callback is needed: the listeners are stopped right below, or
      // deliberately left running when skip_exit is set.
      server_.drainManager().startDrainSequence(direction, []() {});
    }
    if (!skip_exit) {
      server_.listenerManager().stopListeners(stop_listeners_type, {});
    }
  }

  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code ListenersHandler::handlerListenerInfo(Http::ResponseHeaderMap& response_headers,
                                                 Buffer::Instance& response,
                                                 AdminStream& admin_query) {
  const Http::Utility::QueryParamsMulti query_params = admin_query.queryParams();
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
