#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/protobuf/message_validator.h"

#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Outlier {

class ODMonitor {
  // Define PURE functions as API between Envoy and outlier detection mechanism.
};

using ODMonitorPtr = std::unique_ptr<ODMonitor>;

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
class MonitorFactory : public Config::TypedFactory {
public:
  ~MonitorFactory() override = default;

  virtual ODMonitorPtr createMonitor(const Protobuf::Message& config,
                                     MonitorFactoryContext& context) PURE;

  std::string category() const override { return "envoy.outlier_detection_monitors"; }
};

template <class ConfigProto> class MonitorFactoryBase : public MonitorFactory {
public:
  ODMonitorPtr createMonitor(const Protobuf::Message& config,
                             MonitorFactoryContext& context) override {
    return createMonitorFromProtoTyped(MessageUtil::downcastAndValidate<const ConfigProto&>(
                                           config, context.messageValidationVisitor()),
                                       context);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ConfigProto>();
  }

  std::string name() const override { return name_; }

  MonitorFactoryBase(const std::string& name) : name_(name) {}

private:
  virtual ODMonitorPtr createMonitorFromProtoTyped(const ConfigProto& config,
                                                   MonitorFactoryContext& context) PURE;

  const std::string name_;
};

// Types of results which can be reported.
enum class ErrorType {
  HTTP_CODE,
  LOCAL_ORIGIN,
  DATABASE,
};

class Error {
public:
  virtual ErrorType type() const PURE;
  virtual ~Error() = default;
};

class HttpCode : public Error {
public:
  HttpCode(uint64_t code) : code_(code) {}
  HttpCode() = delete;
  ErrorType type() const override { return ErrorType::HTTP_CODE; }
  virtual ~HttpCode() {}
  uint64_t code() const { return code_; }

private:
  uint64_t code_;
};

class ErrorsBucket {
public:
  virtual bool matches(const Error&) const PURE;
  virtual ErrorType type() const PURE;
  virtual ~ErrorsBucket() {}
};

using ErrorsBucketPtr = std::unique_ptr<ErrorsBucket>;

// Class defines a range of consecutive HTTP codes.
class HTTPErrorCodesBucket : public ErrorsBucket {
public:
  ErrorType type() const override { return ErrorType::HTTP_CODE; }
  HTTPErrorCodesBucket() = delete;
  HTTPErrorCodesBucket(const std::string& name, uint64_t start, uint64_t end)
      : name_(name), start_(start), end_(end) {}
  const std::string& name() const { return name_; }
  bool contains(uint64_t code) { return ((code >= start_) && (code <= end_)); }
  bool matches(const Error&) const override;

  virtual ~HTTPErrorCodesBucket() {}

private:
  std::string name_;
  uint64_t start_, end_;
};

// Class groups error buckets. Buckets may be of different types.
class Monitor {
public:
  virtual ~Monitor() {}
  void addErrorBucket(ErrorsBucketPtr&& bucket); // {buckets_.push_back(std::move(bucket));}
  virtual bool reportResult(const Error&) PURE;
  absl::flat_hash_map<ErrorType, std::vector<ErrorsBucketPtr>> buckets_;

  bool tripped() const { return tripped_; }
  virtual void reset() PURE;

protected:
  bool tripped_{false};
};

using MonitorPtr = std::unique_ptr<Monitor>;

class MonitorsSet {
public:
  void addMonitor(MonitorPtr&& monitor) { monitors_.push_back(std::move(monitor)); }
  const std::vector<MonitorPtr>& monitors() { return monitors_; }

private:
  std::vector<MonitorPtr> monitors_;
};

} // namespace Outlier
} // namespace Extensions
} // namespace Envoy
