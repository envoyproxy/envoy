#include "envoy/http/header_map.h"
#include "envoy/registry/registry.h"
#include "envoy/singleton/manager.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/filesystem/directory.h"
#include "source/extensions/filters/http/cache/cache_entry_utils.h"
#include "source/extensions/filters/http/cache/cache_headers_utils.h"
#include "source/extensions/http/cache/file_system_http_cache/cache_eviction_thread.h"
#include "source/extensions/http/cache/file_system_http_cache/cache_file_fixed_block.h"
#include "source/extensions/http/cache/file_system_http_cache/cache_file_header_proto_util.h"
#include "source/extensions/http/cache/file_system_http_cache/file_system_http_cache.h"

#include "test/extensions/common/async_files/mocks.h"
#include "test/extensions/filters/http/cache/common.h"
#include "test/extensions/filters/http/cache/http_cache_implementation_test_common.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "absl/cleanup/cleanup.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

using Common::AsyncFiles::AsyncFileHandle;
using Common::AsyncFiles::MockAsyncFileContext;
using Common::AsyncFiles::MockAsyncFileHandle;
using Common::AsyncFiles::MockAsyncFileManager;
using Common::AsyncFiles::MockAsyncFileManagerFactory;
using ::envoy::extensions::filters::http::cache::v3::CacheConfig;
using ::testing::HasSubstr;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrictMock;

absl::string_view yaml_config = R"(
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.http.cache.file_system_http_cache.v3.FileSystemHttpCacheConfig
    manager_config:
      thread_pool:
        thread_count: 1
    cache_path: /tmp
)";

class FileSystemCacheTestContext {
public:
  FileSystemCacheTestContext() {
    cache_path_ = absl::StrCat(env_.temporaryDirectory(), "/");
    ConfigProto cfg = testConfig();
    deleteCacheFiles(cfg.cache_path());
    auto cache_config = cacheConfig(cfg);
    const std::string type{
        TypeUtil::typeUrlToDescriptorFullName(cache_config.typed_config().type_url())};
    http_cache_factory_ = Registry::FactoryRegistry<HttpCacheFactory>::getFactoryByType(type);
    if (http_cache_factory_ == nullptr) {
      throw EnvoyException(
          fmt::format("Didn't find a registered implementation for type: '{}'", type));
    }
    ON_CALL(context_.server_factory_context_.api_, threadFactory())
        .WillByDefault([]() -> Thread::ThreadFactory& { return Thread::threadFactoryForTest(); });
  }

  void initCache() {
    cache_ = std::dynamic_pointer_cast<FileSystemHttpCache>(
        http_cache_factory_->getCache(cacheConfig(testConfig()), context_));
  }

  void waitForEvictionThreadIdle() { cache_->cache_eviction_thread_.waitForIdle(); }

  ConfigProto testConfig() {
    envoy::extensions::filters::http::cache::v3::CacheConfig cache_config;
    TestUtility::loadFromYaml(std::string(yaml_config), cache_config);
    ConfigProto cfg;
    MessageUtil::unpackTo(cache_config.typed_config(), cfg);
    cfg.set_cache_path(cache_path_);
    return cfg;
  }

  envoy::extensions::filters::http::cache::v3::CacheConfig cacheConfig(ConfigProto cfg) {
    envoy::extensions::filters::http::cache::v3::CacheConfig cache_config;
    cache_config.mutable_typed_config()->PackFrom(cfg);
    return cache_config;
  }

protected:
  void deleteCacheFiles(std::string path) {
    for (const auto& it : ::Envoy::Filesystem::Directory(path)) {
      if (absl::StartsWith(it.name_, "cache-")) {
        env_.removePath(absl::StrCat(path, it.name_));
      }
    }
  }

  ::Envoy::TestEnvironment env_;
  std::string cache_path_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  std::shared_ptr<FileSystemHttpCache> cache_;
  LogLevelSetter log_level_ = LogLevelSetter(spdlog::level::debug);
  HttpCacheFactory* http_cache_factory_;
};

class FileSystemHttpCacheTestWithNoDefaultCache : public FileSystemCacheTestContext,
                                                  public ::testing::Test {};

TEST_F(FileSystemHttpCacheTestWithNoDefaultCache, InitialStatsAreSetCorrectly) {
  const std::string file_1_contents = "XXXXX";
  const std::string file_2_contents = "YYYYYYYYYY";
  const uint64_t max_count = 99;
  const uint64_t max_size = 87654321;
  ConfigProto cfg = testConfig();
  cfg.mutable_max_cache_entry_count()->set_value(max_count);
  cfg.mutable_max_cache_size_bytes()->set_value(max_size);
  env_.writeStringToFileForTest(absl::StrCat(cache_path_, "cache-a"), file_1_contents, true);
  env_.writeStringToFileForTest(absl::StrCat(cache_path_, "cache-b"), file_2_contents, true);
  cache_ = std::dynamic_pointer_cast<FileSystemHttpCache>(
      http_cache_factory_->getCache(cacheConfig(cfg), context_));
  waitForEvictionThreadIdle();
  EXPECT_EQ(cache_->stats().size_limit_bytes_.value(), max_size);
  EXPECT_EQ(cache_->stats().size_limit_count_.value(), max_count);
  EXPECT_EQ(cache_->stats().size_bytes_.value(), file_1_contents.size() + file_2_contents.size());
  EXPECT_EQ(cache_->stats().size_count_.value(), 2);
  EXPECT_EQ(cache_->stats().eviction_runs_.value(), 0);
}

TEST_F(FileSystemHttpCacheTestWithNoDefaultCache, EvictsOldestFilesUntilUnderCountLimit) {
  const std::string file_contents = "XXXXX";
  const uint64_t max_count = 2;
  ConfigProto cfg = testConfig();
  cfg.mutable_max_cache_entry_count()->set_value(max_count);
  env_.writeStringToFileForTest(absl::StrCat(cache_path_, "cache-a"), file_contents, true);
  env_.writeStringToFileForTest(absl::StrCat(cache_path_, "cache-b"), file_contents, true);
  // TODO(#24994): replace this with backdating the files when that's possible.
  sleep(1); // NO_CHECK_FORMAT(real_time)
  cache_ = std::dynamic_pointer_cast<FileSystemHttpCache>(
      http_cache_factory_->getCache(cacheConfig(cfg), context_));
  waitForEvictionThreadIdle();
  EXPECT_EQ(cache_->stats().eviction_runs_.value(), 0);
  EXPECT_EQ(cache_->stats().size_bytes_.value(), file_contents.size() * 2);
  EXPECT_EQ(cache_->stats().size_count_.value(), 2);
  env_.writeStringToFileForTest(absl::StrCat(cache_path_, "cache-c"), file_contents, true);
  env_.writeStringToFileForTest(absl::StrCat(cache_path_, "cache-d"), file_contents, true);
  cache_->trackFileAdded(file_contents.size());
  cache_->trackFileAdded(file_contents.size());
  waitForEvictionThreadIdle();
  EXPECT_EQ(cache_->stats().size_bytes_.value(), file_contents.size() * 2);
  EXPECT_EQ(cache_->stats().size_count_.value(), 2);
  EXPECT_FALSE(Filesystem::fileSystemForTest().fileExists(absl::StrCat(cache_path_, "cache-a")));
  EXPECT_FALSE(Filesystem::fileSystemForTest().fileExists(absl::StrCat(cache_path_, "cache-b")));
  EXPECT_TRUE(Filesystem::fileSystemForTest().fileExists(absl::StrCat(cache_path_, "cache-c")));
  EXPECT_TRUE(Filesystem::fileSystemForTest().fileExists(absl::StrCat(cache_path_, "cache-d")));
  // There may have been one or two eviction runs here, because there's a race
  // between the eviction and the second file being added. Either amount of runs
  // is valid, as the eventual consistency is achieved either way.
  EXPECT_THAT(cache_->stats().eviction_runs_.value(), testing::AnyOf(1, 2));
}

