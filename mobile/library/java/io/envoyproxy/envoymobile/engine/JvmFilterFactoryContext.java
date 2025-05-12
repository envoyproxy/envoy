package io.envoyproxy.envoymobile.engine;

import io.envoyproxy.envoymobile.engine.JvmFilterContext;
import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPFilterFactory;

/**
 * Wrapper class for EnvoyHTTPFilterFactory for receiving JNI calls.
 */
class JvmFilterFactoryContext {
  private final EnvoyHTTPFilterFactory filterFactory;

  public JvmFilterFactoryContext(EnvoyHTTPFilterFactory filterFactory) {
    this.filterFactory = filterFactory;
  }

  public JvmFilterContext create() { return new JvmFilterContext(filterFactory.create()); }
}
