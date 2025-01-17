package io.envoyproxy.envoymobile

/**
 * Available logging levels for an Envoy instance. Note some levels may be compiled out.
 *
 * @param level string representation of a given log level.
 * @param levelInt integer representation of a given log level.
 */
enum class LogLevel(internal val level: String, val levelInt: Int) {
  TRACE("trace", 0),
  DEBUG("debug", 1),
  INFO("info", 2),
  WARN("warn", 3),
  ERROR("error", 4),
  CRITICAL("critical", 5),
  OFF("off", 6);

  companion object {
    /** Converts from numeric int to `LogLevel`. */
    fun from(level: Int): LogLevel {
      return when (level) {
        0 -> TRACE
        1 -> DEBUG
        2 -> INFO
        3 -> WARN
        4 -> ERROR
        5 -> CRITICAL
        else -> OFF
      }
    }
  }
}
