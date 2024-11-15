package io.envoyproxy.envoymobile.engine.testing;

import io.envoyproxy.envoymobile.engine.EnvoyConfiguration;

/**
 * Wrapper class for test JNI functions
 */
public final class TestJni {
  public static String createProtoString(EnvoyConfiguration envoyConfiguration) {
    return nativeCreateProtoString(envoyConfiguration.createBootstrap());
  }

  private static native String nativeCreateProtoString(long bootstrap);

  private TestJni() {}
}
