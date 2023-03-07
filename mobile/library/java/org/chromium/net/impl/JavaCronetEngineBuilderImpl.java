package org.chromium.net.impl;

import android.content.Context;
import org.chromium.net.ExperimentalCronetEngine;
import org.chromium.net.ICronetEngineBuilder;

/**
 * Implementation of {@link ICronetEngineBuilder} that builds Java-based Cronet engine.
 */
final class JavaCronetEngineBuilderImpl extends CronetEngineBuilderImpl {
  /**
   * Builder for Platform Cronet Engine.
   *
   * @param context Android {@link Context} for engine to use.
   */
  public JavaCronetEngineBuilderImpl(Context context) { super(context); }

  @Override
  public ExperimentalCronetEngine build() {
    if (getUserAgent() == null) {
      setUserAgent(getDefaultUserAgent());
    }
    return new JavaCronetEngine(this);
  }
}
