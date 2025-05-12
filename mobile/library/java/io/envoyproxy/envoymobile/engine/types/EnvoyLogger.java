package io.envoyproxy.envoymobile.engine.types;

public interface EnvoyLogger {
  /** The log level for this logger. */
  interface Level {
    int TRACE = 0;
    int DEBUG = 1;
    int INFO = 2;
    int WARN = 3;
    int ERROR = 4;
    int CRITICAL = 5;
    int OFF = 6;
  }

  /** Logs the given string with the specified log level. */
  void log(int logLevel, String str);
}
