package io.envoyproxy.envoymobile.engine.types;

public interface EnvoyHTTPFilterFactory {

  String getFilterName();

  EnvoyHTTPFilter create();
}
