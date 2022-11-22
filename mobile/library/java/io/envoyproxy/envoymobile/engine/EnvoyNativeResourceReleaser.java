package io.envoyproxy.envoymobile.engine;

public interface EnvoyNativeResourceReleaser {

  /**
   * Release a native resource held by a Java object.
   *
   * @param @nativeHandle, JNI identifier for the native resource.
   */
  public void release(long nativeHandle);
}
