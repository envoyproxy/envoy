#pragma once

#include <memory>

#include "source/common/http/message_impl.h"
#include "source/extensions/filters/http/jwt_authn/authenticator.h"
#include "source/extensions/filters/http/jwt_authn/verifier.h"

#include "test/mocks/upstream/cluster_manager.h"

#include "gmock/gmock.h"

using ::google::jwt_verify::Status;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

class MockAuthFactory : public AuthFactory {
public:
  MOCK_METHOD(AuthenticatorPtr, create,
              (const ::google::jwt_verify::CheckAudience*, const absl::optional<std::string>&, bool,
               bool),
              (const));
};

class MockAuthenticator : public Authenticator {
public:
  MOCK_METHOD(void, doVerify,
              (Http::HeaderMap & headers, Tracing::Span& parent_span,
               std::vector<JwtLocationConstPtr>* tokens,
               SetExtractedJwtDataCallback set_extracted_jwt_data_cb,
               AuthenticatorCallback callback));

  void verify(Http::HeaderMap& headers, Tracing::Span& parent_span,
              std::vector<JwtLocationConstPtr>&& tokens,
              SetExtractedJwtDataCallback set_extracted_jwt_data_cb,
              AuthenticatorCallback callback) override {
    doVerify(headers, parent_span, &tokens, std::move(set_extracted_jwt_data_cb),
             std::move(callback));
  }

  MOCK_METHOD(void, onDestroy, ());
};

class MockVerifierCallbacks : public Verifier::Callbacks {
public:
  MOCK_METHOD(void, setExtractedData, (const ProtobufWkt::Struct& payload));
  MOCK_METHOD(void, onComplete, (const Status& status));
};

class MockVerifier : public Verifier {
public:
  MOCK_METHOD(void, verify, (ContextSharedPtr context), (const));
};

class MockExtractor : public Extractor {
public:
  MOCK_METHOD(std::vector<JwtLocationConstPtr>, extract, (const Http::RequestHeaderMap& headers),
              (const));
  MOCK_METHOD(void, sanitizeHeaders, (Http::HeaderMap & headers), (const));
};

class MockJwtCache : public JwtCache {
public:
  MOCK_METHOD(::google::jwt_verify::Jwt*, lookup, (const std::string&), ());
  MOCK_METHOD(void, insert, (const std::string&, std::unique_ptr<::google::jwt_verify::Jwt>&&), ());
};

class MockJwksData : public JwksCache::JwksData {
public:
  MockJwksData() {
    ON_CALL(*this, areAudiencesAllowed(_)).WillByDefault(::testing::Return(true));
    ON_CALL(*this, getJwtProvider()).WillByDefault(::testing::ReturnRef(jwt_provider_));
    ON_CALL(*this, isExpired()).WillByDefault(::testing::Return(false));
    ON_CALL(*this, getJwtCache()).WillByDefault(::testing::ReturnRef(jwt_cache_));
  }

  MOCK_METHOD(bool, areAudiencesAllowed, (const std::vector<std::string>&), (const));
  MOCK_METHOD(const envoy::extensions::filters::http::jwt_authn::v3::JwtProvider&, getJwtProvider,
              (), (const));
  MOCK_METHOD(const ::google::jwt_verify::Jwks*, getJwksObj, (), (const));
  MOCK_METHOD(bool, isExpired, (), (const));
  MOCK_METHOD(const ::google::jwt_verify::Jwks*, setRemoteJwks, (JwksConstPtr &&), ());
  MOCK_METHOD(JwtCache&, getJwtCache, (), ());

  envoy::extensions::filters::http::jwt_authn::v3::JwtProvider jwt_provider_;
  ::testing::NiceMock<MockJwtCache> jwt_cache_;
};

class MockJwksCache : public JwksCache {
public:
  MockJwksCache() : stats_(generateMockStats(stats_store_)) {
    ON_CALL(*this, findByIssuer(_)).WillByDefault(::testing::Return(&jwks_data_));
    ON_CALL(*this, findByProvider(_)).WillByDefault(::testing::Return(&jwks_data_));
    ON_CALL(*this, stats()).WillByDefault(::testing::ReturnRef(stats_));
  }

  JwtAuthnFilterStats generateMockStats(Stats::Scope& scope) {
    return {ALL_JWT_AUTHN_FILTER_STATS(POOL_COUNTER_PREFIX(scope, ""))};
  }

  MOCK_METHOD(JwksData*, findByIssuer, (const std::string&), ());
  MOCK_METHOD(JwksData*, findByProvider, (const std::string&), ());
  MOCK_METHOD(JwtAuthnFilterStats&, stats, ());

  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  JwtAuthnFilterStats stats_;
  ::testing::NiceMock<MockJwksData> jwks_data_;
};

// A mock HTTP upstream with response body.
class MockUpstream {
public:
  MockUpstream(Upstream::MockClusterManager& mock_cm, const std::string& response_body)
      : request_(&mock_cm.thread_local_cluster_.async_client_), response_body_(response_body) {
    ON_CALL(mock_cm.thread_local_cluster_.async_client_, send_(_, _, _))
        .WillByDefault(
            Invoke([this](Http::RequestMessagePtr&, Http::AsyncClient::Callbacks& cb,
                          const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
              Http::ResponseMessagePtr response_message(
                  new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
                      new Http::TestResponseHeaderMapImpl{{":status", "200"}}}));
              response_message->body().add(response_body_);
              cb.onSuccess(request_, std::move(response_message));
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
