#include <algorithm>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/http/filter.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"

#include "source/common/http/conn_manager_impl.h"
#include "source/common/http/context_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/filters/http/ext_proc/ext_proc.h"

#include "test/common/http/common.h"
#include "test/common/http/conn_manager_impl_test_base.h"
#include "test/extensions/filters/http/ext_proc/mock_server.h"
#include "test/extensions/filters/http/ext_proc/utils.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/http/stream_encoder.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/printers.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {
namespace {

using ::Envoy::Http::TestRequestHeaderMapImpl;
using ::Envoy::Http::TestRequestTrailerMapImpl;
using ::Envoy::Http::TestResponseHeaderMapImpl;
using ::Envoy::Http::TestResponseTrailerMapImpl;

class ExpressionManagerTest : public testing::Test {
  void initialize() {
    builder_ = std::make_shared<Filters::Common::Expr::BuilderInstance>(
                  Envoy::Extensions::Filters::Common::Expr::createBuilder(nullptr));
  }
public:
  std::shared_ptr<Filters::Common::Expr::BuilderInstance> builder_;
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  testing::NiceMock<LocalInfo::MockLocalInfo> local_info_;
  TestRequestHeaderMapImpl request_headers_;
  TestResponseHeaderMapImpl response_headers_;
  TestRequestTrailerMapImpl request_trailers_;
  TestResponseTrailerMapImpl response_trailers_;
  Protobuf::RepeatedPtrField<std::string> req_matchers_;
  Protobuf::RepeatedPtrField<std::string> resp_matchers_;
};

#if defined(USE_CEL_PARSER)
TEST_F(ExpressionManagerTest, DuplicateAttributesIgnored) {
  req_matchers_.Add("request.path");
  req_matchers_.Add("request.method");
  req_matchers_.Add("request.method");
  req_matchers_.Add("request.path");
  const auto expr_mgr = ExpressionManager(builder_, local_info_, req_matchers_, resp_matchers_);

  request_headers_.setMethod("GET");
  request_headers_.setPath("/foo");
  const auto activation_ptr = Filters::Common::Expr::createActivation(
      &expr_mgr.localInfo(), stream_info_,
      &request_headers_, &response_headers_,
      &response_trailers_);

  auto result = expr_mgr.evaluateRequestAttributes(*activation_ptr);
  EXPECT_EQ(2, result.fields_size());
  EXPECT_NE(result.fields().end(), result.fields().find("request.path"));
  EXPECT_NE(result.fields().end(), result.fields().find("request.method"));
}

TEST_F(ExpressionManagerTest, UnparsableExpressionThrowsException) {
  req_matchers_.Add("foobar");
  const auto expr_mgr = ExpressionManager(builder_, local_info_, req_matchers_, resp_matchers_);

  request_headers_.setMethod("GET");
  request_headers_.setPath("/foo");
  const auto activation_ptr = Filters::Common::Expr::createActivation(
      &expr_mgr.localInfo(), stream_info_,
      &request_headers_, &response_headers_,
      &response_trailers_);

  EXPECT_THROW_WITH_REGEX(expr_mgr.evaluateRequestAttributes(*activation_ptr), EnvoyException, "Unable to parse descriptor expression.*")
}

TEST_F(ExpressionManagerTest, EmptyExpressionReturnsEmptyStruct) {
  const auto expr_mgr = ExpressionManager(builder_, local_info_, req_matchers_, resp_matchers_);

  request_headers_ = TestRequestHeaderMapImpl();
  request_headers_.setMethod("GET");
  request_headers_.setPath("/foo");
  const auto activation_ptr = Filters::Common::Expr::createActivation(
      &expr_mgr.localInfo(), stream_info_,
      &request_headers_, &response_headers_,
      &response_trailers_);

  EXPECT_EQ(0, expr_mgr.evaluateRequestAttributes(*activation_ptr).fields_size());
}
#endif

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