TEST_F(FileSystemHttpCacheTestWithNoDefaultCache, EvictsOldestFilesUntilUnderSizeLimit) {
  const std::string file_contents = "XXXXX";
  const std::string large_file_contents = "XXXXXXXXXXXX";
  const uint64_t max_size = large_file_contents.size();
  ConfigProto cfg = testConfig();
  cfg.mutable_max_cache_size_bytes()->set_value(max_size);
  env_.writeStringToFileForTest(absl::StrCat(cache_path_, "cache-a"), file_contents, true);
  env_.writeStringToFileForTest(absl::StrCat(cache_path_, "cache-b"), file_contents, true);
  // TODO(#24994): replace this with backdating the files when that's possible.
  sleep(1); // NO_CHECK_FORMAT(real_time)
  cache_ = std::dynamic_pointer_cast<FileSystemHttpCache>(
      http_cache_factory_->getCache(cacheConfig(cfg), context_));
  waitForEvictionThreadIdle();
  EXPECT_EQ(cache_->stats().eviction_runs_.value(), 0);
  env_.writeStringToFileForTest(absl::StrCat(cache_path_, "cache-c"), large_file_contents, true);
  EXPECT_EQ(cache_->stats().size_bytes_.value(), file_contents.size() * 2);
  EXPECT_EQ(cache_->stats().size_count_.value(), 2);
  cache_->trackFileAdded(large_file_contents.size());
  waitForEvictionThreadIdle();
  EXPECT_EQ(cache_->stats().size_bytes_.value(), large_file_contents.size());
  EXPECT_EQ(cache_->stats().size_count_.value(), 1);
  EXPECT_FALSE(Filesystem::fileSystemForTest().fileExists(absl::StrCat(cache_path_, "cache-a")));
  EXPECT_FALSE(Filesystem::fileSystemForTest().fileExists(absl::StrCat(cache_path_, "cache-b")));
  EXPECT_TRUE(Filesystem::fileSystemForTest().fileExists(absl::StrCat(cache_path_, "cache-c")));
  EXPECT_EQ(cache_->stats().eviction_runs_.value(), 1);
}

class FileSystemHttpCacheTest : public FileSystemCacheTestContext, public ::testing::Test {
  void SetUp() override { initCache(); }
};

MATCHER_P2(IsStatTag, name, value, "") {
  if (!ExplainMatchResult(name, arg.name_, result_listener) ||
      !ExplainMatchResult(value, arg.value_, result_listener)) {
    *result_listener << "\nexpected {name: \"" << name << "\", value: \"" << value
                     << "\"},\n but got {name: \"" << arg.name_ << "\", value: \"" << arg.value_
                     << "\"}\n";
    return false;
  }
  return true;
}

TEST_F(FileSystemHttpCacheTest, StatsAreConstructedCorrectly) {
  std::string cache_path_no_periods = absl::StrReplaceAll(cache_path_, {{".", "_"}});
  // Validate that a gauge has appropriate name and tags.
  EXPECT_EQ(cache_->stats().size_bytes_.tagExtractedName(), "cache.size_bytes");
  EXPECT_THAT(cache_->stats().size_bytes_.tags(),
              ::testing::ElementsAre(IsStatTag("cache_path", cache_path_no_periods)));
  // Validate that a counter has appropriate name and tags.
  EXPECT_EQ(cache_->stats().eviction_runs_.tagExtractedName(), "cache.eviction_runs");
  EXPECT_THAT(cache_->stats().eviction_runs_.tags(),
              ::testing::ElementsAre(IsStatTag("cache_path", cache_path_no_periods)));
  EXPECT_EQ(cache_->stats().cache_hit_.tagExtractedName(), "cache.event");
  EXPECT_EQ(cache_->stats().cache_miss_.tagExtractedName(), "cache.event");
  EXPECT_THAT(cache_->stats().cache_hit_.tags(),
              ::testing::ElementsAre(IsStatTag("cache_path", cache_path_no_periods),
                                     IsStatTag("event_type", "hit")));
  EXPECT_THAT(cache_->stats().cache_miss_.tags(),
              ::testing::ElementsAre(IsStatTag("cache_path", cache_path_no_periods),
                                     IsStatTag("event_type", "miss")));
}

TEST_F(FileSystemHttpCacheTest, TrackFileRemovedClampsAtZero) {
  cache_->trackFileAdded(1);
  EXPECT_EQ(cache_->stats().size_bytes_.value(), 1);
  EXPECT_EQ(cache_->stats().size_count_.value(), 1);
  cache_->trackFileRemoved(8);
  EXPECT_EQ(cache_->stats().size_bytes_.value(), 0);
  EXPECT_EQ(cache_->stats().size_count_.value(), 0);
  // Remove a second time to ensure that count going below zero also clamps at zero.
  cache_->trackFileRemoved(8);
  EXPECT_EQ(cache_->stats().size_bytes_.value(), 0);
  EXPECT_EQ(cache_->stats().size_count_.value(), 0);
}

TEST_F(FileSystemHttpCacheTest, ExceptionOnTryingToCreateCachesWithDistinctConfigsOnSamePath) {
  ConfigProto cfg = testConfig();
  cfg.mutable_manager_config()->mutable_thread_pool()->set_thread_count(2);
  EXPECT_ANY_THROW(http_cache_factory_->getCache(cacheConfig(cfg), context_));
}

TEST_F(FileSystemHttpCacheTest, IdenticalCacheConfigReturnsSameCacheInstance) {
  ConfigProto cfg = testConfig();
  auto second_cache = http_cache_factory_->getCache(cacheConfig(cfg), context_);
  EXPECT_EQ(cache_, second_cache);
}

TEST_F(FileSystemHttpCacheTest, CacheConfigsWithDifferentPathsReturnDistinctCacheInstances) {
  ConfigProto cfg = testConfig();
  cfg.set_cache_path("/tmp");
  auto second_cache = http_cache_factory_->getCache(cacheConfig(cfg), context_);
  EXPECT_NE(cache_, second_cache);
}

CacheConfig varyAllowListConfig() {
  CacheConfig config;
  config.add_allowed_vary_headers()->set_exact("accept");
  return config;
}

class MockSingletonManager : public Singleton::ManagerImpl {
public:
  MockSingletonManager() : Singleton::ManagerImpl(Thread::threadFactoryForTest()) {
    // By default just act like a real SingletonManager, but allow overrides.
    ON_CALL(*this, get(_, _))
        .WillByDefault(std::bind(&MockSingletonManager::realGet, this, std::placeholders::_1,
                                 std::placeholders::_2));
  }

