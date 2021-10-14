package io.envoyproxy.envoymobile

/**
 * Available logging levels for an Envoy instance. Note some levels may be compiled out.
 */
enum class LogLevel(internal val level: String, val levelInt: Int) {
  TRACE("trace", 0),
  DEBUG("debug", 1),
  INFO("info", 2),
  WARN("warn", 3),
  ERROR("error", 4),
  CRITICAL("critical", 5),
  OFF("off", -1);
}
