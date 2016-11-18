#include "filter_manager_impl.h"

#include "envoy/network/connection.h"

#include "common/common/assert.h"

namespace Network {

void FilterManagerImpl::addWriteFilter(WriteFilterPtr filter) {
  ASSERT(connection_.state() == Connection::State::Open);
  downstream_filters_.emplace_back(filter);
}

void FilterManagerImpl::addFilter(FilterPtr filter) {
  addReadFilter(filter);
  addWriteFilter(filter);
}

void FilterManagerImpl::addReadFilter(ReadFilterPtr filter) {
  ASSERT(connection_.state() == Connection::State::Open);
  ActiveReadFilterPtr new_filter(new ActiveReadFilter{*this, filter});
  filter->initializeReadFilterCallbacks(*new_filter);
  new_filter->moveIntoListBack(std::move(new_filter), upstream_filters_);
}

void FilterManagerImpl::destroyFilters() {
  upstream_filters_.clear();
  downstream_filters_.clear();
}

void FilterManagerImpl::initializeReadFilters() {
  ASSERT(!upstream_filters_.empty());
  onContinueReading(nullptr);
}

void FilterManagerImpl::onContinueReading(ActiveReadFilter* filter) {
  std::list<ActiveReadFilterPtr>::iterator entry;
  if (!filter) {
    entry = upstream_filters_.begin();
  } else {
    entry = std::next(filter->entry());
  }

  for (; entry != upstream_filters_.end(); entry++) {
    if (!(*entry)->initialized_) {
      (*entry)->initialized_ = true;
      FilterStatus status = (*entry)->filter_->onNewConnection();
      if (status == FilterStatus::StopIteration) {
        return;
      }
    }

    Buffer::Instance& read_buffer = buffer_source_.getReadBuffer();
    if (read_buffer.length() > 0) {
      FilterStatus status = (*entry)->filter_->onData(read_buffer);
      if (status == FilterStatus::StopIteration) {
        return;
      }
    }
  }
}

void FilterManagerImpl::onRead() {
  ASSERT(!upstream_filters_.empty());
  onContinueReading(nullptr);
}

FilterStatus FilterManagerImpl::onWrite() {
  for (const WriteFilterPtr& filter : downstream_filters_) {
    FilterStatus status = filter->onWrite(buffer_source_.getWriteBuffer());
    if (status == FilterStatus::StopIteration) {
      return status;
    }
  }

  return FilterStatus::Continue;
}

} // Network
