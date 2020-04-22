namespace Envoy {

void init(Stats::Scope& scope) {
  scope.counterFromString("hello");
}

} // namespace Envoy
