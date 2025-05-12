#include "source/common/http/date_provider_impl.h"

#include <chrono>
#include <string>

namespace Envoy {
namespace Http {
namespace {

class DateProviderDateFormatter : public DateFormatter {
public:
  DateProviderDateFormatter() : DateFormatter("%a, %d %b %Y %H:%M:%S GMT") {}
};
using DateProviderDateFormatterSingleton = ConstSingleton<DateProviderDateFormatter>;
} // namespace

TlsCachingDateProviderImpl::TlsCachingDateProviderImpl(Event::Dispatcher& dispatcher,
                                                       ThreadLocal::SlotAllocator& tls)
    : DateProviderImplBase(dispatcher.timeSource()), tls_(tls.allocateSlot()),
      refresh_timer_(dispatcher.createTimer([this]() -> void { onRefreshDate(); })) {

  onRefreshDate();
}

void TlsCachingDateProviderImpl::onRefreshDate() {
  std::string new_date_string = DateProviderDateFormatterSingleton::get().now(time_source_);
  tls_->set([new_date_string](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::make_shared<ThreadLocalCachedDate>(new_date_string);
  });

  refresh_timer_->enableTimer(std::chrono::milliseconds(500));
}

void TlsCachingDateProviderImpl::setDateHeader(ResponseHeaderMap& headers) {
  headers.setDate(tls_->getTyped<ThreadLocalCachedDate>().date_string_);
}

void SlowDateProviderImpl::setDateHeader(ResponseHeaderMap& headers) {
  headers.setDate(DateProviderDateFormatterSingleton::get().now(time_source_));
}

} // namespace Http
} // namespace Envoy
