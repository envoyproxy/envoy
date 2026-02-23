#include "source/extensions/filters/http/file_server/config.h"
#include "source/extensions/filters/http/file_server/filter.h"

#include "test/extensions/common/async_files/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FileServer {

using Extensions::Common::AsyncFiles::MockAsyncFileContext;
using Extensions::Common::AsyncFiles::MockAsyncFileHandle;
using Extensions::Common::AsyncFiles::MockAsyncFileManager;
using ::testing::AnyNumber;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::ReturnRef;
using ::testing::StrictMock;

MATCHER_P(OptRefWith, m, "") {
  if (arg == absl::nullopt) {
    *result_listener << "is nullopt";
    return false;
  }
  return ExplainMatchResult(m, arg.ref(), result_listener);
};

class FileServerFilterTest : public testing::Test {
public:
  std::shared_ptr<const FileServerConfig> configFromYaml(absl::string_view yaml) {
    std::string s(yaml);
    ProtoFileServerConfig proto_config;
    TestUtility::loadFromYaml(s, proto_config);
    return std::make_shared<FileServerConfig>(proto_config, nullptr, mock_async_file_manager_);
  }
  void initFilter(FileServerFilter& filter) {
    filter.setDecoderFilterCallbacks(decoder_callbacks_);
    // It's a NiceMock but we do want to be notified of unexpected sendLocalReply.
    EXPECT_CALL(decoder_callbacks_, sendLocalReply).Times(0);
    EXPECT_CALL(decoder_callbacks_, dispatcher)
        .Times(AnyNumber())
        .WillRepeatedly(ReturnRef(*dispatcher_));
  }
  std::shared_ptr<FileServerFilter> testFilter() {
    auto filter = std::make_shared<FileServerFilter>(configFromYaml(R"(
path_mappings:
  - request_path_prefix: /path1
    file_path_prefix: fs1
content_types:
  "txt": "text/plain"
  "html": "text/html"
default_content_type: "application/octet-stream"
directory_behaviors:
  - default_file: "index.html"
  - default_file: "index.txt"
  - list: {}
)"));
    initFilter(*filter);
    return filter;
  }

  void pumpDispatcher() { dispatcher_->run(Event::Dispatcher::RunType::Block); }

  AsyncFileHandle makeMockFile() {
    mock_file_handle_ =
        std::make_shared<StrictMock<MockAsyncFileContext>>(mock_async_file_manager_);
    return mock_file_handle_;
  }

  std::string responseCodeDetails() {
    return decoder_callbacks_.stream_info_.response_code_details_.value_or("");
  }

  std::shared_ptr<MockAsyncFileManager> mock_async_file_manager_ =
      std::make_shared<NiceMock<MockAsyncFileManager>>();
  MockAsyncFileHandle mock_file_handle_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  Api::ApiPtr api_ = Api::createApiForTest();
  Event::DispatcherPtr dispatcher_ = api_->allocateDispatcher("test_thread");
};

TEST_F(FileServerFilterTest, PassThroughIfNoPath) {
  auto filter = testFilter();
  Http::TestRequestHeaderMapImpl request_headers{
      {":host", "test.host"},
      {":method", "GET"},
      {":scheme", "https"},
  };
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter->decodeHeaders(request_headers, true));
}

TEST_F(FileServerFilterTest, DestroyBeforeHeadersIsOkay) {
  auto filter = testFilter();
  filter->onDestroy();
  // Should not crash due to uninitialized abort functions or anything!
}

TEST_F(FileServerFilterTest, PassThroughIfNotMatchingPathMapping) {
  auto filter = testFilter();
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/"},
      {":host", "test.host"},
      {":method", "GET"},
      {":scheme", "https"},
  };
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter->decodeHeaders(request_headers, true));
}

TEST_F(FileServerFilterTest, BadRequestIfNonNormalizedPath) {
  auto filter = testFilter();
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/path1/../"},
      {":host", "test.host"},
      {":method", "GET"},
      {":scheme", "https"},
  };
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::BadRequest, _, _, _,
                                                 "file_server_rejected_non_normalized_path"));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter->decodeHeaders(request_headers, true));
}

TEST_F(FileServerFilterTest, BadRequestIfMissingMethod) {
  auto filter = testFilter();
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/path1/foo"},
      {":host", "test.host"},
      {":scheme", "https"},
  };
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::BadRequest, _, _, _,
                                                 "file_server_rejected_missing_method"));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter->decodeHeaders(request_headers, true));
}

