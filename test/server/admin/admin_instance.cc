#include "test/server/admin/admin_instance.h"

namespace Envoy {
namespace Server {

AdminInstanceTest::AdminInstanceTest()
    : address_out_path_(TestEnvironment::temporaryPath("admin.address")),
      cpu_profile_path_(TestEnvironment::temporaryPath("envoy.prof")),
      admin_(cpu_profile_path_, server_), request_headers_{{":path", "/"}},
      admin_filter_(admin_.createCallbackFunction()) {
  admin_.startHttpListener("/dev/null", address_out_path_,
                           Network::Test::getCanonicalLoopbackAddress(GetParam()), nullptr,
                           listener_scope_.createScope("listener.admin."));
  EXPECT_EQ(std::chrono::milliseconds(100), admin_.drainTimeout());
  admin_.tracingStats().random_sampling_.inc();
  EXPECT_TRUE(admin_.setCurrentClientCertDetails().empty());
  admin_filter_.setDecoderFilterCallbacks(callbacks_);
}

Http::Code AdminInstanceTest::runCallback(absl::string_view path_and_query,
                                          Http::ResponseHeaderMap& response_headers,
                                          Buffer::Instance& response, absl::string_view method,
                                          absl::string_view body) {
  if (!body.empty()) {
    request_headers_.setReferenceContentType(Http::Headers::get().ContentTypeValues.FormUrlEncoded);
    callbacks_.buffer_ = std::make_unique<Buffer::OwnedImpl>(body);
  }

  request_headers_.setMethod(method);
  admin_filter_.decodeHeaders(request_headers_, false);

  return admin_.runCallback(path_and_query, response_headers, response, admin_filter_);
}

Http::Code AdminInstanceTest::getCallback(absl::string_view path_and_query,
                                          Http::ResponseHeaderMap& response_headers,
                                          Buffer::Instance& response) {
  return runCallback(path_and_query, response_headers, response,
                     Http::Headers::get().MethodValues.Get);
}

Http::Code AdminInstanceTest::postCallback(absl::string_view path_and_query,
                                           Http::ResponseHeaderMap& response_headers,
                                           Buffer::Instance& response) {
  return runCallback(path_and_query, response_headers, response,
                     Http::Headers::get().MethodValues.Post);
}

} // namespace Server
} // namespace Envoy
