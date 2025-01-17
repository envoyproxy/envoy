package io.envoyproxy.envoymobile.engine.types;

import java.util.Map;

public interface EnvoyEventTracker {
  void track(Map<String, String> events);
}