TEST_F(FileServerFilterTest, StillMatchesPathIfPercentEncodingUsed) {
  auto filter = testFilter();
  Http::TestRequestHeaderMapImpl request_headers{
      // %31 is encoding of '1'
      {":path", "/path%31/foo"},
      {":host", "test.host"},
      {":scheme", "https"},
  };
  // Missing method is only checked if the path matched, so this is a quick test
  // for "it matched the path and then failed later".
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::BadRequest, _, _, _,
                                                 "file_server_rejected_missing_method"));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter->decodeHeaders(request_headers, true));
}

TEST_F(FileServerFilterTest, MethodNotAllowedIfMatchedPathAndUnsupportedMethod) {
  auto filter = testFilter();
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/path1/foo"},
      {":method", "POST"},
      {":host", "test.host"},
      {":scheme", "https"},
  };
  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::MethodNotAllowed, _, _, _, "file_server_rejected_method"));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter->decodeHeaders(request_headers, true));
}

TEST_F(FileServerFilterTest, BadRequestIfHeadersDoNotEndStream) {
  auto filter = testFilter();
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/path1/foo"},
      {":method", "GET"},
      {":host", "test.host"},
      {":scheme", "https"},
  };
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::BadRequest, _, _, _,
                                                 "file_server_rejected_not_end_stream"));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter->decodeHeaders(request_headers, false));
}

TEST_F(FileServerFilterTest, FileStatFailedNotFoundRespondsNotFound) {
  auto filter = testFilter();
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/path1/foo"},
      {":method", "GET"},
      {":host", "test.host"},
      {":scheme", "https"},
  };
  EXPECT_CALL(*mock_async_file_manager_, stat);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter->decodeHeaders(request_headers, true));
  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::NotFound, _, _, _, "file_server_stat_error"));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<struct stat>{absl::NotFoundError("mocked not found")});
  pumpDispatcher();
}

TEST_F(FileServerFilterTest, FilterOnDestroyWhileFileActionInFlightAbortsResponse) {
  auto filter = testFilter();
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/path1/foo"},
      {":method", "GET"},
      {":host", "test.host"},
      {":scheme", "https"},
  };
  EXPECT_CALL(*mock_async_file_manager_, stat);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter->decodeHeaders(request_headers, true));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<struct stat>{absl::NotFoundError("mocked not found")});
  filter->onDestroy();
  pumpDispatcher();
  // Should have been no call to sendLocalReply due to abort.
}

TEST_F(FileServerFilterTest, ErrorsOnDirectoryWithNoConfiguredBehavior) {
  auto filter = std::make_shared<FileServerFilter>(configFromYaml(R"(
path_mappings:
  - request_path_prefix: /path1
    file_path_prefix: fs1
)"));
  initFilter(*filter);
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/path1/foo"},
      {":method", "GET"},
      {":host", "test.host"},
      {":scheme", "https"},
  };
  {
    InSequence seq;
    EXPECT_CALL(*mock_async_file_manager_, stat);
    EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::Forbidden, _, _, _,
                                                   "file_server_no_valid_directory_behavior"));
  }
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter->decodeHeaders(request_headers, true));
  struct stat stat_result = {};
  stat_result.st_mode = S_IFDIR;
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<struct stat>{stat_result});
  pumpDispatcher();
}

TEST_F(FileServerFilterTest, ErrorsOnDirectoryWithImpossiblyConfiguredBehaviorForCoverage) {
  auto filter = std::make_shared<FileServerFilter>(configFromYaml(R"(
path_mappings:
  - request_path_prefix: /path1
    file_path_prefix: fs1
directory_behaviors:
  - {}
)"));
  initFilter(*filter);
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/path1/foo"},
      {":method", "GET"},
      {":host", "test.host"},
      {":scheme", "https"},
  };
  {
    InSequence seq;
    EXPECT_CALL(*mock_async_file_manager_, stat);
    EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _,
                                                   "file_server_empty_behavior_type"));
  }
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter->decodeHeaders(request_headers, true));
  struct stat stat_result = {};
  stat_result.st_mode = S_IFDIR;
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<struct stat>{stat_result});
  pumpDispatcher();
}

TEST_F(FileServerFilterTest, TriesAllDirectoryBehaviorsInOrder) {
  auto filter = testFilter();
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/path1/foo"},
      {":method", "GET"},
      {":host", "test.host"},
      {":scheme", "https"},
  };
  {
    InSequence seq;
    EXPECT_CALL(*mock_async_file_manager_, stat);
    EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, "fs1/foo/index.html", _, _));
    EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, "fs1/foo/index.txt", _, _));
    EXPECT_CALL(decoder_callbacks_,
                sendLocalReply(Http::Code::Forbidden, _, _, _, "file_server_list_not_implemented"));
  }
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter->decodeHeaders(request_headers, true));
  struct stat stat_result = {};
  stat_result.st_mode = S_IFDIR;
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<struct stat>{stat_result});
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>{absl::NotFoundError("mocked not found index.html")});
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>{absl::NotFoundError("mocked not found index.txt")});
  pumpDispatcher();
}

