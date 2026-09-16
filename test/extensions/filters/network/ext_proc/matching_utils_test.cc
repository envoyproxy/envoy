#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/expr/evaluator.h"
#include "source/extensions/filters/network/ext_proc/matching_utils.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/struct_matchers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::UnorderedElementsAre;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtProc {
namespace {

#ifdef USE_CEL_PARSER

class ExpressionManagerTest : public testing::Test {
protected:
  ExpressionManagerTest() {
    auto builder = Filters::Common::Expr::getBuilder(context_);
    Protobuf::RepeatedPtrField<std::string> connection_matchers;
    absl::Status creation_status = absl::OkStatus();
    expression_manager_ = std::make_unique<ExpressionManager>(builder, &context_.local_info_,
                                                              connection_matchers, creation_status);
    EXPECT_TRUE(creation_status.ok());
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  std::unique_ptr<ExpressionManager> expression_manager_;
};

TEST_F(ExpressionManagerTest, SimpleExpression) {
  EXPECT_FALSE(expression_manager_->hasConnectionExpr());
}

TEST_F(ExpressionManagerTest, InvalidExpression) {
  Protobuf::RepeatedPtrField<std::string> connection_matchers;
  connection_matchers.Add("undefined_func()");
  auto builder = Filters::Common::Expr::getBuilder(context_);
  absl::Status creation_status = absl::OkStatus();
  ExpressionManager test_manager(builder, &context_.local_info_, connection_matchers,
                                 creation_status);
  EXPECT_FALSE(creation_status.ok());
}

TEST_F(ExpressionManagerTest, RepeatedMatchers) {
  Protobuf::RepeatedPtrField<std::string> connection_matchers;
  connection_matchers.Add("true");
  connection_matchers.Add("true");
  auto builder = Filters::Common::Expr::getBuilder(context_);
  absl::Status creation_status = absl::OkStatus();
  ExpressionManager test_manager(builder, &context_.local_info_, connection_matchers,
                                 creation_status);
  ASSERT_TRUE(creation_status.ok());
  EXPECT_TRUE(test_manager.hasConnectionExpr());
}

TEST_F(ExpressionManagerTest, EvaluateAttributesEmpty) {
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  auto activation = Filters::Common::Expr::createActivation(&context_.local_info_, stream_info,
                                                            nullptr, nullptr, nullptr);
  auto result = ExpressionManager::evaluateAttributes(*activation, {});
  EXPECT_TRUE(result.fields().empty());
}

TEST_F(ExpressionManagerTest, EvaluateAttributesValues) {
  Protobuf::RepeatedPtrField<std::string> connection_matchers;
  connection_matchers.Add("connection.mtls");
  connection_matchers.Add("connection.id");
  auto builder = Filters::Common::Expr::getBuilder(context_);
  absl::Status creation_status = absl::OkStatus();
  ExpressionManager test_manager(builder, &context_.local_info_, connection_matchers,
                                 creation_status);
  ASSERT_TRUE(creation_status.ok());
  EXPECT_TRUE(test_manager.hasConnectionExpr());

  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  stream_info.downstream_connection_info_provider_->setConnectionID(12345);
  auto activation = Filters::Common::Expr::createActivation(&context_.local_info_, stream_info,
                                                            nullptr, nullptr, nullptr);
  auto result = test_manager.evaluateConnectionAttributes(*activation);
  EXPECT_THAT(result.fields(), UnorderedElementsAre(IsStructBool("connection.mtls", false),
                                                    IsStructNumber("connection.id", 12345)));
}

#else

TEST(ExpressionManagerTest, CelUnavailableTest) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  auto builder = Filters::Common::Expr::getBuilder(context);
  Protobuf::RepeatedPtrField<std::string> connection_matchers;
  connection_matchers.Add("true");

  // When CEL is not available, this should log a warning but not throw
  absl::Status creation_status = absl::OkStatus();
  ExpressionManager manager(builder, &context.local_info_, connection_matchers, creation_status);
  EXPECT_FALSE(manager.hasConnectionExpr());
}

#endif // USE_CEL_PARSER

} // namespace
} // namespace ExtProc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
