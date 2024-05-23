#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/extensions/outlier_detection_monitors/common/v3/error_types.pb.h"
#include "envoy/extensions/outlier_detection_monitors/common/v3/error_types.pb.validate.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/upstream/outlier_detection.h"

#include "source/common/protobuf/utility.h"

namespace Envoy {

// Types of errors reported to outlier detectors.
// Each type may have a different syntax and content.
// TODO (cpakulski) - maybe use enums from protobufs.
enum class Upstream::Outlier::ExtResultType {
  TEST, // Used in unit tests.
  HTTP_CODE,
  LOCAL_ORIGIN,
};

namespace Extensions {
namespace Outlier {

using namespace Envoy::Upstream::Outlier;

// Types derived from TypedExtResult are used to report the result of transaction to
// an outlier detection monitor.
template <Upstream::Outlier::ExtResultType E>
class TypedExtResult : public Upstream::Outlier::ExtResult {
protected:
  TypedExtResult() : Upstream::Outlier::ExtResult(E) {}
};

class HttpCode : public TypedExtResult<Upstream::Outlier::ExtResultType::HTTP_CODE> {
public:
  HttpCode(uint32_t code) : code_(code) {}
  HttpCode() = delete;
  virtual ~HttpCode() {}
  uint32_t code() const { return code_; }

private:
  uint32_t code_;
};

// LocalOriginEvent is used to report errors like resets, timeouts but also
// successful connection attempts.
class LocalOriginEvent : public TypedExtResult<Upstream::Outlier::ExtResultType::LOCAL_ORIGIN> {
public:
  LocalOriginEvent(Result result) : result_(result) {}
  LocalOriginEvent() = delete;
  Result result() const { return result_; }

private:
  Result result_;
};

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

// Class groups error buckets. Buckets may be of different types.
// Base class for various types of monitors.
// Each monitor may implement different health detection algorithm.
class Monitor {
public:
  Monitor(const std::string& name, uint32_t enforce) : name_(name), enforce_(enforce) {}
  Monitor() = delete;
  virtual ~Monitor() {}
  void reportResult(const ExtResult&);

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
  virtual bool onError() PURE;
  virtual void onSuccess() PURE;
  virtual void onReset() PURE;
  // Default extra info is empty string. Descendant classes may overwrite it.
  virtual std::string getFailedExtraInfo() { return ""; }

  std::string name_;
  uint32_t enforce_{100};
  std::vector<ErrorsBucketPtr> buckets_;
  std::function<void(uint32_t, std::string, absl::optional<std::string>)> callback_;
};

using MonitorPtr = std::unique_ptr<Monitor>;

class MonitorsSet {
public:
  void addMonitor(MonitorPtr&& monitor) { monitors_.push_back(std::move(monitor)); }
  const std::vector<MonitorPtr>& monitors() { return monitors_; }

private:
  std::vector<MonitorPtr> monitors_;
};

class MonitorFactoryContext {
public:
  MonitorFactoryContext(ProtobufMessage::ValidationVisitor& validation_visitor)
      : validation_visitor_(validation_visitor) {}
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() { return validation_visitor_; }

private:
  ProtobufMessage::ValidationVisitor& validation_visitor_;
};

class MonitorFactory : public Envoy::Config::TypedFactory {
public:
  ~MonitorFactory() override = default;

  virtual MonitorPtr createMonitor(const std::string& name, const Protobuf::Message& config,
                                   MonitorFactoryContext& context) PURE;

  std::string category() const override { return "envoy.outlier_detection_monitors"; }
};

template <class ConfigProto> class MonitorFactoryBase : public MonitorFactory {
public:
  MonitorPtr createMonitor(const std::string& monitor_name, const Protobuf::Message& config,
                           MonitorFactoryContext& context) override {
    return createMonitorFromProtoTyped(monitor_name,
                                       MessageUtil::downcastAndValidate<const ConfigProto&>(
                                           config, context.messageValidationVisitor()),
                                       context);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  std::string name() const override { return name_; }

  MonitorFactoryBase(const std::string& name) : name_(name) {}

private:
  virtual MonitorPtr createMonitorFromProtoTyped(const std::string& monitor_name,
                                                 const ConfigProto& config,
                                                 MonitorFactoryContext& context) PURE;

  const std::string name_;
};

} // namespace Outlier
} // namespace Extensions
} // namespace Envoy
