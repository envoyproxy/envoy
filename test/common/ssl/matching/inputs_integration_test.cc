#include "source/common/ssl/matching/inputs.h"

#include "test/common/matcher/test_utility.h"
#include "test/common/ssl/matching/test_data.h"
#include "test/mocks/matcher/mocks.h"
#include "test/mocks/server/factory_context.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Ssl {
namespace Matching {

constexpr absl::string_view yaml = R"EOF(
matcher_tree:
  input:
    name: input
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.ssl.v3.{}
  exact_match_map:
    map:
      "{}":
        action:
          name: test_action
          typed_config:
            "@type": type.googleapis.com/google.protobuf.StringValue
            value: foo
)EOF";

class InputsIntegrationTest : public ::testing::Test {
public:
  explicit InputsIntegrationTest()
      : inject_action_(action_factory_), context_(""),
        matcher_factory_(context_, factory_context_, validation_visitor_) {
    EXPECT_CALL(validation_visitor_, performDataInputValidation(_, _)).Times(testing::AnyNumber());
  }

  void initialize(const std::string& input, const std::string& value) {
    xds::type::matcher::v3::Matcher matcher;
    MessageUtil::loadFromYaml(fmt::format(std::string(yaml), input, value), matcher,
                              ProtobufMessage::getStrictValidationVisitor());

    match_tree_ = matcher_factory_.create(matcher);
  }

protected:
  Matcher::StringActionFactory action_factory_;
  Registry::InjectFactory<Matcher::ActionFactory<absl::string_view>> inject_action_;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context_;
  Matcher::MockMatchTreeValidationVisitor<TestMatchingData> validation_visitor_;
  absl::string_view context_;
  Matcher::MatchTreeFactory<TestMatchingData, absl::string_view> matcher_factory_;
  Matcher::MatchTreeFactoryCb<TestMatchingData> match_tree_;
};

TEST_F(InputsIntegrationTest, UriSanInput) {
  UriSanInputBaseFactory<TestMatchingData> factory;
  const auto host = "example.com";
  Registry::InjectFactory<Matcher::DataInputFactory<TestMatchingData>> registration(factory);

  initialize("UriSanInput", host);

  TestMatchingData data;
  std::vector<std::string> uri_sans{host};
  EXPECT_CALL(*data.ssl_, uriSanPeerCertificate()).WillOnce(Return(uri_sans));

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(InputsIntegrationTest, DnsSanInput) {
  DnsSanInputBaseFactory<TestMatchingData> factory;
  const auto host = "example.com";
  Registry::InjectFactory<Matcher::DataInputFactory<TestMatchingData>> registration(factory);

  initialize("DnsSanInput", host);

  TestMatchingData data;
  std::vector<std::string> dns_sans{host};
  EXPECT_CALL(*data.ssl_, dnsSansPeerCertificate()).WillOnce(Return(dns_sans));

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

TEST_F(InputsIntegrationTest, SubjectInput) {
  SubjectInputBaseFactory<TestMatchingData> factory;
  const std::string host = "example.com";
  Registry::InjectFactory<Matcher::DataInputFactory<TestMatchingData>> registration(factory);

  initialize("SubjectInput", host);

  TestMatchingData data;
  EXPECT_CALL(*data.ssl_, subjectPeerCertificate()).WillOnce(testing::ReturnRef(host));

  const auto result = match_tree_()->match(data);
  EXPECT_EQ(result.match_state_, Matcher::MatchState::MatchComplete);
  EXPECT_TRUE(result.on_match_.has_value());
}

} // namespace Matching
} // namespace Ssl
} // namespace Envoy