  MOCK_METHOD(Singleton::InstanceSharedPtr, get,
              (const std::string& name, Singleton::SingletonFactoryCb cb));
  Singleton::InstanceSharedPtr realGet(const std::string& name, Singleton::SingletonFactoryCb cb) {
    return Singleton::ManagerImpl::get(name, cb);
  }
};

class FileSystemHttpCacheTestWithMockFiles : public FileSystemHttpCacheTest {
public:
  FileSystemHttpCacheTestWithMockFiles() {
    ON_CALL(context_.server_factory_context_, singletonManager())
        .WillByDefault(ReturnRef(mock_singleton_manager_));
    ON_CALL(mock_singleton_manager_, get(HasSubstr("async_file_manager_factory_singleton"), _))
        .WillByDefault(Return(mock_async_file_manager_factory_));
    ON_CALL(*mock_async_file_manager_factory_, getAsyncFileManager(_, _))
        .WillByDefault(Return(mock_async_file_manager_));
    request_headers_.setMethod("GET");
    request_headers_.setHost("example.com");
    request_headers_.setScheme("https");
    request_headers_.setCopy(Http::CustomHeaders::get().CacheControl, "max-age=3600");
    request_headers_.setPath("/");
    expect_false_callback_ = [this](bool result) {
      EXPECT_FALSE(result);
      false_callbacks_called_++;
    };
    expect_true_callback_ = [this](bool result) {
      EXPECT_TRUE(result);
      true_callbacks_called_++;
    };
    trailers_size_ = bufferFromProto(makeCacheFileTrailerProto(response_trailers_)).length();
    key_ = LookupRequest{request_headers_, time_system_.systemTime(), vary_allow_list_}.key();
    headers_size_ = headerProtoSize(makeCacheFileHeaderProto(key_, response_headers_, metadata_));
  }

  Buffer::InstancePtr testHeaderBlock(size_t body_size) {
    CacheFileFixedBlock block;
    block.setHeadersSize(headers_size_);
    block.setTrailersSize(trailers_size_);
    block.setBodySize(body_size);
    auto buffer = std::make_unique<Buffer::OwnedImpl>();
    block.serializeToBuffer(*buffer);
    return buffer;
  }

  CacheFileHeader testHeaderProto() {
    return makeCacheFileHeaderProto(key_, response_headers_, metadata_);
  }

  Buffer::InstancePtr testHeaderBuffer() {
    return std::make_unique<Buffer::OwnedImpl>(bufferFromProto(testHeaderProto()));
  }

  void SetUp() override { initCache(); }

  LookupContextPtr testLookupContext() {
    auto request = LookupRequest{request_headers_, time_system_.systemTime(), vary_allow_list_};
    key_ = request.key();
    return cache_->makeLookupContext(std::move(request), decoder_callbacks_);
  }

  InsertContextPtr testInserter() {
    auto ret = cache_->makeInsertContext(testLookupContext(), encoder_callbacks_);
    return ret;
  }

  LookupResult testLookupResult() {
    // Use a different mock file handle while in this function, then restore the original.
    absl::Cleanup restore_file_handle([this, original_file_handle = mock_async_file_handle_]() {
      mock_async_file_handle_ = original_file_handle;
    });
    mock_async_file_handle_ = std::make_shared<MockAsyncFileContext>(mock_async_file_manager_);
    LookupResult result;
    {
      auto lookup = testLookupContext();
      absl::Cleanup destroy_lookup([&lookup]() { lookup->onDestroy(); });
      EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, _, _));
      EXPECT_CALL(*mock_async_file_handle_, read(_, _, _)).Times(2);
      lookup->getHeaders([&](LookupResult&& r) { result = std::move(r); });
      mock_async_file_manager_->nextActionCompletes(
          absl::StatusOr<Buffer::InstancePtr>(testHeaderBlock(0)));
      mock_async_file_manager_->nextActionCompletes(
          absl::StatusOr<Buffer::InstancePtr>(testHeaderBuffer()));
    }
    // Consume the close from the queue that occurs when lookup context is destroyed.
    mock_async_file_manager_->nextActionCompletes(absl::OkStatus());
    return result;
  }

protected:
  ::testing::NiceMock<MockSingletonManager> mock_singleton_manager_;
  std::shared_ptr<MockAsyncFileManagerFactory> mock_async_file_manager_factory_ =
      std::make_shared<NiceMock<MockAsyncFileManagerFactory>>();
  std::shared_ptr<MockAsyncFileManager> mock_async_file_manager_ =
      std::make_shared<NiceMock<MockAsyncFileManager>>();
  MockAsyncFileHandle mock_async_file_handle_ =
      std::make_shared<StrictMock<MockAsyncFileContext>>(mock_async_file_manager_);
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  Event::SimulatedTimeSystem time_system_;
  Http::TestRequestHeaderMapImpl request_headers_;
  VaryAllowList vary_allow_list_{varyAllowListConfig().allowed_vary_headers()};
  DateFormatter formatter_{"%a, %d %b %Y %H:%M:%S GMT"};
  Http::TestResponseHeaderMapImpl response_headers_{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"cache-control", "public,max-age=3600"},
  };
  Http::TestResponseTrailerMapImpl response_trailers_{{"fruit", "banana"}};
  const ResponseMetadata metadata_{time_system_.systemTime()};
  Key key_;
  int false_callbacks_called_ = 0;
  int true_callbacks_called_ = 0;
  std::function<void(bool result)> expect_false_callback_;
  std::function<void(bool result)> expect_true_callback_;
  size_t headers_size_;
  size_t trailers_size_;
};

