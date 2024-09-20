#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/message_impl.h"
#include "source/extensions/common/aws/sigv4_signer_impl.h"
#include "source/extensions/common/aws/utility.h"

#include "test/extensions/common/aws/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"
#include "test/test_common/environment.h"
#include <filesystem>
#include <fstream>

// using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {
namespace {


std::vector<std::string> directoryListing()
{
  std::vector<std::string> a;
  for (auto const &entry : std::filesystem::directory_iterator(TestEnvironment::runfilesDirectory()+"/test/extensions/common/aws/signer_corpuses/v4"))
  {
    a.push_back(entry.path().string());
  }
  return a;
}

class SigV4SignerCorpusTest : public ::testing::TestWithParam<std::string> {
  public:
  SigV4SignerCorpusTest()
        : credentials_provider_(new NiceMock<MockCredentialsProvider>()),
        message_(new Http::RequestMessageImpl()) {}

  void addMethod(const std::string& method) { message_->headers().setMethod(method); }

  void addPath(const std::string& path) { message_->headers().setPath(path); }

  void addHeader(const std::string& key, const std::string& value) {
    message_->headers().addCopy(Http::LowerCaseString(key), value);
  }

  void setupCredentials(const std::string& akid, const std::string& skid, const std::string token)
  {
      credentials_ = new Credentials(akid, skid, token);
      EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(*credentials_));
  }

  NiceMock<MockCredentialsProvider>* credentials_provider_;
  Http::RequestMessagePtr message_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Credentials *credentials_;
};


// {
//     "credentials": {
//         "access_key_id": "AKIDEXAMPLE",
//         "secret_access_key": "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY"
//     },
//     "expiration_in_seconds": 3600,
//     "normalize": true,
//     "region": "us-east-1",
//     "service": "service",
//     "sign_body": false,
//     "timestamp": "2015-08-30T12:36:00Z"
// }

// GET / HTTP/1.1

TEST_P(SigV4SignerCorpusTest, SigV4SignerCorpus) {

  Envoy::Logger::Registry::setLogLevel(spdlog::level::debug);

  const std::string rundir = GetParam();
  auto contextfile = rundir + "/context.json";

  Json::ObjectSharedPtr json_context;

  std::ifstream context(contextfile);
  std::stringstream buffer;
  buffer << context.rdbuf();
  json_context = Json::Factory::loadFromString(buffer.str());

  auto requestfile = rundir +  "/request.txt";
  std::ifstream request(requestfile);
  std::stringstream buffer2;
  buffer2 << request.rdbuf();
  std::vector<std::string> payload = absl::StrSplit(buffer2.str(),'\n');
  std::vector<std::string> req = absl::StrSplit(payload[0],' ');
  auto method = req[0];
  addMethod(method);
  auto path = req[1];
  addPath(path);
  
  EXPECT_EQ(static_cast<std::string>(req[2]), "HTTP/1.1");
  std::vector<std::string> hostHeader = absl::StrSplit(payload[1],':');
  EXPECT_EQ(static_cast<std::string>(hostHeader[0]), "Host");

  addHeader("host", hostHeader[1]);

  auto expiration = json_context->getInteger("expiration_in_seconds");
  auto region = json_context->getString("region");
  auto service = json_context->getString("service");
  auto timestamp = json_context->getString("timestamp");
  auto akid = json_context->getObject("credentials")->getString("access_key_id");
  auto skid = json_context->getObject("credentials")->getString("secret_access_key");
  std::string token;
  try 
  {
    token = json_context->getObject("credentials")->getString("token");
  }
  catch(EnvoyException& e)
  {
    token = "";
  }
  auto headersignaturfilee = rundir +  "/header-signature.txt";
  auto querysignaturefile = rundir +  "/query-signature.txt";
  std::ifstream headersignaturef(headersignaturfilee);
  std::stringstream headersignature;
  headersignature << headersignaturef.rdbuf();
  auto canonicalrequestfile = rundir +  "/header-canonical-request.txt";
  std::ifstream canonicalrequestfilef(canonicalrequestfile);
  std::stringstream canonicalrequests;
  canonicalrequests << canonicalrequestfilef.rdbuf();

  // setupCredentials(akid, skid, token);

  // SigV4SignerImpl querysigner_(service, region,
  //                          CredentialsProviderSharedPtr{credentials_provider_}, context_,
  //                          Extensions::Common::Aws::AwsSigningHeaderExclusionVector{}, true, expiration);
  // auto status = querysigner_.sign(*message_, false);
  credentials_ = new Credentials(akid, skid, token);
  EXPECT_CALL(*credentials_provider_, getCredentials()).WillOnce(Return(*credentials_));

  auto past_time = TestUtility::parseTime(timestamp, "%E4Y-%m-%dT%H:%M:%S%z");
  Event::SimulatedTimeSystem time_system_;
  time_system_.setSystemTime(absl::ToChronoTime(past_time));
  ON_CALL(context_, timeSystem()).WillByDefault(ReturnRef(time_system_));
  std::vector<Matchers::StringMatcherPtr> exclusion_list = {};

  auto  long_date_formatter_ = DateFormatter(std::string(SignatureConstants::LongDateFormat));
  auto  short_date_formatter_ = DateFormatter(std::string(SignatureConstants::ShortDateFormat));

  const auto long_date = long_date_formatter_.now(time_system_);
  if (token.empty()) {
    message_->headers().setCopy(SignatureHeaders::get().SecurityToken, token);
  }

  message_->headers().setCopy(SignatureHeaders::get().Date, long_date);

  const auto canonical_headers = Utility::canonicalizeHeaders(message_->headers(), exclusion_list);
  const auto calculated_canonical =
      Utility::createCanonicalRequest(service, method, path, canonical_headers, SignatureConstants::HashedEmptyString);
  EXPECT_EQ(canonicalrequests.str(), calculated_canonical);

  SigV4SignerImpl headersigner_(service, region,
                           CredentialsProviderSharedPtr{credentials_provider_}, context_,
                           Extensions::Common::Aws::AwsSigningHeaderExclusionVector{}, false, expiration);

  auto status = headersigner_.sign(*message_, false);
  auto authheader = message_->headers().get(Envoy::Http::LowerCaseString("Authorization"))[0]->value().getStringView();
  std::vector<std::string> authsplit = absl::StrSplit(authheader,',');
  std::vector<std::string> sigsplit = absl::StrSplit(authsplit[2],'=');
  EXPECT_EQ(headersignature.str(), sigsplit[1]);
  EXPECT_EQ(expiration, 3600);
}
  
INSTANTIATE_TEST_SUITE_P(
    SigV4SignerCorpusTestSuite, SigV4SignerCorpusTest,
    ::testing::ValuesIn(directoryListing()), 
    [](const testing::TestParamInfo<SigV4SignerCorpusTest::ParamType>& info) {
      std::string a = std::filesystem::path(info.param).filename();
      a.erase(std::remove(a.begin(), a.end(), '-'), a.end());
      return a;
    }
    );

} // namespace
} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
