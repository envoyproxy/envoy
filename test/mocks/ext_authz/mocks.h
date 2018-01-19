#pragma once

#include <string>
#include <vector>

#include "envoy/ext_authz/ext_authz.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace ExtAuthz {

class MockClient : public Client {
public:
  MockClient();
  ~MockClient();

  // ExtAuthz::Client
  MOCK_METHOD0(cancel, void());
  MOCK_METHOD3(check, void(RequestCallbacks& callbacks,
                           const envoy::api::v2::auth::CheckRequest& request,
                           Tracing::Span& parent_span));
};

class MockCheckRequestGen : public CheckRequestGenerator {
public:
	MockCheckRequestGen();
	~MockCheckRequestGen();

	// ExtAuthz::CheckRequestGenerator
	MOCK_METHOD3(createHttpCheck, void(const Envoy::Http::StreamDecoderFilterCallbacks* callbacks,

                                            const Envoy::Http::HeaderMap &headers,
		                            envoy::api::v2::auth::CheckRequest& request));
	MOCK_METHOD2(createTcpCheck, void(const Network::ReadFilterCallbacks* callbacks,
		                              envoy::api::v2::auth::CheckRequest& request));
};

} // namespace ExtAuthz
} // namespace Envoy
