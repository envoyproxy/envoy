#pragma once

#include <vector>

#include "source/common/upstream/health_checker_impl.h"
#include "source/extensions/health_checkers/grpc/health_checker_impl.h"
#include "source/extensions/health_checkers/http/health_checker_impl.h"
#include "source/extensions/health_checkers/tcp/health_checker_impl.h"

#include "test/common/http/common.h"
#include "test/mocks/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/health_checker_factory_context.h"
#include "test/mocks/upstream/cluster_priority_set.h"
#include "test/mocks/upstream/health_check_event_logger.h"

namespace Envoy {
namespace Upstream {

class HealthCheckerTestBase {
public:
  std::shared_ptr<MockClusterMockPrioritySet> cluster_{
      std::make_shared<NiceMock<MockClusterMockPrioritySet>>()};
  std::unique_ptr<NiceMock<MockHealthCheckEventLogger>> event_logger_storage_{
      std::make_unique<NiceMock<MockHealthCheckEventLogger>>()};
  NiceMock<MockHealthCheckEventLogger>& event_logger_{*event_logger_storage_};
  NiceMock<Server::Configuration::MockHealthCheckerFactoryContext> context_;
};

class TestHttpHealthCheckerImpl : public HttpHealthCheckerImpl {
public:
  using HttpHealthCheckerImpl::HttpHealthCheckerImpl;

  Http::CodecClient* createCodecClient(Upstream::Host::CreateConnectionData& conn_data) override {
    return createCodecClient_(conn_data);
  };

  // HttpHealthCheckerImpl
  MOCK_METHOD(Http::CodecClient*, createCodecClient_, (Upstream::Host::CreateConnectionData&));

  Http::CodecType codecClientType() { return codec_client_type_; }
};

class HttpHealthCheckerImplTestBase : public HealthCheckerTestBase {
public:
  struct TestSession {
    NiceMock<Event::MockTimer>* interval_timer_{};
    NiceMock<Event::MockTimer>* timeout_timer_{};
    Http::MockClientConnection* codec_{};
    Stats::IsolatedStoreImpl stats_store_;
    Network::MockClientConnection* client_connection_{};
    NiceMock<Http::MockRequestEncoder> request_encoder_;
    Http::ResponseDecoder* stream_response_callbacks_{};
    CodecClientForTest* codec_client_{};
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

// TODO(zasweq): This class here isn't currently being used in the unit test class.
// The class here expects the creates the timeout first, then the interval. This is due
// to the normal expectation call to be opposite, or LIFO (Last in, First Out). The InSequence
// object makes the tcp health checker unit tests FIFO (First in, First out). We should standardize
// this amongst the three unit test classes.
class TcpHealthCheckerImplTestBase : public HealthCheckerTestBase {
public:
  void expectSessionCreate();
  void expectClientCreate();

  std::shared_ptr<TcpHealthCheckerImpl> health_checker_;
  Network::MockClientConnection* connection_{};
  NiceMock<Event::MockTimer>* timeout_timer_{};
  NiceMock<Event::MockTimer>* interval_timer_{};
  Network::ReadFilterSharedPtr read_filter_;
};

class TestGrpcHealthCheckerImpl : public GrpcHealthCheckerImpl {
public:
  using GrpcHealthCheckerImpl::GrpcHealthCheckerImpl;

  Http::CodecClientPtr createCodecClient(Upstream::Host::CreateConnectionData& conn_data) override {
    auto codec_client = createCodecClient_(conn_data);
    return Http::CodecClientPtr(codec_client);
  };

  // GrpcHealthCheckerImpl
  MOCK_METHOD(Http::CodecClient*, createCodecClient_, (Upstream::Host::CreateConnectionData&));
};

class GrpcHealthCheckerImplTestBaseUtils : public HealthCheckerTestBase {
public:
  struct TestSession {
    TestSession() = default;

    Event::MockTimer* interval_timer_{};
    Event::MockTimer* timeout_timer_{};
    Http::MockClientConnection* codec_{};
    Stats::IsolatedStoreImpl stats_store_;
    Network::MockClientConnection* client_connection_{};
    NiceMock<Http::MockRequestEncoder> request_encoder_;
    Http::ResponseDecoder* stream_response_callbacks_{};
    CodecClientForTest* codec_client_{};
  };

  using TestSessionPtr = std::unique_ptr<TestSession>;

  GrpcHealthCheckerImplTestBaseUtils();

  void expectSessionCreate();
  void expectClientCreate(size_t index);
  void expectStreamCreate(size_t index);

  std::vector<TestSessionPtr> test_sessions_;
  std::shared_ptr<TestGrpcHealthCheckerImpl> health_checker_;
  std::list<uint32_t> connection_index_{};
  std::list<uint32_t> codec_index_{};
};

} // namespace Upstream
} // namespace Envoy
