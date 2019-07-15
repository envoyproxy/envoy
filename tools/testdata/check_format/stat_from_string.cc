namespace Envoy {

void init(Stats::Scope& scope) {
  scope.counter("hello");
  scope.gauge("world");
}

} // namespace Envoy
