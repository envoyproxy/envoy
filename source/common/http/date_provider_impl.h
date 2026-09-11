#pragma once

#include <cstdint>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/singleton/instance.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/utility.h"
#include "source/common/http/date_provider.h"

namespace Envoy {
namespace Http {

/**
 * Base for all providers.
 */
class DateProviderImplBase : public DateProvider {
public:
  explicit DateProviderImplBase(TimeSource& time_source) : time_source_(time_source) {}

protected:
  TimeSource& time_source_;
};

/**
 * A caching thread local provider. Each thread updates its date string every 500ms using its own
 * dispatcher, so refresh callbacks cannot accumulate on workers that have not started.
 */
class TlsCachingDateProviderImpl : public DateProviderImplBase, public Singleton::Instance {
public:
  TlsCachingDateProviderImpl(Event::Dispatcher& dispatcher, ThreadLocal::SlotAllocator& tls);

  // Http::DateProvider
  void setDateHeader(ResponseHeaderMap& headers) override;

private:
  struct ThreadLocalCachedDate : public ThreadLocal::ThreadLocalObject {
    explicit ThreadLocalCachedDate(Event::Dispatcher& dispatcher);

    void onRefreshDate();

    TimeSource& time_source_;
    std::string date_string_;
    Event::TimerPtr refresh_timer_;
  };

  ThreadLocal::SlotSharedPtr tls_;
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