TEST_F(FileSystemHttpCacheTestWithMockFiles, WriteVaryNodeFailingToCreateFileJustAborts) {
  auto inserter = testInserter();
  absl::Cleanup destroy_inserter{[&inserter]() { inserter->onDestroy(); }};
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"cache-control", "public,max-age=3600"},
      {"vary", "accept"}};
  // one file created for the vary node, one for the actual write.
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _)).Times(2);
  inserter->insertHeaders(
      response_headers, metadata_, [&](bool result) { EXPECT_FALSE(result); }, true);
  // File handle for the vary node.
  // (This is the failure under test, we expect write to *not* be called.)
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>{absl::UnknownError("create failure for vary node")});
  // Fail to create file for the cache entry node.
  // (This provokes the false callback to insertHeaders.)
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>{absl::UnknownError("open failure")});
  // File handle was not used and is expected to be closed.
  EXPECT_OK(mock_async_file_handle_->close([](absl::Status) {}));
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, WriteVaryNodeFailingToWriteJustClosesTheFile) {
  auto inserter = testInserter();
  absl::Cleanup destroy_inserter{[&inserter]() { inserter->onDestroy(); }};
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"cache-control", "public,max-age=3600"},
      {"vary", "accept"}};
  // one file created for the vary node, one for the actual write.
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _)).Times(2);
  inserter->insertHeaders(
      response_headers, metadata_, [&](bool result) { EXPECT_FALSE(result); }, true);
  EXPECT_CALL(*mock_async_file_handle_, write(_, _, _));
  // File handle for the vary node.
  // (This triggers the expected write call.)
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>{mock_async_file_handle_});
  // Fail to create file for the cache entry node.
  // (This provokes the false callback to insertHeaders.)
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>{absl::UnknownError("open failure")});
  // Fail to write for the vary node.
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(absl::UnknownError("write failure")));
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, LookupDuringAnotherInsertPreventsInserts) {
  auto inserter = testInserter();
  absl::Cleanup destroy_inserter{[&inserter]() { inserter->onDestroy(); }};
  // First inserter will try to create a file.
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _));
  inserter->insertHeaders(
      response_headers_, metadata_, [&](bool result) { EXPECT_FALSE(result); }, false);

  auto inserter2 = testInserter();
  absl::Cleanup destroy_inserter2{[&inserter2]() { inserter2->onDestroy(); }};
  // Allow the first inserter to complete after the second lookup was made.
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>{absl::UnknownError("intentionally failed to open file")});
  inserter2->insertHeaders(response_headers_, metadata_, expect_false_callback_, false);
  inserter2->insertBody(Buffer::OwnedImpl("boop"), expect_false_callback_, false);
  inserter2->insertTrailers(response_trailers_, expect_false_callback_);
  EXPECT_EQ(false_callbacks_called_, 3);
  // The file handle didn't actually get used in this test, but is expected to be closed.
  EXPECT_OK(mock_async_file_handle_->close([](absl::Status) {}));
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, DuplicateInsertWhileInsertInProgressIsPrevented) {
  auto inserter = testInserter();
  absl::Cleanup destroy_inserter{[&inserter]() { inserter->onDestroy(); }};
  auto inserter2 = testInserter();
  absl::Cleanup destroy_inserter2{[&inserter2]() { inserter2->onDestroy(); }};
  // First inserter will try to create a file.
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _));
  inserter->insertHeaders(response_headers_, metadata_, expect_false_callback_, false);
  inserter2->insertHeaders(response_headers_, metadata_, expect_false_callback_, false);
  // Allow the first inserter to complete after the second insert was called.
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>{absl::UnknownError("intentionally failed to open file")});
  inserter2->insertBody(Buffer::OwnedImpl("boop"), expect_false_callback_, false);
  inserter2->insertTrailers(response_trailers_, expect_false_callback_);
  EXPECT_EQ(false_callbacks_called_, 4);
  // The file handle didn't actually get used in this test, but is expected to be closed.
  EXPECT_OK(mock_async_file_handle_->close([](absl::Status) {}));
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, FailedOpenForReadReturnsMiss) {
  auto lookup = testLookupContext();
  absl::Cleanup destroy_lookup([&lookup]() { lookup->onDestroy(); });
  LookupResult result;
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, _, _));
  lookup->getHeaders([&](LookupResult&& r) { result = std::move(r); });
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(absl::UnknownError("Intentionally failed to open file")));
  // File handle didn't get used but is expected to be closed.
  EXPECT_OK(mock_async_file_handle_->close([](absl::Status) {}));
  EXPECT_EQ(result.cache_entry_status_, CacheEntryStatus::Unusable);
  EXPECT_EQ(cache_->stats().cache_miss_.value(), 1);
  EXPECT_EQ(cache_->stats().cache_hit_.value(), 0);
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, FailedReadOfHeaderBlockInvalidatesTheCacheEntry) {
  // Fake-add two files of size 12345, so we can validate the stats decrease of removing a file.
  cache_->trackFileAdded(12345);
  cache_->trackFileAdded(12345);
  EXPECT_EQ(cache_->stats().size_bytes_.value(), 2 * 12345);
  EXPECT_EQ(cache_->stats().size_count_.value(), 2);
  auto lookup = testLookupContext();
  absl::Cleanup destroy_lookup([&lookup]() { lookup->onDestroy(); });
  LookupResult result;
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, _, _));
  EXPECT_CALL(*mock_async_file_handle_, read(0, CacheFileFixedBlock::size(), _));
  lookup->getHeaders([&](LookupResult&& r) { result = std::move(r); });
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  EXPECT_CALL(*mock_async_file_manager_, stat(_, _));
  EXPECT_CALL(*mock_async_file_manager_, unlink(_, _));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(absl::UnknownError("intentional failure to read")));
  struct stat stat_result = {};
  stat_result.st_size = 12345;
  // stat
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<struct stat>{stat_result});
  // unlink
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus());
  EXPECT_EQ(result.cache_entry_status_, CacheEntryStatus::Unusable);
  waitForEvictionThreadIdle();
  // Should have deducted the size of the file that got deleted. Since we started at 2 * 12345,
  // this should make the value 12345.
  EXPECT_EQ(cache_->stats().size_bytes_.value(), 12345);
  // Should have deducted one file for the file that got deleted. Since we started at 2,
  // this should make the value 1.
  EXPECT_EQ(cache_->stats().size_count_.value(), 1);
}

