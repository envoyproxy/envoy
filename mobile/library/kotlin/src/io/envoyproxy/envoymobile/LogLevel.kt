package io.envoyproxy.envoymobile

/**
 * Available logging levels for an Envoy instance. Note some levels may be compiled out.
 */
enum class LogLevel(internal val level: String) {
  TRACE("trace"),
  DEBUG("debug"),
  INFO("info"),
  WARN("warn"),
  ERROR("error"),
  CRITICAL("critical"),
  OFF("off");
}
