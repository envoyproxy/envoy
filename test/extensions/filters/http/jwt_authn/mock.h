#pragma once

#include <memory>

#include "extensions/filters/http/jwt_authn/authenticator.h"
#include "extensions/filters/http/jwt_authn/verifier.h"

#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"

using ::google::jwt_verify::Status;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

class MockAuthFactory : public AuthFactory {
public:
  MOCK_CONST_METHOD3(create, AuthenticatorPtr(const ::google::jwt_verify::CheckAudience*,
                                              const absl::optional<std::string>&, bool));
};

class MockAuthenticator : public Authenticator {
public:
  MOCK_METHOD4(doVerify, void(Http::HeaderMap& headers, std::vector<JwtLocationConstPtr>* tokens,
                              SetPayloadCallback set_payload_cb, AuthenticatorCallback callback));

  void verify(Http::HeaderMap& headers, std::vector<JwtLocationConstPtr>&& tokens,
              SetPayloadCallback set_payload_cb, AuthenticatorCallback callback) {
    doVerify(headers, &tokens, std::move(set_payload_cb), std::move(callback));
  }

  MOCK_METHOD0(onDestroy, void());
};

class MockVerifierCallbacks : public Verifier::Callbacks {
public:
  MOCK_METHOD1(setPayload, void(const ProtobufWkt::Struct& payload));
  MOCK_METHOD1(onComplete, void(const Status& status));
};

class MockVerifier : public Verifier {
public:
  MOCK_CONST_METHOD1(verify, void(ContextSharedPtr context));
};

class MockExtractor : public Extractor {
public:
  MOCK_CONST_METHOD1(extract, std::vector<JwtLocationConstPtr>(const Http::HeaderMap& headers));
  MOCK_CONST_METHOD1(sanitizePayloadHeaders, void(Http::HeaderMap& headers));
};

// A mock HTTP upstream with response body.
class MockUpstream {
public:
  MockUpstream(Upstream::MockClusterManager& mock_cm, const std::string& response_body)
      : request_(&mock_cm.async_client_), response_body_(response_body) {
    ON_CALL(mock_cm.async_client_, send_(_, _, _))
        .WillByDefault(
            Invoke([this](Http::MessagePtr&, Http::AsyncClient::Callbacks& cb,
                          const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
              Http::MessagePtr response_message(new Http::ResponseMessageImpl(
                  Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
              response_message->body() = std::make_unique<Buffer::OwnedImpl>(response_body_);
              cb.onSuccess(std::move(response_message));
              called_count_++;
              return &request_;
            }));
  }

  int called_count() const { return called_count_; }

private:
  Http::MockAsyncClientRequest request_;
  std::string response_body_;
  int called_count_{};
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
