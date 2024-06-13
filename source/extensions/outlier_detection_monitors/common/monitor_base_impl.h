#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/extensions/outlier_detection_monitors/common/v3/error_types.pb.h"
#include "envoy/extensions/outlier_detection_monitors/common/v3/error_types.pb.validate.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/upstream/outlier_detection.h"

#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Outlier {

using namespace Envoy::Upstream::Outlier;

// ErrorsBucket is used by outlier detection monitors and is used to
// "catch" reported Error (TypedError);
class ErrorsBucket {
public:
  virtual bool matchType(const ExtResult&) PURE;
  virtual bool match(const ExtResult&) PURE;
  virtual ~ErrorsBucket() {}
};

template <Upstream::Outlier::ExtResultType E> class TypedErrorsBucket : public ErrorsBucket {
public:
  bool matchType(const ExtResult& result) override { return (result.type() == E); }

  bool match(const ExtResult& result) override {
    return matches(static_cast<const TypedExtResult<E>&>(result));
  }

  virtual ~TypedErrorsBucket() {}

private:
  virtual bool matches(const TypedExtResult<E>&) const PURE;
};

using ErrorsBucketPtr = std::unique_ptr<ErrorsBucket>;

// Class defines a range of consecutive HTTP codes.
class HTTPCodesBucket : public TypedErrorsBucket<Upstream::Outlier::ExtResultType::HTTP_CODE> {
public:
  HTTPCodesBucket() = delete;
  HTTPCodesBucket(uint64_t start, uint64_t end) : start_(start), end_(end) {}
  bool matches(const TypedExtResult<Upstream::Outlier::ExtResultType::HTTP_CODE>&) const override;

  virtual ~HTTPCodesBucket() {}

private:
  uint64_t start_, end_;
};

// Class defines a "bucket" which catches LocalOriginEvent.
class LocalOriginEventsBucket
    : public TypedErrorsBucket<Upstream::Outlier::ExtResultType::LOCAL_ORIGIN> {
public:
  LocalOriginEventsBucket() = default;
  bool
  matches(const TypedExtResult<Upstream::Outlier::ExtResultType::LOCAL_ORIGIN>&) const override;
};

class ExtMonitorBase : public ExtMonitor {
public:
  ExtMonitorBase(const std::string& name, uint32_t enforce) : ExtMonitor(name, enforce) {}
  ExtMonitorBase() = delete;
  virtual ~ExtMonitorBase() {}
  void reportResult(const ExtResult&) override;

  void
  setCallback(std::function<void(uint32_t, std::string, absl::optional<std::string>)> callback) {
    callback_ = callback;
  }

  void reset() { onReset(); }
  std::string name() const { return name_; }

  void processBucketsConfig(
      const envoy::extensions::outlier_detection_monitors::common::v3::ErrorBuckets& config);
  void addErrorBucket(ErrorsBucketPtr&& bucket) { buckets_.push_back(std::move(bucket)); }

protected:
  std::vector<ErrorsBucketPtr> buckets_;
};

template <class ConfigProto>
class ExtMonitorFactoryBase : public Upstream::Outlier::ExtMonitorFactory {
public:
  ExtMonitorCreateFn createMonitor(const std::string& monitor_name,
                                   ProtobufTypes::MessagePtr&& config,
                                   std::shared_ptr<ExtMonitorFactoryContext> context) override {
    // This should throw exception if config is wrong. In the lambda below there is no need to
    // validate config again.
    Envoy::MessageUtil::downcastAndValidate<const ConfigProto&>(
        *config, context->messageValidationVisitor());
    // "convert" unique pointer to shared one.
    std::shared_ptr<Protobuf::Message> cfg(config.release());
    return [&monitor_name, cfg, context, this]() {
      return createMonitorFromProtoTyped(monitor_name, dynamic_cast<ConfigProto&>(*cfg), *context);
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  std::string name() const override { return name_; }

  ExtMonitorFactoryBase(const std::string& name) : name_(name) {}

private:
  virtual ExtMonitorPtr createMonitorFromProtoTyped(const std::string& monitor_name,
                                                    const ConfigProto& config,
                                                    ExtMonitorFactoryContext& context) PURE;

  const std::string name_;
};

} // namespace Outlier
} // namespace Extensions
} // namespace Envoy
