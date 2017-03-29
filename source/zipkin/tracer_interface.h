#pragma once

namespace Zipkin {

class Span;

class TracerInterface {
public:
  virtual ~TracerInterface() {}

  virtual void reportSpan(Span&& span) = 0;
};

typedef TracerInterface* TracerPtr;

} // Zipkin
