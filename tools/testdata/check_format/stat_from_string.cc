namespace Envoy {

void init(int stats) {
  stats.counter("hello");
  stats.gauge("world");
}

} // namespace Envoy
