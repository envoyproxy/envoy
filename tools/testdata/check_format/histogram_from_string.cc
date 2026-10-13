namespace Envoy {

void init(Stats::Scope& scope) {
  scope.histogramFromString("hello", Stats::Histogram::Unit::Unspecified);
}

} // namespace Envoy
