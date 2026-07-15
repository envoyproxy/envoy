#pragma once

#include <cstdint>
#include <list>
#include <string>

#include "source/extensions/filters/network/common/redis/client_impl.h"
#include "source/extensions/filters/network/common/redis/codec_impl.h"

#include "test/test_common/printers.h"

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

/**
 * Pretty print const RespValue& value
 */

void PrintTo(const RespValue& value, std::ostream* os);
void PrintTo(const RespValuePtr& value, std::ostream* os);

class MockEncoder : public Common::Redis::Encoder {
public:
  MockEncoder();
  ~MockEncoder() override;

  MOCK_METHOD(void, encode, (const Common::Redis::RespValue& value, Buffer::Instance& out));
  MOCK_METHOD(void, setProtocolVersion, (Common::Redis::RespProtocolVersion version));

private:
  Common::Redis::EncoderImpl real_encoder_;
};

class MockDecoder : public Common::Redis::Decoder {
public:
  MockDecoder();
  ~MockDecoder() override;

  MOCK_METHOD(void, decode, (Buffer::Instance & data));
};

namespace Client {

class MockPoolRequest : public PoolRequest {
public:
  MockPoolRequest();
  ~MockPoolRequest() override;

  MOCK_METHOD(void, cancel, ());
};

class MockClient : public Client {
public:
  MockClient();
  ~MockClient() override;

  void raiseEvent(Network::ConnectionEvent event) {
    for (Network::ConnectionCallbacks* callbacks : callbacks_) {
      callbacks->onEvent(event);
    }
  }

  void runHighWatermarkCallbacks() {
    for (auto* callback : callbacks_) {
      callback->onAboveWriteBufferHighWatermark();
    }
  }

  void runLowWatermarkCallbacks() {
    for (auto* callback : callbacks_) {
      callback->onBelowWriteBufferLowWatermark();
    }
  }

  PoolRequest* makeRequest(const Common::Redis::RespValue& request,
                           ClientCallbacks& callbacks) override {
    client_callbacks_.push_back(&callbacks);
    return makeRequest_(request, callbacks);
  }

  MOCK_METHOD(void, addConnectionCallbacks, (Network::ConnectionCallbacks & callbacks));
  MOCK_METHOD(bool, active, ());
  MOCK_METHOD(void, close, ());
  MOCK_METHOD(PoolRequest*, makeRequest_,
              (const Common::Redis::RespValue& request, ClientCallbacks& callbacks));
  MOCK_METHOD(void, initialize, (const std::string& username, const std::string& password));
  MOCK_METHOD(void, sendCommand, (const Common::Redis::RespValue& request));
  MOCK_METHOD(void, setPushCallbacks, (PushMessageCallbacks * callbacks));

  std::list<Network::ConnectionCallbacks*> callbacks_;
  std::list<ClientCallbacks*> client_callbacks_;
};

class MockClientCallbacks : public ClientCallbacks {
public:
  MockClientCallbacks();
  ~MockClientCallbacks() override;

  void onResponse(Common::Redis::RespValuePtr&& value) override { onResponse_(value); }
  void onRedirection(Common::Redis::RespValuePtr&& value, const std::string& host_address,
                     bool ask_redirection) override {
    onRedirection_(value, host_address, ask_redirection);
  }

  MOCK_METHOD(void, onResponse_, (Common::Redis::RespValuePtr & value));
  MOCK_METHOD(void, onFailure, ());
  MOCK_METHOD(void, onRedirection_,
              (Common::Redis::RespValuePtr & value, const std::string& host_address,
               bool ask_redirection));
};

// Shared double for the RESP3 Push callback sink (dedup: was hand-rolled as a mock in
// client_impl_test and as a separate no-op in conn_pool_impl_test). gmock cannot match the
// move-only RespValuePtr directly, so the virtual methods forward to by-ref MOCK_METHOD
// helpers; wrap in NiceMock for a no-op sink. onUpstreamControlError already defaults to a
// no-op on the interface but is mocked here so subscription tests can assert on it.
class MockPushMessageCallbacks : public PushMessageCallbacks {
public:
  MOCK_METHOD(void, onPushMessage_, (Common::Redis::RespValue & value));
  void onPushMessage(Common::Redis::RespValuePtr&& value,
                     const Upstream::HostConstSharedPtr&) override {
    onPushMessage_(*value);
  }
  MOCK_METHOD(void, onUpstreamControlError_,
              (Common::Redis::RespValue & value, const Upstream::HostConstSharedPtr& host));
  void onUpstreamControlError(Common::Redis::RespValuePtr&& value,
                              const Upstream::HostConstSharedPtr& host) override {
    onUpstreamControlError_(*value, host);
  }
};

} // namespace Client

namespace AwsIamAuthenticator {
class MockAwsIamAuthenticator : public Envoy::Extensions::NetworkFilters::Common::Redis::
                                    AwsIamAuthenticator::AwsIamAuthenticatorImpl {
public:
  MockAwsIamAuthenticator(Envoy::Extensions::Common::Aws::SignerPtr signer)
      : AwsIamAuthenticatorImpl(std::move(signer)) {}
  ~MockAwsIamAuthenticator() override = default;
  MOCK_METHOD(std::string, getAuthToken,
              (absl::string_view auth_user,
               const envoy::extensions::filters::network::redis_proxy::v3::AwsIam& aws_iam_config));
  MOCK_METHOD(bool, addCallbackIfCredentialsPending,
              (Extensions::Common::Aws::CredentialsPendingCallback && cb));
};

} // namespace AwsIamAuthenticator
} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
