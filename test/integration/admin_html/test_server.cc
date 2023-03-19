#include "source/common/filesystem/filesystem_impl.h"
#include "source/exe/main_common.h"
#include "source/server/admin/admin_html.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace {

/*std::string readFileOrDie(absl::string_view prefix, absl::string_view filename) {
  Filesystem::InstanceImpl file_system;
  std::string path = absl::StrCat(prefix, filename);
  TRY_ASSERT_MAIN_THREAD { return file_system.fileReadToEnd(path); }
  END_TRY
  catch (EnvoyException& e) {
    ENVOY_LOG_MISC(error, "Error reading file {}", e.what());
    return e.what();
  }
  }*/

Http::Code testCallback(Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
                        Server::AdminStream& admin_stream) {
  Http::Utility::QueryParams query_params = admin_stream.queryParams();
  auto iter = query_params.find("file");
  if (iter == query_params.end()) {
    response.add("query param 'file' missing");
    return Http::Code::BadRequest;
  }

  Filesystem::InstanceImpl file_system;
  std::string path = absl::StrCat("test/integration/", iter->second);
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

class DebugHtmlResourceProvider : public Server::AdminHtml::HtmlResourceProvider {
public:
  absl::string_view getResource(absl::string_view resource_name, std::string& buf) override {
    std::string path = absl::StrCat("source/server/admin/html/", resource_name);
    Filesystem::InstanceImpl file_system;
    TRY_ASSERT_MAIN_THREAD { buf = file_system.fileReadToEnd(path); }
    END_TRY
    catch (EnvoyException& e) {
      ENVOY_LOG_MISC(error, "Error reading file {}", e.what());
      buf = e.what();
    }
    return buf;
  }
};

} // namespace
} // namespace Envoy

/**
 * Envoy server with an additional '/test' admin endpoint for serving test
 * files.
 */
int main(int argc, char** argv) {
  if (argc > 1 && absl::string_view("-debug") == argv[1]) {
    Envoy::Server::AdminHtml::setHtmlResourceProvider(
        std::make_unique<Envoy::DebugHtmlResourceProvider>());
    argv[1] = argv[0];
    --argc;
    ++argv;
  }

  return Envoy::MainCommon::main(argc, argv, [](Envoy::Server::Instance& server) {
    Envoy::OptRef<Envoy::Server::Admin> admin = server.admin();
    if (admin.has_value()) {
      admin->addHandler("/test", "test file-serving endpoint", Envoy::testCallback, false, false);
    }
  });
}
