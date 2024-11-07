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
  virtual bool matchType(const ExtResult&) const PURE;
  virtual bool match(const ExtResult&) const PURE;
  virtual ~ErrorsBucket() {}
};

template <class E> class TypedErrorsBucket : public ErrorsBucket {
public:
  bool matchType(const ExtResult& result) const override {
    return absl::holds_alternative<E>(result);
  }

  virtual ~TypedErrorsBucket() {}
};

using ErrorsBucketPtr = std::unique_ptr<ErrorsBucket>;

// Class defines a range of consecutive HTTP codes.
class HTTPCodesBucket : public TypedErrorsBucket<Upstream::Outlier::HttpCode> {
public:
  HTTPCodesBucket() = delete;
  HTTPCodesBucket(uint64_t start, uint64_t end) : start_(start), end_(end) {}
  bool match(const ExtResult&) const override;

  virtual ~HTTPCodesBucket() {}

private:
  uint64_t start_, end_;
};

// Class defines a "bucket" which catches LocalOriginEvent.
class LocalOriginEventsBucket : public TypedErrorsBucket<Upstream::Outlier::LocalOriginEvent> {
public:
  LocalOriginEventsBucket() = default;
  bool match(const ExtResult&) const override;
};

/*
    Base config for all types of monitors.
*/
class ExtMonitorConfig {
public:
  ExtMonitorConfig(const std::string& name, uint32_t enforce) : name_(name), enforce_(enforce) {}
  void processBucketsConfig(
      const envoy::extensions::outlier_detection_monitors::common::v3::ErrorBuckets& config);
  void addErrorBucket(ErrorsBucketPtr&& bucket) { buckets_.push_back(std::move(bucket)); }

  uint32_t enforce() const { return enforce_; }
  const std::string& name() const { return name_; }
  const std::vector<ErrorsBucketPtr>& buckets() const { return buckets_; }

private:
  std::string name_;
  uint32_t enforce_{100};
  std::vector<ErrorsBucketPtr> buckets_;
};

using ExtMonitorConfigSharedPtr = std::shared_ptr<ExtMonitorConfig>;

class ExtMonitorBase : public ExtMonitor {
public:
  ExtMonitorBase(ExtMonitorConfigSharedPtr config)
      : config_(std::move(config)),
        enforce_runtime_key_("outlier_detection.enforcing_extension." + config_->name()) {}
  ExtMonitorBase() = delete;
  virtual ~ExtMonitorBase() {}
  void putResult(const ExtResult) override;

  void setExtMonitorCallback(ExtMonitorCallback callback) override { callback_ = callback; }

  void reset() override { onReset(); }
  uint32_t enforce() const override { return config_->enforce(); }
  absl::string_view name() const override { return config_->name(); }
  absl::string_view enforceRuntimeKey() const override { return enforce_runtime_key_; }

  const ExtMonitorConfigSharedPtr& config() const { return config_; }

protected:
  virtual bool onError() PURE;
  virtual void onSuccess() PURE;
  virtual void onReset() PURE;

  ExtMonitor::ExtMonitorCallback callback_;
  ExtMonitorConfigSharedPtr config_;
  std::string enforce_runtime_key_;
};

template <class ConfigProto>
class ExtMonitorFactoryBase : public Upstream::Outlier::ExtMonitorFactory {
public:
  ExtMonitorCreateFn getCreateMonitorCallback(const std::string& monitor_name,
                                              const Protobuf::Message& config,
                                              ExtMonitorFactoryContext& context) override {
    // This should throw exception if config is wrong.
    return createMonitorFromProtoTyped(monitor_name,
                                       Envoy::MessageUtil::downcastAndValidate<const ConfigProto&>(
                                           config, context.messageValidationVisitor()),
                                       context);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  std::string name() const override { return name_; }

  ExtMonitorFactoryBase(const std::string& name) : name_(name) {}

private:
  virtual ExtMonitorCreateFn createMonitorFromProtoTyped(const std::string& monitor_name,
                                                         const ConfigProto& config,
                                                         ExtMonitorFactoryContext& context) PURE;

  const std::string name_;
};

} // namespace Outlier
} // namespace Extensions
} // namespace Envoy
