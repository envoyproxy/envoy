package io.envoyproxy.envoymobile.engine;

import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPFilterCallbacks;

final class EnvoyHTTPFilterCallbacksImpl
    implements EnvoyHTTPFilterCallbacks, EnvoyNativeResourceWrapper {

  private static final EnvoyNativeResourceReleaser releaseCallbacks = (long handle) -> {
    callReleaseCallbacks(handle);
  };

  private final long callbackHandle;

  /**
   * @param callbackHandle, native handle for callback execution. This must be eventually passed to
                            `callReleaseCallbacks` to release underlying memory.
   */
  EnvoyHTTPFilterCallbacksImpl(long callbackHandle) { this.callbackHandle = callbackHandle; }

  static EnvoyHTTPFilterCallbacksImpl create(long callbackHandle) {
    final EnvoyHTTPFilterCallbacksImpl object = new EnvoyHTTPFilterCallbacksImpl(callbackHandle);
    EnvoyNativeResourceRegistry.globalRegister(object, callbackHandle, releaseCallbacks);
    return object;
  }

  public void resumeIteration() { callResumeIteration(callbackHandle, this); }

  /**
   * @param callbackHandle, native handle for callback execution.
   * @param object, pass this object so that the JNI retains it, preventing it from potentially
   *                being concurrently garbage-collected while the native call is executing.
   */
  private native void callResumeIteration(long callbackHandle, EnvoyHTTPFilterCallbacksImpl object);

  private static native void callReleaseCallbacks(long callbackHandle);
}
