namespace Envoy {

void init(Stats::Scope& scope) {
  scope.gaugeFromString("hello");
}

} // namespace Envoy
