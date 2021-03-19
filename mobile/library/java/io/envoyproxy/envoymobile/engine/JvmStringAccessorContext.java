package io.envoyproxy.envoymobile.engine;

import io.envoyproxy.envoymobile.engine.types.EnvoyStringAccessor;

class JvmStringAccessorContext {
  private final EnvoyStringAccessor accessor;

  public JvmStringAccessorContext(EnvoyStringAccessor accessor) { this.accessor = accessor; }

  /**
   * Invokes getEnvoyString callback. This method signature is used within the jni_interface.cc.
   * Changing naming of this class or methods will likely require an audit across the jni usages
   * and proguard rules.
   *
   * @return String, the string retrieved from the platform.
   */
  public String getEnvoyString() { return accessor.getEnvoyString(); }
}
