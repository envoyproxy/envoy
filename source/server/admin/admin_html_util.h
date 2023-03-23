#pragma once

#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/http/header_map.h"
#include "envoy/server/admin.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {
namespace AdminHtmlUtil {

/**
 * @param buf a buffer that may be used by the implementation to prepare the return value.
 * @return resource contents
 */
absl::string_view getResource(absl::string_view resource_name, std::string& buf);

/**
 * Renders the beginning of the help-table into the response buffer provided
 * in the constructor.
 */
void tableBegin(Buffer::Instance&);

/**
 * Renders the end of the help-table into the response buffer provided in the
 * constructor.
 */
void tableEnd(Buffer::Instance&);

void renderHead(Http::ResponseHeaderMap& response_headers, Buffer::Instance& response);

void finalize(Buffer::Instance& response);

/**
 * Renders a table row for a URL endpoint, including the name of the endpoint,
 * entries for each parameter, and help text.
 *
 * This must be called after renderTableBegin and before renderTableEnd. Any
 * number of URL Handlers can be rendered.
 *
 * @param response buffer to write the HTML for the handler
 * @param handler the URL handler.
 * @param query query params
 * @param index url handler's index.
 * @param submit_on_change by default, editing parameters does not cause a
 *        form-submit -- you have to click on the link or button first. This
 *        is useful for the admin home page which lays out all the parameters
 *        so users can tweak them before submitting. Setting to true, the form
 *        auto-submits when any parameters change, and does not have its own
 *        explicit submit button. This is used to enable the user to adjust
 *        query-parameters while visiting an html-rendered endpoint.
 * @param active indicates
 */
void urlHandler(Buffer::Instance& response, const Admin::UrlHandler& handler,
                OptRef<const Http::Utility::QueryParams> query, int index, bool submit_on_change,
                bool active);

// Overridable mechanism to provide resources for constructing HTML resources.
// This is used to facilitate interactive debugging by dynamically reading
// resource contents from the file system.
//
// Note: rather than creating a new interface here, we could have re-used
// Filesystem::Instance, however the current implementation of MemFileSystem is
// intended for tests, and it's simpler to create a much leaner new API rather
// than make a production-ready version of the full memory-based filesystem.
class HtmlResourceProvider {
public:
  virtual ~HtmlResourceProvider() = default;

  /**
   * @param buf a buffer that may be used by the implementation to prepare the return value.
   * @return resource contents
   */
  virtual absl::string_view getResource(absl::string_view resource_name, std::string& buf) PURE;
};

void setHtmlResourceProvider(std::unique_ptr<HtmlResourceProvider> resource_provider);

} // namespace AdminHtmlUtil
} // namespace Server
} // namespace Envoy
