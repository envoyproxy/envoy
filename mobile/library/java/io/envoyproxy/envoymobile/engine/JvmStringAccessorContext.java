package io.envoyproxy.envoymobile.engine;

import java.nio.ByteBuffer;

import io.envoyproxy.envoymobile.engine.types.EnvoyStringAccessor;

class JvmStringAccessorContext {
  private final EnvoyStringAccessor accessor;

  public JvmStringAccessorContext(EnvoyStringAccessor accessor) { this.accessor = accessor; }

  /**
   * Invokes getString callback.
   *
   * @return ByteBuffer, the string retrieved from the platform.
   */
  public ByteBuffer getString() { return accessor.getString(); }
}
