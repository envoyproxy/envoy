package io.envoyproxy.envoymobile.engine.types;

import java.nio.ByteBuffer;

public interface EnvoyStringAccessor {

  /**
   * Called to retrieve a string from the Application
   */
  ByteBuffer getString();
}
