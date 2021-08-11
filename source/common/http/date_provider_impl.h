#pragma once

#include <cstdint>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/singleton/instance.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/utility.h"

#include "date_provider.h"

namespace Envoy {
namespace Http {

/**
 * Base for all providers.
 */
class DateProviderImplBase : public DateProvider {
public:
  explicit DateProviderImplBase(TimeSource& time_source) : time_source_(time_source) {}

protected:
  static DateFormatter date_formatter_;
  TimeSource& time_source_;
};

/**
 * A caching thread local provider. This implementation updates the date string every 500ms and
 * caches on each thread.
 */
class TlsCachingDateProviderImpl : public DateProviderImplBase, public Singleton::Instance {
public:
  TlsCachingDateProviderImpl(Event::Dispatcher& dispatcher, ThreadLocal::SlotAllocator& tls);

  // Http::DateProvider
  void setDateHeader(ResponseHeaderMap& headers) override;

private:
  struct ThreadLocalCachedDate : public ThreadLocal::ThreadLocalObject {
    ThreadLocalCachedDate(const std::string& date_string) : date_string_(date_string) {}

    const std::string date_string_;
  };

  void onRefreshDate();

  ThreadLocal::SlotPtr tls_;
  Event::TimerPtr refresh_timer_;
};

/**
 * A basic provider that just creates the date string every time.
 */
class SlowDateProviderImpl : public DateProviderImplBase {
  using DateProviderImplBase::DateProviderImplBase;

public:
  // Http::DateProvider
  void setDateHeader(ResponseHeaderMap& headers) override;
};

} // namespace Http
} // namespace Envoy
