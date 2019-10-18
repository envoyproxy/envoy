namespace Envoy {

void init(Stats::Scope& scope) { scope.histogram("hello", Stats::Histogram::Unit::Unspecified); }

} // namespace Envoy
