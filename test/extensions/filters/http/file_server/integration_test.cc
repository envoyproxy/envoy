#include <cstdlib>
#include <iostream>

#include "test/integration/http_protocol_integration.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FileServer {

using ::testing::AllOf;

class FileServerIntegrationTest : public HttpProtocolIntegrationTest {
public:
  static constexpr absl::string_view index_txt_contents_ = "12345678";
  static constexpr absl::string_view banana_html_contents_ = "abcdefgh";
  static constexpr absl::string_view readme_md_contents_ = "README CONTENT";
  static constexpr absl::string_view index_html_contents_ = "87654321";
  static absl::string_view testTmpDir() {
    auto env_tmpdir = std::getenv("TEST_TMPDIR");
    if (env_tmpdir) {
      return env_tmpdir;
    }
    env_tmpdir = std::getenv("TMPDIR");
    return env_tmpdir ? env_tmpdir : "/tmp";
  }

  static void prepareTmpFiles() {
    std::cerr << "Writing test filesystem in tmpdir: " << testTmpDir() << std::endl;
    TestEnvironment::createPath(absl::StrCat(testTmpDir(), "/fs1"));
    TestEnvironment::createPath(absl::StrCat(testTmpDir(), "/fs2"));
    TestEnvironment::writeStringToFileForTest(absl::StrCat(testTmpDir(), "/fs1/banana.html"),
                                              std::string{banana_html_contents_}, true);
    TestEnvironment::writeStringToFileForTest(absl::StrCat(testTmpDir(), "/fs1/index.txt"),
                                              std::string{index_txt_contents_}, true);
    TestEnvironment::writeStringToFileForTest(absl::StrCat(testTmpDir(), "/fs1/README.md"),
                                              std::string{readme_md_contents_}, true);
    TestEnvironment::writeStringToFileForTest(absl::StrCat(testTmpDir(), "/fs2/index.html"),
                                              std::string{index_html_contents_}, true);
  }

  static void SetUpTestSuite() { prepareTmpFiles(); }

  std::string testConfig() {
    return absl::StrCat(R"(
name: "envoy.filters.http.file_server"
typed_config:
  "@type": "type.googleapis.com/envoy.extensions.filters.http.file_server.v3.FileServerConfig"
  manager_config:
    thread_pool:
      thread_count: 1
  path_mappings:
    - request_path_prefix: /path1
      file_path_prefix: )",
                        testTmpDir(), R"(/fs1
    - request_path_prefix: /path1/path2
      file_path_prefix: )",
                        testTmpDir(), R"(/fs2
  content_types:
    "txt": "text/plain"
    "html": "text/html"
  default_content_type: "application/octet-stream"
  directory_behaviors:
    - default_file: "index.html"
    - default_file: "index.txt"
    - list: {}
)");
  }

  void initializeFilter(const std::string& config) {
    config_helper_.prependFilter(config);
    initialize();
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  }

  IntegrationStreamDecoderPtr
  sendHeaderOnlyRequestAwaitResponse(const Http::TestRequestHeaderMapImpl& headers) {
    IntegrationStreamDecoderPtr response_decoder = codec_client_->makeHeaderOnlyRequest(headers);
    // Wait for the response to be read by the codec client.
    EXPECT_TRUE(response_decoder->waitForEndStream());
    return response_decoder;
  }

  Http::TestRequestHeaderMapImpl requestPath(std::string path) {
    return Http::TestRequestHeaderMapImpl{
        {":method", "GET"},
        {":path", path},
        {":authority", "some_authority"},
        {":scheme", "http"},
    };
  }
};

