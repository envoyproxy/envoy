package org.chromium.net.testing;

import org.chromium.net.ExperimentalCronetEngine;
import org.chromium.net.impl.NativeCronvoyEngineBuilderImpl;

/**
 * Utilities for Cronet testing
 */
public final class CronetTestUtil {

  public static void setMockCertVerifierForTesting(ExperimentalCronetEngine.Builder builder) {
    getCronetEngineBuilderImpl(builder).setMockCertVerifierForTesting();
  }

  public static NativeCronvoyEngineBuilderImpl
  getCronetEngineBuilderImpl(ExperimentalCronetEngine.Builder builder) {
    return (NativeCronvoyEngineBuilderImpl)builder.getBuilderDelegate();
  }

  public static boolean nativeCanGetTaggedBytes() {
    return false; // TODO(carloseltuerto) implement
  }

  public static long nativeGetTaggedBytes(int tag) {
    return 0; // TODO(carloseltuerto) implement
  }

  private CronetTestUtil() {}
}
