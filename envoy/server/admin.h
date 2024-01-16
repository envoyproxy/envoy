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

  /**
   * Construct query-param map for the stream, using the URL-specified params,
   * or the request data if that is url-form-encoded.
   *
   * @param The query name/value map.
   */
  virtual Http::Utility::QueryParamsMulti queryParams() const PURE;
};

/**
 * This macro is used to add handlers to the Admin HTTP Endpoint. It builds
 * a callback that executes X when the specified admin handler is hit. This macro can be
 * used to add static handlers as in source/server/admin/admin.cc and also dynamic handlers as
 * done in the RouteConfigProviderManagerImpl constructor in source/common/router/rds_impl.cc.
 */
#define MAKE_ADMIN_HANDLER(X)                                                                      \
  [this](Http::ResponseHeaderMap& response_headers, Buffer::Instance& data,                        \
         Server::AdminStream& admin_stream) -> Http::Code {                                        \
    return X(response_headers, data, admin_stream);                                                \
  }

/**
 * Global admin HTTP endpoint for the server, holding a map from URL prefixes to
 * handlers. When an HTTP request arrives at the admin port, the URL is linearly
 * prefixed-matched against an ordered list of handlers. When a match is found,
 * the handler is used to generate a Request.
 *
 * Requests are capable of streaming out content to the client, however, most
 * requests are delivered all at once. The implementation supplies adapters for
 * simplifying the creation of streaming requests based on a simple callback
 * that takes a URL and generates response headers and response body.
 *
 * A Taxonomy of the major types involved may help clarify:
 *   Request     a class holding state for streaming admin content to clients.
 *               These are re-created for each request.
 *   Handler     a class that holds context for a family of admin requests,
 *               supplying one-shot callbacks for non-streamed responses, and
 *               for generating Request objects directly for streamed responses.
 *               These have the same lifetime as Admin objects.
 *   Admin       Holds the ordered list of handlers to be prefix-matched.
 */
class Admin {
public:
  virtual ~Admin() = default;

  // Describes a parameter for an endpoint. This structure is used when
  // admin-html has not been disabled to populate an HTML form to enable a
  // visitor to the admin console to intuitively specify query-parameters for
  // each endpoint. The parameter descriptions also appear in the /help
  // endpoint, independent of how Envoy is compiled.
  struct ParamDescriptor {
    enum class Type { Boolean, String, Enum };
    Type type_;
    std::string id_;   // HTML form ID and query-param name (JS var name rules).
    std::string help_; // Rendered into home-page HTML and /help text.
    std::vector<absl::string_view> enum_choices_{};
  };
  using ParamDescriptorVec = std::vector<ParamDescriptor>;

  // Represents a request for admin endpoints, enabling streamed responses.
  class Request {
  public:
    virtual ~Request() = default;

    /**
     * Initiates a handler. The URL can be supplied to the constructor if needed.
     *
     * @param response_headers successful text responses don't need to modify this,
     *        but if we want to respond with (e.g.) JSON or HTML we can can set
     *        those here.
     * @return the HTTP status of the response.
     */
    virtual Http::Code start(Http::ResponseHeaderMap& response_headers) PURE;

    /**
     * Adds the next chunk of data to the response. Note that nextChunk can
     * return 'true' but not add any data to the response, in which case a chunk
     * is not sent, and a subsequent call to nextChunk can be made later,
     * possibly after a post() or low-watermark callback on the http filter.
     *
     * It is not necessary for the caller to drain the response after each call;
     * it can leave the data in response if it's necessary to buffer the entire
     * response prior to sending it to the network. It is preferable to stream
     * the data out, draining the response buffer on each call, but not
     * required.
     *
     * @param response a buffer in which to write the chunk
     * @return whether or not any chunks follow this one.
     */
    virtual bool nextChunk(Buffer::Instance& response) PURE;
  };
  using RequestPtr = std::unique_ptr<Request>;

  /**
   * Lambda to generate a Request.
   */
  using GenRequestFn = std::function<RequestPtr(AdminStream&)>;

  /**
   * Individual admin handler including prefix, help text, and callback.
   */
  struct UrlHandler {
    const std::string prefix_;
    const std::string help_text_;
    const GenRequestFn handler_;
    const bool removable_;
    const bool mutates_server_state_;
    const ParamDescriptorVec params_{};
  };

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
  using HandlerCb =
      std::function<Http::Code(Http::ResponseHeaderMap& response_headers,
                               Buffer::Instance& response, AdminStream& admin_stream)>;

  /**
   * Add a legacy admin handler where the entire response is written in
   * one chunk.
   *
   * @param prefix supplies the URL prefix to handle.
   * @param help_text supplies the help text for the handler.
   * @param callback supplies the callback to invoke when the prefix matches.
   * @param removable if true allows the handler to be removed via removeHandler.
   * @param mutates_server_state indicates whether callback will mutate server state.
   * @param params command parameter descriptors.
   * @return bool true if the handler was added, false if it was not added.
   */
  virtual bool addHandler(const std::string& prefix, const std::string& help_text,
                          HandlerCb callback, bool removable, bool mutates_server_state,
                          const ParamDescriptorVec& params = {}) PURE;

  /**
   * Adds a an chunked admin handler.
   *
   * @param prefix supplies the URL prefix to handle.
   * @param help_text supplies the help text for the handler.
   * @param gen_request supplies the callback to generate a Request.
   * @param removable if true allows the handler to be removed via removeHandler.
   * @param mutates_server_state indicates whether callback will mutate server state.
   * @param params command parameter descriptors.
   * @return bool true if the handler was added, false if it was not added.
   */
  virtual bool addStreamingHandler(const std::string& prefix, const std::string& help_text,
                                   GenRequestFn gen_request, bool removable,
                                   bool mutates_server_state,
                                   const ParamDescriptorVec& params = {}) PURE;

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
   * @param access_logs access_logs list of file loggers to write the HTTP request log to.
   * @param address network address to bind and listen on.
   * @param socket_options socket options to apply to the listening socket.
   */
  virtual void startHttpListener(std::list<AccessLog::InstanceSharedPtr> access_logs,
                                 Network::Address::InstanceConstSharedPtr address,
                                 Network::Socket::OptionsSharedPtr socket_options) PURE;

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

  /**
   * @return the number of worker threads to run in the server.
   */
  virtual uint32_t concurrency() const PURE;

  /**
   * Makes a request for streamed static text. The version that takes the
   * Buffer::Instance& transfers the content from the passed-in buffer.
   *
   * @param response_text the text to populate response with
   * @param code the Http::Code for the response
   * @return the request
   */
  static RequestPtr makeStaticTextRequest(absl::string_view response_text, Http::Code code);
  static RequestPtr makeStaticTextRequest(Buffer::Instance& response_text, Http::Code code);

  /**
   * Closes the listening socket for the admin.
   */
  virtual void closeSocket() PURE;
};

} // namespace Server
} // namespace Envoy
