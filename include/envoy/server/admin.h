#pragma once

#include <functional>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/http/query_params.h"
#include "envoy/network/listen_socket.h"
#include "envoy/server/config_tracker.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

class AdminStream {
public:
  virtual ~AdminStream() = default;

  /**
   * @param end_stream set to false for streaming response. Default is true, which will
   * end the response when the initial handler completes.
   */
  virtual void setEndStreamOnComplete(bool end_stream) PURE;

  /**
   * @param cb callback to be added to the list of callbacks invoked by onDestroy() when stream
   * is closed.
   */
  virtual void addOnDestroyCallback(std::function<void()> cb) PURE;

  /**
   * @return Http::StreamDecoderFilterCallbacks& to be used by the handler to get HTTP request data
   * for streaming.
   */
  virtual Http::StreamDecoderFilterCallbacks& getDecoderFilterCallbacks() const PURE;

  /**
   * @return const Buffer::Instance* the fully buffered admin request if applicable.
   */
  virtual const Buffer::Instance* getRequestBody() const PURE;

  /**
   * @return Http::HeaderMap& to be used by handler to parse header information sent with the
   * request.
   */
  virtual const Http::RequestHeaderMap& getRequestHeaders() const PURE;

  /**
   * Return the HTTP/1 stream encoder options if applicable. If the stream is not HTTP/1 returns
   * absl::nullopt.
   */
  virtual Http::Http1StreamEncoderOptionsOptRef http1StreamEncoderOptions() PURE;
};

/**
 * This macro is used to add handlers to the Admin HTTP Endpoint. It builds
 * a callback that executes X when the specified admin handler is hit. This macro can be
 * used to add static handlers as in source/server/http/admin.cc and also dynamic handlers as
 * done in the RouteConfigProviderManagerImpl constructor in source/common/router/rds_impl.cc.
 */
#define MAKE_ADMIN_HANDLER(X)                                                                      \
  [this](absl::string_view path_and_query, Http::ResponseHeaderMap& response_headers,              \
         Buffer::Instance& data, Server::AdminStream& admin_stream) -> Http::Code {                \
    return X(path_and_query, response_headers, data, admin_stream);                                \
  }

/**
 * Global admin HTTP endpoint for the server.
 */
class Admin {
public:
  virtual ~Admin() = default;

  /**
   * Callback for admin URL handlers.
   * @param path_and_query supplies the path and query of the request URL.
   * @param response_headers enables setting of http headers (e.g., content-type, cache-control) in
   * the handler.
   * @param response supplies the buffer to fill in with the response body.
   * @param admin_stream supplies the filter which invoked the handler, enables the handler to use
   * its data.
   * @return Http::Code the response code.
   */
  using HandlerCb = std::function<Http::Code(
      absl::string_view path_and_query, Http::ResponseHeaderMap& response_headers,
      Buffer::Instance& response, AdminStream& admin_stream)>;

  /**
   * Add an admin handler.
   * @param prefix supplies the URL prefix to handle.
   * @param help_text supplies the help text for the handler.
   * @param callback supplies the callback to invoke when the prefix matches.
   * @param removable if true allows the handler to be removed via removeHandler.
   * @param mutates_server_state indicates whether callback will mutate server state.
   * @return bool true if the handler was added, false if it was not added.
   */
  virtual bool addHandler(const std::string& prefix, const std::string& help_text,
                          HandlerCb callback, bool removable, bool mutates_server_state) PURE;

  /**
   * Remove an admin handler if it is removable.
   * @param prefix supplies the URL prefix of the handler to delete.
   * @return bool true if the handler was removed, false if it was not removed.
   */
  virtual bool removeHandler(const std::string& prefix) PURE;

  /**
   * Obtain socket the admin endpoint is bound to.
   * @return Network::Socket& socket reference.
   */
  virtual const Network::Socket& socket() PURE;

  /**
   * @return ConfigTracker& tracker for /config_dump endpoint.
   */
  virtual ConfigTracker& getConfigTracker() PURE;

  /**
   * Expose this Admin console as an HTTP server.
   * @param access_log_path file path to write the HTTP request log to.
   * @param address_out_path file path to write the listening socket's address to.
   * @param address network address to bind and listen on.
   * @param listener_scope stats scope for the listener being started,
   */
  virtual void startHttpListener(const std::string& access_log_path,
                                 const std::string& address_out_path,
                                 Network::Address::InstanceConstSharedPtr address,
                                 const Network::Socket::OptionsSharedPtr& socket_options,
                                 Stats::ScopePtr&& listener_scope) PURE;

  /**
   * Executes an admin request with the specified query params. Note: this must
   * be called from Envoy's main thread.
   *
   * @param path_and_query the path and query of the admin URL.
   * @param method the HTTP method (POST or GET).
   * @param response_headers populated the response headers from executing the request,
   *     most notably content-type.
   * @param body populated with the response-body from the admin request.
   * @return Http::Code The HTTP response code from the admin request.
   */
  virtual Http::Code request(absl::string_view path_and_query, absl::string_view method,
                             Http::ResponseHeaderMap& response_headers, std::string& body) PURE;

  /**
   * Add this Admin's listener to the provided handler, if the listener exists.
   * Throws an exception if the listener does not exist.
   * @param handler the handler that will receive this Admin's listener.
   */
  virtual void addListenerToHandler(Network::ConnectionHandler* handler) PURE;
};

} // namespace Server
} // namespace Envoy
