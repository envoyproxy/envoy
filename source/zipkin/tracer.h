#pragma once

#include "zipkin_core_types.h"
#include "span_context.h"
#include "tracer_interface.h"

namespace Zipkin {

/**
 * Abstract class for Tracer users to implement their own span-buffer management policies
 */
class Reporter {
public:
  virtual ~Reporter() {}

  /**
   * Span-buffer management policy to be implemented by users of the Trace class
   */
  virtual void reportSpan(Span&& span) = 0;
};

class Tracer : public TracerInterface {
public:
  Tracer(const std::string& service_name, const std::string& address)
      : service_name_(service_name), address_(address) {}

  virtual ~Tracer() {}

  /**
   * Creates a "root" span
   */
  Span startSpan(const std::string& operation_name, uint64_t start_time);

  /**
   * Based on the given context, creates either a "child" or a "shared-context" span
   */
  Span startSpan(const std::string& operation_name, uint64_t start_time,
                 SpanContext& previous_context);

  /**
   * Called when the Span is finished
   */
  void reportSpan(Span&& span) override;

  std::shared_ptr<Reporter> reporter() { return reporter_; }

  void setReporter(std::unique_ptr<Reporter> reporter);

private:
  void getIPAndPort(const std::string& address, std::string& ip, uint16_t& port);

  std::string service_name_;
  std::string address_;

  std::shared_ptr<Reporter> reporter_;
};
} // Zipkin
