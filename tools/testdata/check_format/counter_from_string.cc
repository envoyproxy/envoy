namespace Envoy {

void init(Stats::Scope& scope) {
  scope.counter("hello");
}

} // namespace Envoy