TEST_F(FileServerFilterTest, ErrorOpeningExistingFileGivesErrorResponse) {
  auto filter = testFilter();
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/path1/foo/index.html"},
      {":method", "GET"},
      {":host", "test.host"},
      {":scheme", "https"},
  };
  {
    InSequence seq;
    EXPECT_CALL(*mock_async_file_manager_, stat);
    EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, "fs1/foo/index.html", _, _));
    EXPECT_CALL(decoder_callbacks_,
                sendLocalReply(Http::Code::Forbidden, _, _, _, "file_server_open_error"));
  }
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter->decodeHeaders(request_headers, true));
  struct stat stat_result = {};
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<struct stat>{stat_result});
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<AsyncFileHandle>{
      absl::PermissionDeniedError("mocked permission denied index.html")});
  pumpDispatcher();
}

TEST_F(FileServerFilterTest, OpeningIndexFileStartsFileAndStatErrorGivesErrorResponse) {
  auto filter = testFilter();
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/path1/foo"},
      {":method", "GET"},
      {":host", "test.host"},
      {":scheme", "https"},
  };
  makeMockFile();
  {
    InSequence seq;
    EXPECT_CALL(*mock_async_file_manager_, stat);
    EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, "fs1/foo/index.html", _, _));
    EXPECT_CALL(*mock_file_handle_, stat);
    EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::InternalServerError, _, _, _,
                                                   "file_server_opened_file_stat_failed"));
  }
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter->decodeHeaders(request_headers, true));
  struct stat stat_result = {};
  stat_result.st_mode = S_IFDIR;
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<struct stat>{stat_result});
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<AsyncFileHandle>{mock_file_handle_});
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<struct stat>{absl::InternalError("mocked stat-for-size fail")});
  pumpDispatcher();
}

TEST_F(FileServerFilterTest, HeadRequestJustStatsFileAndResponds) {
  auto filter = testFilter();
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/path1/foo/index.html"},
      {":method", "HEAD"},
      {":host", "test.host"},
      {":scheme", "https"},
  };
  makeMockFile();
  Http::TestResponseHeaderMapImpl expected_headers{
      {":status", "200"},
      {"accept-ranges", "bytes"},
      {"content-length", "12345"},
      {"content-type", "text/html"},
  };
  {
    InSequence seq;
    EXPECT_CALL(*mock_async_file_manager_, stat);
    EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, "fs1/foo/index.html", _, _));
    EXPECT_CALL(*mock_file_handle_, stat);
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&expected_headers), true));
  }
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter->decodeHeaders(request_headers, true));
  struct stat stat_result = {};
  stat_result.st_size = 12345;
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<struct stat>{stat_result});
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<AsyncFileHandle>{mock_file_handle_});
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<struct stat>{stat_result});
  pumpDispatcher();
}

TEST_F(FileServerFilterTest, GetRequestResetsStreamOnReadError) {
  auto filter = testFilter();
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/path1/foo/index.html"},
      {":method", "GET"},
      {":host", "test.host"},
      {":scheme", "https"},
  };
  makeMockFile();
  Http::TestResponseHeaderMapImpl expected_headers{
      {":status", "200"},
      {"accept-ranges", "bytes"},
      {"content-length", "12345"},
      {"content-type", "text/html"},
  };
  {
    InSequence seq;
    EXPECT_CALL(*mock_async_file_manager_, stat);
    EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, "fs1/foo/index.html", _, _));
    EXPECT_CALL(*mock_file_handle_, stat);
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&expected_headers), false));
    EXPECT_CALL(*mock_file_handle_, read(_, 0, 12345, _));
    EXPECT_CALL(decoder_callbacks_, resetStream);
  }
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter->decodeHeaders(request_headers, true));
  struct stat stat_result = {};
  stat_result.st_size = 12345;
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<struct stat>{stat_result});
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<AsyncFileHandle>{mock_file_handle_});
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<struct stat>{stat_result});
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>{absl::InternalError("mocked read error")});
  pumpDispatcher();
  EXPECT_EQ(responseCodeDetails(), "file_server_read_operation_failed");
}

