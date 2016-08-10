#include "filter_manager.h"

#include "common/common/assert.h"

namespace Network {

void FilterManager::addWriteFilter(WriteFilterPtr filter) {
  downstream_filters_.emplace_back(filter);
}

void FilterManager::addFilter(FilterPtr filter) {
  addReadFilter(filter);
  addWriteFilter(filter);
}

void FilterManager::addReadFilter(ReadFilterPtr filter) {
  ActiveReadFilterPtr new_filter(new ActiveReadFilter{*this, filter});
  filter->initializeReadFilterCallbacks(*new_filter);
  new_filter->moveIntoListBack(std::move(new_filter), upstream_filters_);
}

void FilterManager::destroyFilters() {
  upstream_filters_.clear();
  downstream_filters_.clear();
}

void FilterManager::onContinueReading(ActiveReadFilter* filter) {
  std::list<ActiveReadFilterPtr>::iterator entry;
  if (!filter) {
    entry = upstream_filters_.begin();
  } else {
    entry = std::next(filter->entry());
  }

  for (; entry != upstream_filters_.end(); entry++) {
    FilterStatus status = (*entry)->filter_->onData(buffer_source_.getReadBuffer());
    if (status == FilterStatus::StopIteration) {
      return;
    }
  }
}

void FilterManager::onRead() {
  ASSERT(!upstream_filters_.empty());
  onContinueReading(nullptr);
}

FilterStatus FilterManager::onWrite() {
  for (const WriteFilterPtr& filter : downstream_filters_) {
    FilterStatus status = filter->onWrite(buffer_source_.getWriteBuffer());
    if (status == FilterStatus::StopIteration) {
      return status;
    }
  }

  return FilterStatus::Continue;
}

} // Network
