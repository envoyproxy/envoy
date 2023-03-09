#include "source/common/filesystem/filesystem_impl.h"
#include "source/exe/main_common.h"

#include "absl/strings/match.h"

namespace Envoy {

static Http::Code testCallback(Http::ResponseHeaderMap& response_headers,
                               Buffer::Instance& response, Server::AdminStream& admin_stream) {
  Http::Utility::QueryParams query_params = admin_stream.queryParams();
  auto iter = query_params.find("file");
  if (iter == query_params.end()) {
    response.add("query param 'file' missing");
    return Http::Code::BadRequest;
  }

  Filesystem::InstanceImpl file_system;
  std::string path = absl::StrCat("test/integration/admin_html/", iter->second);
  TRY_ASSERT_MAIN_THREAD { response.add(file_system.fileReadToEnd(path)); }
  END_TRY
  catch (EnvoyException& e) {
    response.add(e.what());
    return Http::Code::NotFound;
  }
  if (absl::EndsWith(path, ".html")) {
    response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Html);
  } else if (absl::EndsWith(path, ".js")) {
    response_headers.setReferenceContentType("text/javascript");
  }
  return Http::Code::OK;
}

} // namespace Envoy

/**
 * Envoy server with an additional '/test' admin endpoint for serving test
 * files.
 */
int main(int argc, char** argv) {
  return Envoy::MainCommon::main(argc, argv, [](Envoy::Server::Instance& server) {
    Envoy::OptRef<Envoy::Server::Admin> admin = server.admin();
    if (admin.has_value()) {
      admin->addHandler("/test", "test file-serving endpoint", Envoy::testCallback, false, false);
    }
  });
}