Buffer::InstancePtr invalidHeaderBlock() {
  CacheFileFixedBlock block;
  auto buffer = std::make_unique<Buffer::OwnedImpl>();
  block.serializeToBuffer(*buffer);
  // Replace the four byte id at the start with a bad id.
  buffer->drain(4);
  buffer->prepend("BAD!");
  return buffer;
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, ReadWithInvalidHeaderBlockInvalidatesTheCacheEntry) {
  auto lookup = testLookupContext();
  absl::Cleanup destroy_lookup([&lookup]() { lookup->onDestroy(); });
  LookupResult result;
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, _, _));
  EXPECT_CALL(*mock_async_file_handle_, read(0, CacheFileFixedBlock::size(), _));
  lookup->getHeaders([&](LookupResult&& r) { result = std::move(r); });
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  EXPECT_CALL(*mock_async_file_manager_, stat(_, _));
  EXPECT_CALL(*mock_async_file_manager_, unlink(_, _));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(invalidHeaderBlock()));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<struct stat>{
      absl::UnknownError("intentionally failed to stat, for coverage")});
  mock_async_file_manager_->nextActionCompletes(
      absl::UnknownError("intentionally failed to unlink, for coverage"));
  EXPECT_EQ(result.cache_entry_status_, CacheEntryStatus::Unusable);
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, FailedReadOfHeaderProtoInvalidatesTheCacheEntry) {
  auto lookup = testLookupContext();
  absl::Cleanup destroy_lookup([&lookup]() { lookup->onDestroy(); });
  LookupResult result;
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, _, _));
  EXPECT_CALL(*mock_async_file_handle_, read(0, CacheFileFixedBlock::size(), _));
  EXPECT_CALL(*mock_async_file_handle_, read(CacheFileFixedBlock::size(), headers_size_, _));
  lookup->getHeaders([&](LookupResult&& r) { result = std::move(r); });
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(testHeaderBlock(0)));
  EXPECT_CALL(*mock_async_file_manager_, stat(_, _));
  EXPECT_CALL(*mock_async_file_manager_, unlink(_, _));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(absl::UnknownError("intentional failure to read")));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<struct stat>{
      absl::UnknownError("intentionally failed to stat, for coverage")});
  mock_async_file_manager_->nextActionCompletes(
      absl::UnknownError("intentionally failed to unlink, for coverage"));
  EXPECT_EQ(result.cache_entry_status_, CacheEntryStatus::Unusable);
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, FailedReadOfBodyInvalidatesTheCacheEntry) {
  auto lookup = testLookupContext();
  absl::Cleanup destroy_lookup([&lookup]() { lookup->onDestroy(); });
  LookupResult result;
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, _, _));
  EXPECT_CALL(*mock_async_file_handle_, read(0, CacheFileFixedBlock::size(), _));
  EXPECT_CALL(*mock_async_file_handle_, read(CacheFileFixedBlock::size(), headers_size_, _));
  lookup->getHeaders([&](LookupResult&& r) { result = std::move(r); });
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(testHeaderBlock(0)));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(testHeaderBuffer()));
  // result should be populated.
  EXPECT_NE(result.cache_entry_status_, CacheEntryStatus::Unusable);
  EXPECT_CALL(*mock_async_file_handle_, read(_, _, _));
  lookup->getBody(AdjustedByteRange(0, 8),
                  [&](Buffer::InstancePtr body) { EXPECT_EQ(body.get(), nullptr); });
  EXPECT_CALL(*mock_async_file_manager_, stat(_, _));
  EXPECT_CALL(*mock_async_file_manager_, unlink(_, _));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(absl::UnknownError("intentional failure to read")));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<struct stat>{
      absl::UnknownError("intentionally failed to stat, for coverage")});
  mock_async_file_manager_->nextActionCompletes(
      absl::UnknownError("intentionally failed to unlink, for coverage"));
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, FailedReadOfTrailersInvalidatesTheCacheEntry) {
  auto lookup = testLookupContext();
  absl::Cleanup destroy_lookup([&lookup]() { lookup->onDestroy(); });
  LookupResult result;
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, _, _));
  EXPECT_CALL(*mock_async_file_handle_, read(0, CacheFileFixedBlock::size(), _));
  EXPECT_CALL(*mock_async_file_handle_, read(CacheFileFixedBlock::size(), headers_size_, _));
  lookup->getHeaders([&](LookupResult&& r) { result = std::move(r); });
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(testHeaderBlock(0)));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(testHeaderBuffer()));
  // result should be populated.
  EXPECT_NE(result.cache_entry_status_, CacheEntryStatus::Unusable);
  EXPECT_CALL(*mock_async_file_handle_, read(_, 8, _));
  lookup->getBody(AdjustedByteRange(0, 8),
                  [&](Buffer::InstancePtr body) { EXPECT_EQ(body->toString(), "beepbeep"); });
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(std::make_unique<Buffer::OwnedImpl>("beepbeep")));
  EXPECT_CALL(*mock_async_file_handle_, read(_, _, _));
  // No point validating that the trailers are empty since that's not even particularly
  // desirable behavior - it's a quirk of the filter that we can't properly signify an error.
  lookup->getTrailers([&](Http::ResponseTrailerMapPtr) {});
  EXPECT_CALL(*mock_async_file_manager_, stat(_, _));
  EXPECT_CALL(*mock_async_file_manager_, unlink(_, _));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<Buffer::InstancePtr>(
      absl::UnknownError("intentional failure to read trailers")));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<struct stat>{
      absl::UnknownError("intentionally failed to stat, for coverage")});
  mock_async_file_manager_->nextActionCompletes(
      absl::UnknownError("intentionally failed to unlink, for coverage"));
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, ReadWithMultipleBlocksWorksCorrectly) {
  auto lookup = testLookupContext();
  LookupResult result;
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, _, _));
  EXPECT_CALL(*mock_async_file_handle_, read(0, CacheFileFixedBlock::size(), _));
  EXPECT_CALL(*mock_async_file_handle_,
              read(CacheFileFixedBlock::offsetToHeaders(), headers_size_, _));
  lookup->getHeaders([&](LookupResult&& r) { result = std::move(r); });
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(testHeaderBlock(8)));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(testHeaderBuffer()));
  EXPECT_CALL(*mock_async_file_handle_,
              read(CacheFileFixedBlock::offsetToHeaders() + headers_size_, 4, _));
  EXPECT_CALL(*mock_async_file_handle_,
              read(CacheFileFixedBlock::offsetToHeaders() + headers_size_ + 4, 4, _));
  lookup->getBody(AdjustedByteRange(0, 4),
                  [&](Buffer::InstancePtr body) { EXPECT_EQ(body->toString(), "beep"); });
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(std::make_unique<Buffer::OwnedImpl>("beep")));
  lookup->getBody(AdjustedByteRange(4, 8),
                  [&](Buffer::InstancePtr body) { EXPECT_EQ(body->toString(), "boop"); });
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(std::make_unique<Buffer::OwnedImpl>("boop")));
  // While we're here, incidentally test the behavior of aborting a lookup in progress
  // while no file actions are in flight.
  lookup->onDestroy();
  lookup.reset();
  // There should be a file-close in the queue.
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus());
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, DestroyingALookupWithFileActionInFlightCancelsAction) {
  auto lookup = testLookupContext();
  absl::Cleanup destroy_lookup([&lookup]() { lookup->onDestroy(); });
  LookupResult result;
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, _, _));
  EXPECT_CALL(*mock_async_file_manager_, mockCancel()).WillOnce([this]() {
    mock_async_file_manager_->queue_.pop_front();
  });
  lookup->getHeaders([&](LookupResult&& r) { result = std::move(r); });
  // File wasn't used in this test but is expected to be closed.
  EXPECT_OK(mock_async_file_handle_->close([](absl::Status) {}));
}

