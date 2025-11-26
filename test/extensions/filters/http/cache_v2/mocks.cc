#include "test/extensions/filters/http/cache_v2/mocks.h"

#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {

void PrintTo(const EndStream& end_stream, std::ostream* os) {
  static const absl::flat_hash_map<EndStream, absl::string_view> vmap{
      {EndStream::End, "End"},
      {EndStream::More, "More"},
      {EndStream::Reset, "Reset"},
  };
  *os << "EndStream::" << vmap.at(end_stream);
}

void PrintTo(const Key& key, std::ostream* os) { *os << key.DebugString(); }

using testing::NotNull;

FakeStreamHttpSource::FakeStreamHttpSource(Event::Dispatcher& dispatcher,
                                           Http::ResponseHeaderMapPtr headers,
                                           absl::string_view body,
                                           Http::ResponseTrailerMapPtr trailers)
    : dispatcher_(dispatcher), headers_(std::move(headers)), body_(body),
      trailers_(std::move(trailers)) {}

void FakeStreamHttpSource::getHeaders(GetHeadersCallback&& cb) {
  ASSERT_THAT(headers_, NotNull());
  EndStream end_stream = (!body_.empty() || trailers_) ? EndStream::More : EndStream::End;
  dispatcher_.post([headers = std::move(headers_), cb = std::move(cb), end_stream]() mutable {
    cb(std::move(headers), end_stream);
  });
}

void FakeStreamHttpSource::getBody(AdjustedByteRange range, GetBodyCallback&& cb) {
  if (body_.empty()) {
    cb(nullptr, trailers_ ? EndStream::More : EndStream::End);
  } else {
    if (range.length() > max_fragment_size_) {
      range = AdjustedByteRange(range.begin(), range.begin() + max_fragment_size_);
    }
    ASSERT_THAT(range.begin(), testing::Ge(body_pos_))
        << "getBody called out of order, pos=" << body_pos_ << ", range=[" << range.begin() << ", "
        << range.end() << ")";
    if (range.begin() == body_.size()) {
      cb(nullptr, trailers_ ? EndStream::More : EndStream::End);
    } else {
      range = AdjustedByteRange(range.begin(), std::min(range.end(), body_.size()));
      EndStream end_stream =
          (trailers_ || range.end() < body_.size()) ? EndStream::More : EndStream::End;
      Buffer::InstancePtr fragment = std::make_unique<Buffer::OwnedImpl>(
          absl::string_view{body_}.substr(range.begin(), range.length()));
      dispatcher_.post([cb = std::move(cb), fragment = std::move(fragment), end_stream]() mutable {
        cb(std::move(fragment), end_stream);
      });
      body_pos_ = range.end();
    }
  }
}

void FakeStreamHttpSource::getTrailers(GetTrailersCallback&& cb) {
  ASSERT_THAT(trailers_, NotNull())
      << "should have stopped on an earlier EndStream::End not called getTrailers";
  cb(std::move(trailers_), EndStream::End);
}

} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