// Nothing about this filter interacts with the http protocols in any way, so there's no need
// to run combinatorial iterations of each test, we can just run one.
INSTANTIATE_TEST_SUITE_P(
    Protocols, FileServerIntegrationTest,
    testing::ValuesIn({HttpProtocolIntegrationTest::getProtocolTestParams()[0]}),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(FileServerIntegrationTest, ReadsConfiguredIndexFileOnRequestForDirectory) {
  initializeFilter(testConfig());
  Http::TestRequestHeaderMapImpl request_headers = requestPath("/path1");
  auto response = sendHeaderOnlyRequestAwaitResponse(request_headers);
  EXPECT_THAT(response->headers(),
              AllOf(ContainsHeader(":status", "200"), ContainsHeader("content-type", "text/plain"),
                    ContainsHeader("content-length", absl::StrCat(index_txt_contents_.length()))));
  EXPECT_THAT(response->body(), index_txt_contents_);
}

TEST_P(FileServerIntegrationTest, ReadsSpecifiedFile) {
  initializeFilter(testConfig());
  Http::TestRequestHeaderMapImpl request_headers = requestPath("/path1/banana.html");
  auto response = sendHeaderOnlyRequestAwaitResponse(request_headers);
  EXPECT_THAT(
      response->headers(),
      AllOf(ContainsHeader(":status", "200"), ContainsHeader("content-type", "text/html"),
            ContainsHeader("content-length", absl::StrCat(banana_html_contents_.length()))));
  EXPECT_THAT(response->body(), banana_html_contents_);
}

TEST_P(FileServerIntegrationTest, IgnoresInvalidlyFormattedRangeHeader) {
  initializeFilter(testConfig());
  Http::TestRequestHeaderMapImpl request_headers = requestPath("/path1/banana.html");
  request_headers.addCopy("range", "megatrons=3-5");
  auto response = sendHeaderOnlyRequestAwaitResponse(request_headers);
  EXPECT_THAT(
      response->headers(),
      AllOf(ContainsHeader(":status", "200"), ContainsHeader("content-type", "text/html"),
            ContainsHeader("content-length", absl::StrCat(banana_html_contents_.length()))));
  EXPECT_THAT(response->body(), banana_html_contents_);
}

TEST_P(FileServerIntegrationTest, IgnoresMultipleRangeHeader) {
  initializeFilter(testConfig());
  Http::TestRequestHeaderMapImpl request_headers = requestPath("/path1/banana.html");
  request_headers.addCopy("range", "bytes=3-5,6-9");
  auto response = sendHeaderOnlyRequestAwaitResponse(request_headers);
  EXPECT_THAT(
      response->headers(),
      AllOf(ContainsHeader(":status", "200"), ContainsHeader("content-type", "text/html"),
            ContainsHeader("content-length", absl::StrCat(banana_html_contents_.length()))));
  EXPECT_THAT(response->body(), banana_html_contents_);
}

TEST_P(FileServerIntegrationTest, IgnoresSuffixRangeHeader) {
  initializeFilter(testConfig());
  Http::TestRequestHeaderMapImpl request_headers = requestPath("/path1/banana.html");
  request_headers.addCopy("range", "bytes=-6");
  auto response = sendHeaderOnlyRequestAwaitResponse(request_headers);
  EXPECT_THAT(
      response->headers(),
      AllOf(ContainsHeader(":status", "200"), ContainsHeader("content-type", "text/html"),
            ContainsHeader("content-length", absl::StrCat(banana_html_contents_.length()))));
  EXPECT_THAT(response->body(), banana_html_contents_);
}

TEST_P(FileServerIntegrationTest, IgnoresNonNumericRangeHeader) {
  initializeFilter(testConfig());
  Http::TestRequestHeaderMapImpl request_headers = requestPath("/path1/banana.html");
  request_headers.addCopy("range", "bytes=banana-");
  auto response = sendHeaderOnlyRequestAwaitResponse(request_headers);
  EXPECT_THAT(
      response->headers(),
      AllOf(ContainsHeader(":status", "200"), ContainsHeader("content-type", "text/html"),
            ContainsHeader("content-length", absl::StrCat(banana_html_contents_.length()))));
  EXPECT_THAT(response->body(), banana_html_contents_);
}

TEST_P(FileServerIntegrationTest, ReadsSpecifiedFileWithRange) {
  initializeFilter(testConfig());
  Http::TestRequestHeaderMapImpl request_headers = requestPath("/path1/banana.html");
  request_headers.addCopy("range", "bytes=3-5");
  auto response = sendHeaderOnlyRequestAwaitResponse(request_headers);
  EXPECT_THAT(response->headers(),
              AllOf(ContainsHeader(":status", "206"), ContainsHeader("content-type", "text/html"),
                    ContainsHeader("content-length", "3"),
                    ContainsHeader("content-range", "bytes 3-5/8")));
  EXPECT_THAT(response->body(), banana_html_contents_.substr(3, 3));
}

TEST_P(FileServerIntegrationTest, ReadsSpecifiedFileWithRangeToEnd) {
  initializeFilter(testConfig());
  Http::TestRequestHeaderMapImpl request_headers = requestPath("/path1/banana.html");
  request_headers.addCopy("range", "bytes=3-");
  auto response = sendHeaderOnlyRequestAwaitResponse(request_headers);
  EXPECT_THAT(response->headers(),
              AllOf(ContainsHeader(":status", "206"), ContainsHeader("content-type", "text/html"),
                    ContainsHeader("content-length", "5"),
                    ContainsHeader("content-range", "bytes 3-7/8")));
  EXPECT_THAT(response->body(), banana_html_contents_.substr(3));
}

TEST_P(FileServerIntegrationTest, RejectsRangeRequestLargerThanFile) {
  initializeFilter(testConfig());
  Http::TestRequestHeaderMapImpl request_headers = requestPath("/path1/banana.html");
  request_headers.addCopy("range", "bytes=3-20");
  auto response = sendHeaderOnlyRequestAwaitResponse(request_headers);
  EXPECT_THAT(response->headers(),
              ContainsHeader(":status", absl::StrCat(Http::Code::RangeNotSatisfiable)));
}

} // namespace FileServer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