TEST_F(FileSystemHttpCacheTestWithMockFiles,
       DestroyingInsertContextWithFileActionInFlightCancelsAction) {
  auto inserter = testInserter();
  absl::Cleanup destroy_inserter([&inserter]() { inserter->onDestroy(); });
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _));
  EXPECT_CALL(*mock_async_file_manager_, mockCancel()).WillOnce([this]() {
    mock_async_file_manager_->queue_.pop_front();
  });
  inserter->insertHeaders(response_headers_, metadata_, expect_false_callback_, false);
  // File wasn't used in this test but is expected to be closed.
  EXPECT_OK(mock_async_file_handle_->close([](absl::Status) {}));
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, InsertAbortsOnFailureToWriteEmptyHeaderBlock) {
  auto inserter = testInserter();
  absl::Cleanup destroy_inserter([&inserter]() { inserter->onDestroy(); });
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _));
  EXPECT_CALL(*mock_async_file_handle_, write(_, _, _));
  inserter->insertHeaders(response_headers_, metadata_, expect_false_callback_, false);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(
      absl::UnknownError("intentionally failed write to empty header block")));
  EXPECT_EQ(false_callbacks_called_, 1);
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, InsertAbortsOnFailureToWriteHeaderChunk) {
  auto inserter = testInserter();
  absl::Cleanup destroy_inserter([&inserter]() { inserter->onDestroy(); });
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _));
  EXPECT_CALL(*mock_async_file_handle_, write(_, _, _)).Times(2);
  inserter->insertHeaders(response_headers_, metadata_, expect_false_callback_, false);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(CacheFileFixedBlock::size()));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(absl::UnknownError("intentionally failed write of header chunk")));
  EXPECT_EQ(false_callbacks_called_, 1);
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, InsertAbortsOnFailureToWriteBodyChunk) {
  auto inserter = testInserter();
  absl::Cleanup destroy_inserter([&inserter]() { inserter->onDestroy(); });
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _));
  EXPECT_CALL(*mock_async_file_handle_, write(_, _, _)).Times(3);
  inserter->insertHeaders(response_headers_, metadata_, expect_true_callback_, false);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(CacheFileFixedBlock::size()));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(headers_size_));
  EXPECT_EQ(true_callbacks_called_, 1);
  inserter->insertBody(Buffer::OwnedImpl("woop"), expect_false_callback_, false);
  // Intentionally undersized write of body chunk.
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(1));
  EXPECT_EQ(false_callbacks_called_, 1);
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, InsertAbortsOnFailureToWriteTrailerChunk) {
  auto inserter = testInserter();
  absl::Cleanup destroy_inserter([&inserter]() { inserter->onDestroy(); });
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _));
  EXPECT_CALL(*mock_async_file_handle_, write(_, _, _)).Times(4);
  inserter->insertHeaders(response_headers_, metadata_, expect_true_callback_, false);
  const absl::string_view body = "woop";
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(CacheFileFixedBlock::size()));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(headers_size_));
  EXPECT_EQ(true_callbacks_called_, 1);
  inserter->insertBody(Buffer::OwnedImpl(body), expect_true_callback_, false);
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(body.size()));
  EXPECT_EQ(true_callbacks_called_, 2);
  inserter->insertTrailers(response_trailers_, expect_false_callback_);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(absl::UnknownError("intentionally failed write of trailer chunk")));
  EXPECT_EQ(false_callbacks_called_, 1);
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, InsertAbortsOnFailureToWriteUpdatedHeaderBlock) {
  auto inserter = testInserter();
  absl::Cleanup destroy_inserter([&inserter]() { inserter->onDestroy(); });
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _));
  EXPECT_CALL(*mock_async_file_handle_, write(_, _, _)).Times(5);
  inserter->insertHeaders(response_headers_, metadata_, expect_true_callback_, false);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(CacheFileFixedBlock::size()));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(headers_size_));
  EXPECT_EQ(true_callbacks_called_, 1);
  const absl::string_view body = "woop";
  inserter->insertBody(Buffer::OwnedImpl(body), expect_true_callback_, false);
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(body.size()));
  EXPECT_EQ(true_callbacks_called_, 2);
  inserter->insertTrailers(response_trailers_, expect_false_callback_);
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(trailers_size_));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(
      absl::UnknownError("intentionally failed write of updated header block")));
  EXPECT_EQ(false_callbacks_called_, 1);
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, InsertAbortsOnFailureToLinkFile) {
  auto inserter = testInserter();
  absl::Cleanup destroy_inserter([&inserter]() { inserter->onDestroy(); });
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _));
  EXPECT_CALL(*mock_async_file_handle_, write(_, _, _)).Times(5);
  EXPECT_CALL(*mock_async_file_manager_, stat(_, _));
  EXPECT_CALL(*mock_async_file_manager_, unlink(_, _));
  EXPECT_CALL(*mock_async_file_handle_, createHardLink(_, _));
  inserter->insertHeaders(response_headers_, metadata_, expect_true_callback_, false);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(CacheFileFixedBlock::size()));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(headers_size_));
  EXPECT_EQ(true_callbacks_called_, 1);
  const absl::string_view body = "woop";
  inserter->insertBody(Buffer::OwnedImpl(body), expect_true_callback_, false);
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(body.size()));
  EXPECT_EQ(true_callbacks_called_, 2);
  inserter->insertTrailers(response_trailers_, expect_false_callback_);
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(trailers_size_));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(CacheFileFixedBlock::size()));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<struct stat>{
      absl::UnknownError("intentionally failed to stat, for coverage")});
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus());
  mock_async_file_manager_->nextActionCompletes(
      absl::UnknownError("intentionally failed to link cache file"));
  EXPECT_EQ(false_callbacks_called_, 1);
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, UpdateHeadersAbortsIfFileOpenFailed) {
  time_system_.advanceTimeWait(Seconds(3601));
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"x-whatever", "updated"},
      {"cache-control", "public,max-age=3600"},
  };
  auto lookup_context = testLookupContext();
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, _, _));
  bool update_success;
  cache_->updateHeaders(*lookup_context, response_headers, {time_system_.systemTime()},
                        [&update_success](bool success) { update_success = success; });
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(absl::UnknownError("Intentionally failed to open file")));
  lookup_context->onDestroy();
  EXPECT_FALSE(update_success);
  // File is not used in this test, but is expected to be closed.
  EXPECT_OK(mock_async_file_handle_->close([](absl::Status) {}));
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, UpdateHeadersKeepsTryingIfUnlinkOriginalFails) {
  time_system_.advanceTimeWait(Seconds(3601));
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"x-whatever", "updated"},
      {"cache-control", "public,max-age=3600"},
  };
  auto lookup_context = testLookupContext();
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, _, _));
  EXPECT_CALL(*mock_async_file_manager_, unlink(_, _));
  EXPECT_CALL(*mock_async_file_handle_, read(0, CacheFileFixedBlock::size(), _));
  bool update_success;
  cache_->updateHeaders(*lookup_context, response_headers, {time_system_.systemTime()},
                        [&update_success](bool success) { update_success = success; });
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(
      absl::UnknownError("Intentionally failed to unlink"));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<Buffer::InstancePtr>(
      absl::UnknownError("Intentionally failed to read header block")));
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus()); // close read handle
  lookup_context->onDestroy();
  EXPECT_FALSE(update_success);
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, UpdateHeadersAbortsIfReadHeadersFails) {
  time_system_.advanceTimeWait(Seconds(3601));
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"x-whatever", "updated"},
      {"cache-control", "public,max-age=3600"},
  };
  auto lookup_context = testLookupContext();
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, _, _));
  EXPECT_CALL(*mock_async_file_manager_, unlink(_, _));
  EXPECT_CALL(*mock_async_file_handle_, read(0, CacheFileFixedBlock::size(), _));
  EXPECT_CALL(*mock_async_file_handle_, read(CacheFileFixedBlock::size(), headers_size_, _));
  bool update_success;
  cache_->updateHeaders(*lookup_context, response_headers, {time_system_.systemTime()},
                        [&update_success](bool success) { update_success = success; });
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus());
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(testHeaderBlock(0)));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<Buffer::InstancePtr>(
      absl::UnknownError("Intentionally failed to read headers block")));
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus()); // close read handle
  lookup_context->onDestroy();
  EXPECT_FALSE(update_success);
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, UpdateHeadersAbortsIfReadHeadersFindsAVaryEntry) {
  time_system_.advanceTimeWait(Seconds(3601));
  CacheFileFixedBlock vary_block;
  CacheFileHeader vary_headers;
  auto* vary_header = vary_headers.add_headers();
  vary_header->set_key("vary");
  vary_header->set_value("irrelevant");
  auto vary_headers_buffer = std::make_unique<Buffer::OwnedImpl>(bufferFromProto(vary_headers));
  vary_block.setHeadersSize(vary_headers_buffer->length());
  auto vary_block_buffer = std::make_unique<Buffer::OwnedImpl>();
  vary_block.serializeToBuffer(*vary_block_buffer);
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"x-whatever", "updated"},
      {"cache-control", "public,max-age=3600"},
  };
  auto lookup_context = testLookupContext();
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, _, _));
  EXPECT_CALL(*mock_async_file_manager_, unlink(_, _));
  EXPECT_CALL(*mock_async_file_handle_, read(0, CacheFileFixedBlock::size(), _));
  EXPECT_CALL(*mock_async_file_handle_,
              read(CacheFileFixedBlock::size(), vary_headers_buffer->length(), _));
  bool update_success;
  cache_->updateHeaders(*lookup_context, response_headers, {time_system_.systemTime()},
                        [&update_success](bool success) { update_success = success; });
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus());
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(std::move(vary_block_buffer)));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(std::move(vary_headers_buffer)));
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus()); // close read handle
  lookup_context->onDestroy();
  EXPECT_FALSE(update_success);
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, UpdateHeadersAbortsIfOpenForWriteFails) {
  time_system_.advanceTimeWait(Seconds(3601));
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"x-whatever", "updated"},
      {"cache-control", "public,max-age=3600"},
  };
  auto lookup_context = testLookupContext();
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, _, _));
  EXPECT_CALL(*mock_async_file_manager_, unlink(_, _));
  EXPECT_CALL(*mock_async_file_handle_, read(0, CacheFileFixedBlock::size(), _));
  EXPECT_CALL(*mock_async_file_handle_, read(CacheFileFixedBlock::size(), headers_size_, _));
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _));
  bool update_success;
  cache_->updateHeaders(*lookup_context, response_headers, {time_system_.systemTime()},
                        [&update_success](bool success) { update_success = success; });
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus());
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(testHeaderBlock(0)));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(testHeaderBuffer()));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<AsyncFileHandle>(
      absl::UnknownError("Intentionally failed to create file for write")));
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus()); // close read handle
  lookup_context->onDestroy();
  EXPECT_FALSE(update_success);
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, UpdateHeadersAbortsIfWriteHeaderBlockFails) {
  time_system_.advanceTimeWait(Seconds(3601));
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"x-whatever", "updated"},
      {"cache-control", "public,max-age=3600"},
  };
  auto lookup_context = testLookupContext();
  MockAsyncFileHandle write_handle =
      std::make_shared<StrictMock<MockAsyncFileContext>>(mock_async_file_manager_);
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, _, _));
  EXPECT_CALL(*mock_async_file_manager_, unlink(_, _));
  EXPECT_CALL(*mock_async_file_handle_, read(0, CacheFileFixedBlock::size(), _));
  EXPECT_CALL(*mock_async_file_handle_, read(CacheFileFixedBlock::size(), headers_size_, _));
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _));
  EXPECT_CALL(*write_handle, write(_, 0, _));
  bool update_success;
  cache_->updateHeaders(*lookup_context, response_headers, {time_system_.systemTime()},
                        [&update_success](bool success) { update_success = success; });
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus());
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(testHeaderBlock(0)));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(testHeaderBuffer()));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<AsyncFileHandle>(write_handle));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(absl::UnknownError("Intentionally failed to write header block")));
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus()); // close read handle
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus()); // close write handle
  lookup_context->onDestroy();
  EXPECT_FALSE(update_success);
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, UpdateHeadersAbortsIfReadBodyFails) {
  time_system_.advanceTimeWait(Seconds(3601));
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"x-whatever", "updated"},
      {"cache-control", "public,max-age=3600"},
  };
  size_t updated_headers_size = headerProtoSize(mergeProtoWithHeadersAndMetadata(
      testHeaderProto(), response_headers, {time_system_.systemTime()}));
  size_t body_size = 64;
  auto lookup_context = testLookupContext();
  MockAsyncFileHandle write_handle =
      std::make_shared<StrictMock<MockAsyncFileContext>>(mock_async_file_manager_);
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, _, _));
  EXPECT_CALL(*mock_async_file_manager_, unlink(_, _));
  EXPECT_CALL(*mock_async_file_handle_, read(0, CacheFileFixedBlock::size(), _));
  EXPECT_CALL(*mock_async_file_handle_, read(CacheFileFixedBlock::size(), headers_size_, _));
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _));
  EXPECT_CALL(*write_handle, write(_, 0, _));
  EXPECT_CALL(*mock_async_file_handle_,
              read(CacheFileFixedBlock::size() + headers_size_, body_size + trailers_size_, _));
  bool update_success;
  cache_->updateHeaders(*lookup_context, response_headers, {time_system_.systemTime()},
                        [&update_success](bool success) { update_success = success; });
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus());
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(testHeaderBlock(body_size)));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(testHeaderBuffer()));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<AsyncFileHandle>(write_handle));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(updated_headers_size + CacheFileFixedBlock::size()));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(absl::UnknownError("intentionally failed body read")));
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus()); // close read handle
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus()); // close write handle
  lookup_context->onDestroy();
  EXPECT_FALSE(update_success);
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, UpdateHeadersAbortsIfWriteBodyFails) {
  time_system_.advanceTimeWait(Seconds(3601));
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"x-whatever", "updated"},
      {"cache-control", "public,max-age=3600"},
  };
  size_t updated_headers_size = headerProtoSize(mergeProtoWithHeadersAndMetadata(
      testHeaderProto(), response_headers, {time_system_.systemTime()}));
  size_t body_size = 64;
  auto lookup_context = testLookupContext();
  MockAsyncFileHandle write_handle =
      std::make_shared<StrictMock<MockAsyncFileContext>>(mock_async_file_manager_);
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, _, _));
  EXPECT_CALL(*mock_async_file_manager_, unlink(_, _));
  EXPECT_CALL(*mock_async_file_handle_, read(0, CacheFileFixedBlock::size(), _));
  EXPECT_CALL(*mock_async_file_handle_, read(CacheFileFixedBlock::size(), headers_size_, _));
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _));
  EXPECT_CALL(*write_handle, write(_, 0, _));
  EXPECT_CALL(*mock_async_file_handle_,
              read(CacheFileFixedBlock::size() + headers_size_, body_size + trailers_size_, _));
  EXPECT_CALL(*write_handle, write(_, CacheFileFixedBlock::size() + updated_headers_size, _));
  bool update_success;
  cache_->updateHeaders(*lookup_context, response_headers, {time_system_.systemTime()},
                        [&update_success](bool success) { update_success = success; });
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus());
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(testHeaderBlock(body_size)));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(testHeaderBuffer()));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<AsyncFileHandle>(write_handle));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(updated_headers_size + CacheFileFixedBlock::size()));
  std::string body_and_trailers;
  body_and_trailers.resize(body_size + trailers_size_);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(std::make_unique<Buffer::OwnedImpl>(body_and_trailers)));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(absl::UnknownError("intentionally failed body write")));
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus()); // close read handle
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus()); // close write handle
  lookup_context->onDestroy();
  EXPECT_FALSE(update_success);
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, UpdateHeadersCopiesInChunksIfBodySizeIsLarge) {
  time_system_.advanceTimeWait(Seconds(3601));
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"x-whatever", "updated"},
      {"cache-control", "public,max-age=3600"},
  };
  size_t updated_headers_size = headerProtoSize(mergeProtoWithHeadersAndMetadata(
      testHeaderProto(), response_headers, {time_system_.systemTime()}));
  size_t body_size = FileSystemHttpCache::max_update_headers_copy_chunk_size_ + 1;
  auto lookup_context = testLookupContext();
  MockAsyncFileHandle write_handle =
      std::make_shared<StrictMock<MockAsyncFileContext>>(mock_async_file_manager_);
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, _, _));
  EXPECT_CALL(*mock_async_file_manager_, unlink(_, _));
  EXPECT_CALL(*mock_async_file_handle_, read(0, CacheFileFixedBlock::size(), _));
  EXPECT_CALL(*mock_async_file_handle_, read(CacheFileFixedBlock::size(), headers_size_, _));
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _));
  EXPECT_CALL(*write_handle, write(_, 0, _));
  EXPECT_CALL(*mock_async_file_handle_,
              read(CacheFileFixedBlock::size() + headers_size_,
                   FileSystemHttpCache::max_update_headers_copy_chunk_size_, _));
  EXPECT_CALL(*write_handle, write(_, CacheFileFixedBlock::size() + updated_headers_size, _));
  EXPECT_CALL(
      *mock_async_file_handle_,
      read(CacheFileFixedBlock::size() + headers_size_ +
               FileSystemHttpCache::max_update_headers_copy_chunk_size_,
           body_size + trailers_size_ - FileSystemHttpCache::max_update_headers_copy_chunk_size_,
           _));
  bool update_success;
  cache_->updateHeaders(*lookup_context, response_headers, {time_system_.systemTime()},
                        [&update_success](bool success) { update_success = success; });
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus());
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(testHeaderBlock(body_size)));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(testHeaderBuffer()));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<AsyncFileHandle>(write_handle));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(updated_headers_size + CacheFileFixedBlock::size()));
  std::string body_chunk;
  body_chunk.resize(FileSystemHttpCache::max_update_headers_copy_chunk_size_);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(std::make_unique<Buffer::OwnedImpl>(body_chunk)));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(FileSystemHttpCache::max_update_headers_copy_chunk_size_));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<Buffer::InstancePtr>(
      absl::UnknownError("intentionally failed second body read")));
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus()); // close read handle
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus()); // close write handle
  lookup_context->onDestroy();
  EXPECT_FALSE(update_success);
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, UpdateHeadersAbortsIfLinkFails) {
  time_system_.advanceTimeWait(Seconds(3601));
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"x-whatever", "updated"},
      {"cache-control", "public,max-age=3600"},
  };
  size_t updated_headers_size = headerProtoSize(mergeProtoWithHeadersAndMetadata(
      testHeaderProto(), response_headers, {time_system_.systemTime()}));
  size_t body_size = 64;
  auto lookup_context = testLookupContext();
  MockAsyncFileHandle write_handle =
      std::make_shared<StrictMock<MockAsyncFileContext>>(mock_async_file_manager_);
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, _, _));
  EXPECT_CALL(*mock_async_file_manager_, unlink(_, _));
  EXPECT_CALL(*mock_async_file_handle_, read(0, CacheFileFixedBlock::size(), _));
  EXPECT_CALL(*mock_async_file_handle_, read(CacheFileFixedBlock::size(), headers_size_, _));
  EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _));
  EXPECT_CALL(*write_handle, write(_, 0, _));
  EXPECT_CALL(*mock_async_file_handle_,
              read(CacheFileFixedBlock::size() + headers_size_, body_size + trailers_size_, _));
  EXPECT_CALL(*write_handle, write(_, CacheFileFixedBlock::size() + updated_headers_size, _));
  EXPECT_CALL(*write_handle, createHardLink(_, _));
  bool update_success;
  cache_->updateHeaders(*lookup_context, response_headers, {time_system_.systemTime()},
                        [&update_success](bool success) { update_success = success; });
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(mock_async_file_handle_));
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus());
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(testHeaderBlock(body_size)));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(testHeaderBuffer()));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<AsyncFileHandle>(write_handle));
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(updated_headers_size + CacheFileFixedBlock::size()));
  std::string body_and_trailers;
  body_and_trailers.resize(body_size + trailers_size_);
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<Buffer::InstancePtr>(std::make_unique<Buffer::OwnedImpl>(body_and_trailers)));
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>(body_and_trailers.size()));
  mock_async_file_manager_->nextActionCompletes(absl::UnknownError("intentionally failed to link"));
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus()); // close read handle
  mock_async_file_manager_->nextActionCompletes(absl::OkStatus()); // close write handle
  lookup_context->onDestroy();
  EXPECT_FALSE(update_success);
}