TEST_F(FileServerFilterTest, GetRequestPausesWhenOverBufferLimit) {
  auto filter = testFilter();
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/path1/foo/index.html"},
      {":method", "GET"},
      {":host", "test.host"},
      {":scheme", "https"},
  };
  makeMockFile();
  Http::TestResponseHeaderMapImpl expected_headers{
      {":status", "200"},
      {"accept-ranges", "bytes"},
      {"content-length", "42000"},
      {"content-type", "text/html"},
  };
  // chunk1 is the max read size.
  std::string chunk1(32 * 1024, 'A');
  // chunk2 is the remainder.
  std::string chunk2(42000 - chunk1.length(), 'B');
  {
    InSequence seq;
    EXPECT_CALL(*mock_async_file_manager_, stat);
    EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, "fs1/foo/index.html", _, _));
    EXPECT_CALL(*mock_file_handle_, stat);
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&expected_headers), false));
    EXPECT_CALL(*mock_file_handle_, read(_, 0, chunk1.length(), _));
    EXPECT_CALL(decoder_callbacks_, encodeData(BufferStringEqual(chunk1), false));
    EXPECT_CALL(*mock_file_handle_, read(_, chunk1.length(), chunk2.length(), _));
    EXPECT_CALL(decoder_callbacks_, encodeData(BufferStringEqual(chunk2), true));
  }
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter->decodeHeaders(request_headers, true));
  struct stat stat_result = {};
  stat_result.st_size = chunk1.length() + chunk2.length();
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<struct stat>{stat_result});
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<AsyncFileHandle>{mock_file_handle_});
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<struct stat>{stat_result});
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>{std::make_unique<Buffer::OwnedImpl>(chunk1)});
  filter->onAboveWriteBufferHighWatermark();
  pumpDispatcher();
  ASSERT_TRUE(mock_async_file_manager_->queue_.empty())
      << "next action should not have been queued due to high watermark";
  filter->onBelowWriteBufferLowWatermark();
  ASSERT_FALSE(mock_async_file_manager_->queue_.empty())
      << "next action should have been queued due to low watermark";
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>{std::make_unique<Buffer::OwnedImpl>(chunk2)});
  pumpDispatcher();
  EXPECT_EQ(responseCodeDetails(), "file_server");
}

TEST_F(FileServerFilterTest, BufferLimitsDontPauseIfClearedBeforeActionCompletes) {
  auto filter = testFilter();
  Http::TestRequestHeaderMapImpl request_headers{
      {":path", "/path1/foo/index.html"},
      {":method", "GET"},
      {":host", "test.host"},
      {":scheme", "https"},
  };
  makeMockFile();
  Http::TestResponseHeaderMapImpl expected_headers{
      {":status", "200"},
      {"accept-ranges", "bytes"},
      {"content-length", "42000"},
      {"content-type", "text/html"},
  };
  // chunk1 is the max read size.
  std::string chunk1(32 * 1024, 'A');
  // chunk2 is the remainder.
  std::string chunk2(42000 - chunk1.length(), 'B');
  {
    InSequence seq;
    EXPECT_CALL(*mock_async_file_manager_, stat);
    EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, "fs1/foo/index.html", _, _));
    EXPECT_CALL(*mock_file_handle_, stat);
    EXPECT_CALL(decoder_callbacks_, encodeHeaders_(HeaderMapEqualRef(&expected_headers), false));
    EXPECT_CALL(*mock_file_handle_, read(_, 0, chunk1.length(), _));
    EXPECT_CALL(decoder_callbacks_, encodeData(BufferStringEqual(chunk1), false));
    EXPECT_CALL(*mock_file_handle_, read(_, chunk1.length(), chunk2.length(), _));
    EXPECT_CALL(decoder_callbacks_, encodeData(BufferStringEqual(chunk2), true));
  }
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter->decodeHeaders(request_headers, true));
  struct stat stat_result = {};
  stat_result.st_size = chunk1.length() + chunk2.length();
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<struct stat>{stat_result});
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<AsyncFileHandle>{mock_file_handle_});
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<struct stat>{stat_result});
  pumpDispatcher();
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>{std::make_unique<Buffer::OwnedImpl>(chunk1)});
  filter->onAboveWriteBufferHighWatermark();
  filter->onBelowWriteBufferLowWatermark();
  pumpDispatcher();
  ASSERT_FALSE(mock_async_file_manager_->queue_.empty())
      << "next action should have been queued because watermark was cleared before previous action "
         "completed";
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>{std::make_unique<Buffer::OwnedImpl>(chunk2)});
  pumpDispatcher();
  EXPECT_EQ(responseCodeDetails(), "file_server");
}

} // namespace FileServer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
