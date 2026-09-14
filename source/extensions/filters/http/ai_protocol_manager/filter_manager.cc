#include "source/extensions/filters/http/ai_protocol_manager/filter_manager.h"

#include <utility>
#include <vector>

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

FilterManager::FilterManager(std::vector<AiFilterSharedPtr> filters)
    : filters_(std::move(filters)) {}

FilterManager::FilterManager(std::vector<AiFilterSharedPtr> filters, JsonWithExtBuf payload_index,
                             BufferManager* buffer_manager, Event::Dispatcher& dispatcher,
                             StreamInfo::StreamInfo& stream_info,
                             Http::RequestHeaderMap* request_headers, LocalReplyFn local_reply_fn)
    : filters_(std::move(filters)),
      request_manager_(std::make_unique<RequestFilterManager>(
          filters_, std::move(payload_index), buffer_manager, dispatcher, stream_info,
          request_headers, std::move(local_reply_fn))) {}

FilterManager::~FilterManager() { cancel(); }

void FilterManager::start(OnCompleteFn on_complete) {
  ASSERT(request_manager_ != nullptr);
  request_manager_->start(std::move(on_complete));
}

void FilterManager::startRequest(JsonWithExtBuf payload_index, BufferManager* buffer_manager,
                                 Event::Dispatcher& dispatcher, StreamInfo::StreamInfo& stream_info,
                                 Http::RequestHeaderMap* request_headers,
                                 LocalReplyFn local_reply_fn, OnCompleteFn on_complete) {
  ASSERT(request_manager_ == nullptr);
  request_manager_ = std::make_unique<RequestFilterManager>(
      filters_, std::move(payload_index), buffer_manager, dispatcher, stream_info, request_headers,
      std::move(local_reply_fn));
  request_manager_->start(std::move(on_complete));
}

void FilterManager::startSseResponse(ExternalBufferFactory& buffer_factory,
                                     FilterChainBridge& bridge, BufferManager& out_buffer_manager,
                                     OnCompleteFn on_complete,
                                     ResponseFilterManager::Config config) {
  ASSERT(response_manager_ == nullptr);
  std::vector<AiFilterSharedPtr> reversed(filters_.rbegin(), filters_.rend());
  response_manager_ =
      std::make_unique<ResponseFilterManager>(std::move(reversed), buffer_factory, bridge,
                                              out_buffer_manager, std::move(on_complete), config);
}

void FilterManager::onResponseData(Buffer::Instance& data, bool end_stream) {
  ASSERT(response_manager_ != nullptr);
  response_manager_->onData(data, end_stream);
}

void FilterManager::cancel() {
  if (request_manager_ != nullptr) {
    request_manager_->cancel();
  }
  if (response_manager_ != nullptr) {
    response_manager_->cancel();
  }
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