TEST_F(FileSystemHttpCacheTestWithMockFiles, UpdateHeadersAbortsEarlyIfCacheEntryIsInProgress) {
  auto lookup_context = testLookupContext();
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"},
      {"date", formatter_.fromTime(time_system_.systemTime())},
      {"x-whatever", "updated"},
      {"cache-control", "public,max-age=3600"},
  };
  EXPECT_CALL(*mock_async_file_manager_, openExistingFile(_, _, _));
  bool update_success;
  cache_->updateHeaders(*lookup_context, response_headers, {time_system_.systemTime()},
                        [&update_success](bool success) { update_success = success; });
  // A second updateHeaders call for the same resource while the first is still operating
  // should do nothing.
  auto lookup_context_2 = testLookupContext();
  cache_->updateHeaders(*lookup_context_2, response_headers, {time_system_.systemTime()},
                        [&update_success](bool success) { update_success = success; });
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<AsyncFileHandle>(absl::UnknownError("intentionally failed to open file")));
  lookup_context->onDestroy();
  lookup_context_2->onDestroy();
  // The file handle didn't actually get used in this test, but is expected to be closed.
  EXPECT_OK(mock_async_file_handle_->close([](absl::Status) {}));
}

// For the standard cache tests from http_cache_implementation_test_common.cc
// These will be run with the real file system, and therefore only cover the
// "no file errors" paths.
class FileSystemHttpCacheTestDelegate : public HttpCacheTestDelegate,
                                        public FileSystemCacheTestContext {
public:
  FileSystemHttpCacheTestDelegate() { initCache(); }
  std::shared_ptr<HttpCache> cache() override { return cache_; }
  bool validationEnabled() const override { return true; }
};

