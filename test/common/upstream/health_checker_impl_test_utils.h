#include <vector>

#include "common/upstream/health_checker_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/mocks/upstream/health_check_event_logger.h"

namespace Envoy {
namespace Upstream {

class HealthCheckerTestBase {
public:
  std::shared_ptr<MockClusterMockPrioritySet> cluster_{
      std::make_shared<NiceMock<MockClusterMockPrioritySet>>()};
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::unique_ptr<MockHealthCheckEventLogger> event_logger_storage_{
      std::make_unique<MockHealthCheckEventLogger>()};
  MockHealthCheckEventLogger& event_logger_{*event_logger_storage_};
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<Runtime::MockLoader> runtime_;
};

class TestHttpHealthCheckerImpl : public HttpHealthCheckerImpl {
public:
  using HttpHealthCheckerImpl::HttpHealthCheckerImpl;

  Http::CodecClient* createCodecClient(Upstream::Host::CreateConnectionData& conn_data) override {
    return createCodecClient_(conn_data);
  };

  // HttpHealthCheckerImpl
  MOCK_METHOD(Http::CodecClient*, createCodecClient_, (Upstream::Host::CreateConnectionData&));

  Http::CodecClient::Type codecClientType() { return codec_client_type_; }
};

class HttpHealthCheckerImplTestBase : public HealthCheckerTestBase {
public:
  struct TestSession {
    Event::MockTimer* interval_timer_{};
    Event::MockTimer* timeout_timer_{};
    Http::MockClientConnection* codec_{};
    Stats::IsolatedStoreImpl stats_store_;
    Network::MockClientConnection* client_connection_{};
    NiceMock<Http::MockRequestEncoder> request_encoder_;
    Http::ResponseDecoder* stream_response_callbacks_{};
  };

  using TestSessionPtr = std::unique_ptr<TestSession>;
  using HostWithHealthCheckMap =
      absl::node_hash_map<std::string,
                          const envoy::config::endpoint::v3::Endpoint::HealthCheckConfig>;

  void expectSessionCreate(const HostWithHealthCheckMap& health_check_map);

  void expectClientCreate(size_t index, const HostWithHealthCheckMap& health_check_map);

  void expectStreamCreate(size_t index);

  void expectSessionCreate();
  void expectClientCreate(size_t index);

  std::vector<TestSessionPtr> test_sessions_;
  std::shared_ptr<TestHttpHealthCheckerImpl> health_checker_;
  std::list<uint32_t> connection_index_{};
  std::list<uint32_t> codec_index_{};
  const HostWithHealthCheckMap health_checker_map_{};
};

} // namespace Upstream
} // namespace Envoy
