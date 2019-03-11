#include "init/callback.h"

namespace Envoy {
namespace Init {

Caller::Caller(Common::Callback::Caller<> caller, absl::string_view name,
               absl::string_view receiver_name)
    : caller_(std::move(caller)), name_(name), receiver_name_(receiver_name) {}

Caller::operator bool() const { return caller_; }

void Caller::reset() { caller_.reset(); }

void Caller::operator()() const {
  if (caller_) {
    ENVOY_LOG(debug, "{} initialized, notifying {}", name_, receiver_name_);
    caller_();
  } else {
    ENVOY_LOG(debug, "{} initialized, but can't notify {} (unavailable)", name_, receiver_name_);
  }
}

Receiver::Receiver(absl::string_view name, std::function<void()> fn) : name_(name), receiver_(fn) {}

Receiver::~Receiver() {
  if (receiver_) {
    ENVOY_LOG(debug, "{} destroyed", name_);
  }
}

Caller Receiver::caller(absl::string_view name) const {
  return Caller(receiver_.caller(), name, name_);
}

void Receiver::reset() {
  if (receiver_) {
    ENVOY_LOG(debug, "{} reset", name_);
    name_.clear();
    receiver_.reset();
  }
}

TargetCaller::TargetCaller(Common::Callback::Caller<Caller> caller, absl::string_view name,
                           absl::string_view receiver_name)
    : caller_(std::move(caller)), name_(name), receiver_name_(receiver_name) {}

TargetCaller::operator bool() const { return caller_; }

void TargetCaller::operator()(const Receiver& receiver) const {
  if (caller_) {
    ENVOY_LOG(debug, "{} initializing {}", name_, receiver_name_);
    caller_(receiver.caller(receiver_name_));
  } else {
    ENVOY_LOG(debug, "{} can't initialize {} (unavailable)", name_, receiver_name_);
  }
}

TargetReceiver::TargetReceiver(absl::string_view name, std::function<void(Caller)> fn)
    : name_(fmt::format("target {}", name)), receiver_(fn) {}

TargetReceiver::~TargetReceiver() {
  if (receiver_) {
    ENVOY_LOG(debug, "{} destroyed", name_);
  }
}

TargetCaller TargetReceiver::caller(absl::string_view name) const {
  return TargetCaller(receiver_.caller(), name, name_);
}

void TargetReceiver::reset() {
  if (receiver_) {
    ENVOY_LOG(debug, "{} reset", name_);
    name_.clear();
    receiver_.reset();
  }
}

absl::string_view TargetReceiver::name() const { return name_; }

} // namespace Init
} // namespace Envoy
