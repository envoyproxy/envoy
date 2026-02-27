#include "source/extensions/filters/http/file_server/file_streamer.h"

#include "envoy/http/codes.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/http/header_map_impl.h"
#include "source/extensions/common/async_files/async_file_manager.h"
#include "source/extensions/filters/http/file_server/absl_status_to_http_status.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FileServer {

namespace {

const Http::LowerCaseString& acceptRangesHeaderKey() {
  CONSTRUCT_ON_FIRST_USE(Http::LowerCaseString, "accept-ranges");
}

} // namespace

void FileStreamer::begin(const FileServerConfig& config, Event::Dispatcher& dispatcher,
                         uint64_t start, uint64_t end, std::filesystem::path file_path) {
  ASSERT(config.asyncFileManager() != nullptr);
  file_server_config_ = &config;
  dispatcher_ = &dispatcher;
  pos_ = start;
  end_ = end;
  file_path_ = std::move(file_path);
  cancel_callback_ = file_server_config_->asyncFileManager()->stat(
      dispatcher_, file_path_.string(), [this](absl::StatusOr<struct stat> result) {
        if (!result.ok()) {
          client_.errorFromFile(abslStatusToHttpStatus(result.status().code()),
                                absl::StrCat("file_server_stat_error"));
          return;
        }
        const struct stat& s = result.value();
        if (S_ISDIR(s.st_mode)) {
          startDir(0);
          return;
        }
        cancel_callback_ = file_server_config_->asyncFileManager()->openExistingFile(
            dispatcher_, file_path_.string(), Common::AsyncFiles::AsyncFileManager::Mode::ReadOnly,
            [this](absl::StatusOr<AsyncFileHandle> result) {
              if (!result.ok()) {
                client_.errorFromFile(abslStatusToHttpStatus(result.status().code()),
                                      absl::StrCat("file_server_open_error"));
                return;
              }
              async_file_ = std::move(result.value());
              startFile();
            });
      });
}

void FileStreamer::startDir(int behavior_index) {
  OptRef<const ProtoFileServerConfig::DirectoryBehavior> behavior =
      file_server_config_->directoryBehavior(behavior_index);
  if (!behavior) {
    client_.errorFromFile(Http::Code::Forbidden, "file_server_no_valid_directory_behavior");
    return;
  }
  if (!behavior->default_file().empty()) {
    cancel_callback_ = file_server_config_->asyncFileManager()->openExistingFile(
        dispatcher_, (file_path_ / std::filesystem::path{behavior->default_file()}).string(),
        Common::AsyncFiles::AsyncFileManager::Mode::ReadOnly,
        [this, behavior_index](absl::StatusOr<AsyncFileHandle> result) {
          if (!result.ok()) {
            // Try the next directoryBehavior.
            // Since the action is dispatched, this isn't recursion.
            return startDir(behavior_index + 1);
          }
          file_path_ = file_path_ /
                       std::filesystem::path{
                           file_server_config_->directoryBehavior(behavior_index)->default_file()};
          async_file_ = std::move(result.value());
          startFile();
        });
    return;
  } else if (behavior->has_list()) {
    client_.errorFromFile(Http::Code::Forbidden, "file_server_list_not_implemented");
    return;
  } else {
    // Normally unreachable due to proto validations.
    client_.errorFromFile(Http::Code::InternalServerError, "file_server_empty_behavior_type");
    return;
  }
}

void FileStreamer::startFile() {
  ASSERT(async_file_);
  auto queued = async_file_->stat(dispatcher_, [this](absl::StatusOr<struct stat> result) {
    if (!result.ok()) {
      client_.errorFromFile(abslStatusToHttpStatus(result.status().code()),
                            "file_server_opened_file_stat_failed");
      return;
    }
    const struct stat& s = result.value();
    if (static_cast<uint64_t>(s.st_size) < end_ || static_cast<uint64_t>(s.st_size) < pos_ ||
        (end_ != 0 && end_ < pos_)) {
      client_.errorFromFile(Http::Code::RangeNotSatisfiable, "file_server_range_not_satisfiable");
      return;
    }
    auto headers = Http::ResponseHeaderMapImpl::create();
    headers->setReference(acceptRangesHeaderKey(), "bytes");
    if (pos_ || end_) {
      // Range request gets PartialContent, a content-range, and reduced content-length header.
      if (!end_) {
        end_ = s.st_size;
      }
      headers->setContentLength(end_ - pos_);
      // Subtract one from end_ in this header because range headers use [start, end) vs.
      // end_ is in normal programmer [start, end] style.
      headers->setReferenceKey(Envoy::Http::Headers::get().ContentRange,
                               absl::StrCat("bytes ", pos_, "-", end_ - 1, "/", s.st_size));
      headers->setStatus(enumToInt(Http::Code::PartialContent));
    } else {
      end_ = s.st_size;
      headers->setContentLength(s.st_size);
      headers->setStatus(enumToInt(Http::Code::OK));
    }
    absl::string_view ct = file_server_config_->contentTypeForPath(file_path_);
    if (!ct.empty()) {
      headers->setContentType(ct);
    }
    if (client_.headersFromFile(std::move(headers))) {
      readBodyChunk();
    }
  });
  ASSERT(queued.ok());
  cancel_callback_ = std::move(queued.value());
}

void FileStreamer::pause() { paused_ = true; }

void FileStreamer::unpause() {
  if (paused_) {
    paused_ = false;
    if (action_has_been_postponed_by_pause_) {
      action_has_been_postponed_by_pause_ = false;
      readBodyChunk();
    }
  }
}

void FileStreamer::readBodyChunk() {
  ASSERT(async_file_);
  static const uint64_t kMaxReadSize = 32 * 1024;
  uint64_t sz = std::min(end_ - pos_, kMaxReadSize);
  auto queued =
      async_file_->read(dispatcher_, pos_, sz, [this](absl::StatusOr<Buffer::InstancePtr> result) {
        if (!result.ok()) {
          client_.errorFromFile(abslStatusToHttpStatus(result.status().code()),
                                "file_server_read_operation_failed");
          return;
        }
        Buffer::InstancePtr buf = std::move(result.value());
        pos_ += buf->length();
        client_.bodyChunkFromFile(std::move(buf), pos_ == end_);
        if (!paused_ && pos_ != end_) {
          readBodyChunk();
        } else if (paused_) {
          action_has_been_postponed_by_pause_ = true;
        }
      });
  ASSERT(queued.ok());
  cancel_callback_ = std::move(queued.value());
}

void FileStreamer::abort() { cancel_callback_(); }

FileStreamer::~FileStreamer() {
  if (async_file_) {
    async_file_->close(nullptr, [](absl::Status) {}).IgnoreError();
  }
}

} // namespace FileServer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
