#include "envoy/split"

// NOLINT(namespace-envoy)

class Foo {};

class Bar {
  Foo getFoo();
};

class FooBar : Foo, Bar {};

class DeadBeaf {
public:
  int val();
  FooBar foobar;
};