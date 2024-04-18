package io.envoyproxy.envoymobile.engine.testing;

import io.envoyproxy.envoymobile.engine.EnvoyConfiguration;

/**
 * Wrapper class for test JNI functions
 */
public final class TestJni {
  public static String createYaml(EnvoyConfiguration envoyConfiguration) {
    return nativeCreateYaml(envoyConfiguration.createBootstrap());
  }

  private static native String nativeCreateYaml(long bootstrap);

  private TestJni() {}
}
