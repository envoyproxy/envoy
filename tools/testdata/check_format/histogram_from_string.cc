namespace Envoy {

void init(Stats::Scope& scope) {
  scope.histogram("hello");
}

} // namespace Envoy
