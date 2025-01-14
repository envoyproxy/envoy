#include "source/extensions/filters/http/cache/upstream_request_impl.h"

#include "range_utils.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

HttpSourcePtr UpstreamRequestImplFactory::create(Http::RequestHeaderMap& request_headers) {
  // Can't use make_unique because the constructor is private.
  auto ret =
      std::unique_ptr<UpstreamRequestImpl>(new UpstreamRequestImpl(async_client_, stream_options_));
  ret->postHeaders(dispatcher_, request_headers);
  return ret;
}

UpstreamRequestImpl::UpstreamRequestImpl(Http::AsyncClient& async_client,
                                         const Http::AsyncClient::StreamOptions& options)
    : stream_(async_client.start(*this, options)),
      body_buffer_([this]() { onBelowLowWatermark(); }, [this]() { onAboveHighWatermark(); },
                   nullptr) {
  ASSERT(stream_ != nullptr);
  body_buffer_.setWatermarks(options.buffer_limit_.value_or(0));
}

void UpstreamRequestImpl::onAboveHighWatermark() {
  // TODO(ravenblack): currently AsyncRequest::Stream does not support pausing.
  // Waiting on issue #33319
}

void UpstreamRequestImpl::onBelowLowWatermark() {
  // TODO(ravenblack): currently AsyncRequest::Stream does not support pausing.
  // Waiting on issue #33319
}

void UpstreamRequestImpl::getHeaders(GetHeadersCallback&& cb) {
  ASSERT(callbackEmpty());
  if (!stream_ && !end_stream_after_headers_ && !end_stream_after_body_ && !trailers_) {
    return cb(nullptr, EndStream::Reset);
  }
  callback_ = std::move(cb);
  return maybeDeliverHeaders();
}

void UpstreamRequestImpl::onHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) {
  headers_ = std::move(headers);
  end_stream_after_headers_ = end_stream;
  if (end_stream) {
    stream_ = nullptr;
  }
  return maybeDeliverHeaders();
}

void UpstreamRequestImpl::maybeDeliverHeaders() {
  if (!absl::holds_alternative<GetHeadersCallback>(callback_) || !headers_) {
    return;
  }
  return absl::get<GetHeadersCallback>(consumeCallback())(
      std::move(headers_), end_stream_after_headers_ ? EndStream::End : EndStream::More);
}

void UpstreamRequestImpl::getBody(AdjustedByteRange range, GetBodyCallback&& cb) {
  ASSERT(callbackEmpty());
  ASSERT(range.begin() == stream_pos_, "UpstreamRequest does not support out of order reads");
  ASSERT(!end_stream_after_headers_);
  if (!stream_ && !end_stream_after_body_ && !trailers_) {
    return cb(nullptr, EndStream::Reset);
  }
  requested_body_range_ = std::move(range);
  callback_ = std::move(cb);
  return maybeDeliverBody();
}

void UpstreamRequestImpl::onData(Buffer::Instance& data, bool end_stream) {
  end_stream_after_body_ = end_stream;
  if (end_stream) {
    stream_ = nullptr;
  }
  body_buffer_.move(data);
  return maybeDeliverBody();
}

void UpstreamRequestImpl::maybeDeliverBody() {
  if (!absl::holds_alternative<GetBodyCallback>(callback_)) {
    return;
  }
  uint64_t len = std::min(requested_body_range_.length(), body_buffer_.length());
  if (len == 0) {
    if (trailers_) {
      // If we've already seen trailers from upstream and there's no more buffered
      // body, but the client is still requesting body, it means the client didn't
      // know how much body to expect. A null body with end_stream=false informs the
      // client to move on to requesting trailers.
      return absl::get<GetBodyCallback>(consumeCallback())(nullptr, EndStream::More);
    }
    if (end_stream_after_body_) {
      // If we already reached the end of message and are still requesting more
      // body, a null buffer indicates the body ended.
      return absl::get<GetBodyCallback>(consumeCallback())(nullptr, EndStream::End);
    }
    // If we have no body or end but have requested some body, that means we're
    // just waiting for it to arrive, and maybeDeliverBody will be called again
    // when that happens.
    return;
  }
  auto fragment = std::make_unique<Buffer::OwnedImpl>();
  fragment->move(body_buffer_, len);
  stream_pos_ += len;
  bool end_stream = end_stream_after_body_ && body_buffer_.length() == 0;
  return absl::get<GetBodyCallback>(consumeCallback())(
      std::move(fragment), end_stream ? EndStream::End : EndStream::More);
}

void UpstreamRequestImpl::getTrailers(GetTrailersCallback&& cb) {
  ASSERT(callbackEmpty());
  ASSERT(!end_stream_after_headers_ && !end_stream_after_body_);
  if (!stream_ && !trailers_) {
    return cb(nullptr, EndStream::Reset);
  }
  callback_ = std::move(cb);
  return maybeDeliverTrailers();
}

void UpstreamRequestImpl::onTrailers(Http::ResponseTrailerMapPtr&& trailers) {
  trailers_ = std::move(trailers);
  stream_ = nullptr;
  return maybeDeliverTrailers();
}

void UpstreamRequestImpl::maybeDeliverTrailers() {
  if (!absl::holds_alternative<GetTrailersCallback>(callback_) || !trailers_) {
    if (body_buffer_.length() == 0 && absl::holds_alternative<GetBodyCallback>(callback_)) {
      // If we received trailers while requesting body it means that we didn't
      // know how much body to request, or the upstream returned less body than
      // expected by surprise - a null body response informs the client to
      // request trailers instead.
      return absl::get<GetBodyCallback>(consumeCallback())(nullptr, EndStream::More);
    }
    return;
  }
  return absl::get<GetTrailersCallback>(consumeCallback())(std::move(trailers_), EndStream::End);
}

UpstreamRequestImpl::~UpstreamRequestImpl() {
  // Cancel in-flight callbacks on destroy.
  callback_ = absl::monostate{};
  if (stream_) {
    // Resets the stream and calls onReset, guaranteeing no further callbacks.
    stream_->reset();
  }
}

void UpstreamRequestImpl::postHeaders(Event::Dispatcher& dispatcher,
                                      Http::RequestHeaderMap& request_headers) {
  // UpstreamRequest must take a copy of the headers as the upstream request may
  // still use the reference provided to it after the original reference has moved.
  request_headers_ = Http::createHeaderMap<Http::RequestHeaderMapImpl>(request_headers);
  dispatcher.post([this]() {
    // If this request had a body or trailers, CacheFilter::decodeHeaders
    // would have bypassed cache lookup and insertion, so this class wouldn't
    // be instantiated. So end_stream will always be true.
    stream_->sendHeaders(*request_headers_, /*end_stream=*/true);
    absl::optional<absl::string_view> range_header = RangeUtils::getRangeHeader(*request_headers_);
    if (range_header) {
      absl::optional<std::vector<RawByteRange>> ranges =
          RangeUtils::parseRangeHeader(range_header.value(), 1);
      if (ranges) {
        stream_pos_ = ranges.value().front().firstBytePos();
      }
    }
  });
}

template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

void UpstreamRequestImpl::onReset() {
  stream_ = nullptr;
  absl::visit(overloaded{
                  [](absl::monostate&&) {},
                  [](GetHeadersCallback&& cb) { cb(nullptr, EndStream::Reset); },
                  [](GetBodyCallback&& cb) { cb(nullptr, EndStream::Reset); },
                  [](GetTrailersCallback&& cb) { cb(nullptr, EndStream::Reset); },
              },
              consumeCallback());
}

void UpstreamRequestImpl::onComplete() { stream_ = nullptr; }

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
