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
// TODO - maybe use enums from protobufs.
enum class Upstream::Outlier::ErrorType {
  TEST, // Used in unit tests.
  HTTP_CODE,
  LOCAL_ORIGIN,
};

namespace Extensions {
namespace Outlier {

using namespace Envoy::Upstream::Outlier;

// Base class for monitors. It defines interface to:
// - components reporting errors
// - cluster manager which checks for health of the endpoints
#if 0
class ODMonitor {
  // Define PURE functions as API between Envoy and outlier detection mechanism.
};

using ODMonitorPtr = std::unique_ptr<ODMonitor>;
#endif

// Types derived from TypedError are used to report the result of transaction to
// an outlier detection monitor.
template <Upstream::Outlier::ErrorType E> class TypedError : public Upstream::Outlier::Error {
protected:
  TypedError() : Upstream::Outlier::Error(E) {}
};

class HttpCode : public TypedError<Upstream::Outlier::ErrorType::HTTP_CODE> {
public:
  HttpCode(uint64_t code) : code_(code) {}
  HttpCode() = delete;
  // ErrorType type() const override { return ErrorType::HTTP_CODE; }
  virtual ~HttpCode() {}
  uint64_t code() const { return code_; }

private:
  uint64_t code_;
};

// ErrorsBucket is used by outlier detection monitors and is used to
// "catch" reported Error (TypedError);
class ErrorsBucket {
public:
  virtual bool matchType(const ErrorPtr&) PURE;
  virtual bool matchType(const Error&) PURE;
  virtual bool match(const ErrorPtr&) PURE;
  virtual bool match(const Error&) PURE;
  virtual ~ErrorsBucket() {}
};

template <Upstream::Outlier::ErrorType E> class TypedErrorsBucket : public ErrorsBucket {
public:
  bool matchType(const ErrorPtr& error) override { return (error->type() == E); }
  bool matchType(const Error& error) override { return (error.type() == E); }

  bool match(const ErrorPtr& error) override {
    return matches(static_cast<TypedError<E>&>(*error));
  }

  bool match(const Error& error) override {
    return matches(static_cast<const TypedError<E>&>(error));
  }

  virtual ~TypedErrorsBucket() {}

private:
  virtual bool matches(const TypedError<E>&) const PURE;
};

using ErrorsBucketPtr = std::unique_ptr<ErrorsBucket>;

// Class defines a range of consecutive HTTP codes.
class HTTPErrorCodesBucket : public TypedErrorsBucket<Upstream::Outlier::ErrorType::HTTP_CODE> {
public:
  // ErrorType type() const override { return ErrorType::HTTP_CODE; }
  HTTPErrorCodesBucket() = delete;
  HTTPErrorCodesBucket(const std::string& name, uint64_t start, uint64_t end)
      : name_(name), start_(start), end_(end) {}
  const std::string& name() const { return name_; }
  bool contains(uint64_t code) { return ((code >= start_) && (code <= end_)); }
  bool matches(const TypedError<Upstream::Outlier::ErrorType::HTTP_CODE>&) const override;
  // bool matches(const HttpCode&) const override;

  virtual ~HTTPErrorCodesBucket() {}

private:
  std::string name_;
  uint64_t start_, end_;
};

// Class groups error buckets. Buckets may be of different types.
// Base class for various types of monitors.
// Each monitor may implement different health detection algorithm.
class Monitor {
public:
  //  Monitor() = delete;
  virtual ~Monitor() {}
  void addErrorBucket(ErrorsBucketPtr&& bucket); // {buckets_.push_back(std::move(bucket));}
  void reportResult(const Error&);
  // TODO - make this private.
  // absl::flat_hash_map<ErrorType, std::vector<ErrorsBucketPtr>> buckets_;
  std::vector<ErrorsBucketPtr> buckets_;
  std::function<void(uint32_t)> callback_;

  bool tripped() const { return tripped_; }
  void reset() {
    tripped_ = false;
    onReset();
  }

protected:
  virtual bool onError() PURE;
  virtual void onSuccess() PURE;
  virtual void onReset() PURE;

  bool tripped_{false};
};

using MonitorPtr = std::unique_ptr<Monitor>;
void processBucketsConfig(
    Monitor& monitor,
    const envoy::extensions::outlier_detection_monitors::common::v3::ErrorBuckets& config);

class MonitorsSet {
public:
  void addMonitor(MonitorPtr&& monitor) { monitors_.push_back(std::move(monitor)); }
  const std::vector<MonitorPtr>& monitors() { return monitors_; }

private:
  std::vector<MonitorPtr> monitors_;
};

// (todo): maybe move it to config file
class MonitorFactoryContext {
public:
  MonitorFactoryContext(ProtobufMessage::ValidationVisitor& validation_visitor)
      : validation_visitor_(validation_visitor) {}
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() { return validation_visitor_; }

private:
  // ProtobufMessage::ProdValidationContextImpl validation_context_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
};

// This should go to something like source/extensions/outlier_detection/common
// (todo): maybe move it to config file
class MonitorFactory : public Config::TypedFactory {
public:
  ~MonitorFactory() override = default;

  virtual MonitorPtr createMonitor(const Protobuf::Message& config,
                                   MonitorFactoryContext& context) PURE;

  std::string category() const override { return "envoy.outlier_detection_monitors"; }
};

// (todo): maybe move it to config file
template <class ConfigProto> class MonitorFactoryBase : public MonitorFactory {
public:
  MonitorPtr createMonitor(const Protobuf::Message& config,
                           MonitorFactoryContext& context) override {
    return createMonitorFromProtoTyped(MessageUtil::downcastAndValidate<const ConfigProto&>(
                                           config, context.messageValidationVisitor()),
                                       context);
  }

  // (todo): maybe move it to config file
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  std::string name() const override { return name_; }

  MonitorFactoryBase(const std::string& name) : name_(name) {}

private:
  virtual MonitorPtr createMonitorFromProtoTyped(const ConfigProto& config,
                                                 MonitorFactoryContext& context) PURE;

  const std::string name_;
};

} // namespace Outlier
} // namespace Extensions
} // namespace Envoy
