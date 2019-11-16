#pragma once

#include "common/http/http1/parser.h"

#include <memory>

namespace Envoy {
namespace Http {
namespace Http1 {

class LlHttpParserImpl : public Parser {
public:
  LlHttpParserImpl(MessageType type, void* data);
  ~LlHttpParserImpl();
  int execute(const char* data, int len) override;
  void resume() override;
  int pause() override;
  int getErrno() override;
  int statusCode() const override;
  int httpMajor() const  override;
  int httpMinor() const override;
  uint64_t contentLength() const override;
  int flags() const override;
  uint16_t method() const override;
  const char* methodName() const override;
  const char* errnoName() override;
  bool usesOldImpl() const override { return false; }

private:
  // PImpl pattern is used to contain the llhttp library to avoid global namespace collisions.
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace Http1
} // namespace Http
} // namespace Envoy
