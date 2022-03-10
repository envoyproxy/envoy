package org.chromium.net.testing;

import org.chromium.net.ExperimentalCronetEngine;
import org.chromium.net.impl.NativeCronetEngineBuilderImpl;

/**
 * Utilities for Cronet testing
 */
public final class CronetTestUtil {

  public static void setMockCertVerifierForTesting(ExperimentalCronetEngine.Builder builder) {
    getCronetEngineBuilderImpl(builder).setMockCertVerifierForTesting();
  }

  public static NativeCronetEngineBuilderImpl
  getCronetEngineBuilderImpl(ExperimentalCronetEngine.Builder builder) {
    return (NativeCronetEngineBuilderImpl)builder.getBuilderDelegate();
  }

  private CronetTestUtil() {}
}
