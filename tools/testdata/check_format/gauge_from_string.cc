namespace Envoy {

void init(Stats::Scope& scope) {
  scope.gauge("hello");
}

} // namespace Envoy
