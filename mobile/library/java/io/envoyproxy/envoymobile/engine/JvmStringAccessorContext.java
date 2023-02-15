package io.envoyproxy.envoymobile.engine;

import io.envoyproxy.envoymobile.engine.types.EnvoyStringAccessor;
import java.nio.charset.StandardCharsets;

class JvmStringAccessorContext {
  private final EnvoyStringAccessor accessor;

  public JvmStringAccessorContext(EnvoyStringAccessor accessor) { this.accessor = accessor; }

  /**
   * Invokes getEnvoyString callback. This method signature is used within the jni_interface.cc.
   * Changing naming of this class or methods will likely require an audit across the jni usages
   * and proguard rules.
   *
   * @return byte[], the string retrieved from the platform.
   */
  public byte[] getEnvoyString() {
    // This class returns a byte[] instead of a String because dealing with Java Strings in the
    // JNI is a bit finicky.
    return accessor.getEnvoyString().getBytes(StandardCharsets.UTF_8);
  }
}