// For the standard cache tests from http_cache_implementation_test_common.cc
INSTANTIATE_TEST_SUITE_P(FileSystemHttpCacheTest, HttpCacheImplementationTest,
                         testing::Values(std::make_unique<FileSystemHttpCacheTestDelegate>),
                         [](const testing::TestParamInfo<HttpCacheImplementationTest::ParamType>&) {
                           return "FileSystemHttpCache";
                         });

TEST(Registration, GetCacheFromFactory) {
  HttpCacheFactory* factory = Registry::FactoryRegistry<HttpCacheFactory>::getFactoryByType(
      "envoy.extensions.http.cache.file_system_http_cache.v3.FileSystemHttpCacheConfig");
  ASSERT_NE(factory, nullptr);
  envoy::extensions::filters::http::cache::v3::CacheConfig cache_config;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  ON_CALL(factory_context.server_factory_context_.api_, threadFactory())
      .WillByDefault([]() -> Thread::ThreadFactory& { return Thread::threadFactoryForTest(); });
  TestUtility::loadFromYaml(std::string(yaml_config), cache_config);
  EXPECT_EQ(factory->getCache(cache_config, factory_context)->cacheInfo().name_,
            "envoy.extensions.http.cache.file_system_http_cache");
  // Verify that the config path got a / suffixed onto it.
  EXPECT_EQ(std::dynamic_pointer_cast<FileSystemHttpCache>(
                factory->getCache(cache_config, factory_context))
                ->config()
                .cache_path(),
            "/tmp/");
}

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
