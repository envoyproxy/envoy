package org.chromium.net.impl;

import android.content.Context;

import org.chromium.net.CronetEngine;
import org.chromium.net.CronetProvider;
import org.chromium.net.ExperimentalCronetEngine;
import org.chromium.net.ICronetEngineBuilder;

import java.util.Arrays;

/**
 * Implementation of {@link CronetProvider} that creates {@link CronetEngine.Builder}
 * for building the Java-based implementation of {@link CronetEngine}.
 */
public class JavaCronetProvider extends CronetProvider {
  /**
   * Constructor.
   *
   * @param context Android context to use.
   */
  public JavaCronetProvider(Context context) { super(context); }

  @Override
  public CronetEngine.Builder createBuilder() {
    ICronetEngineBuilder impl = new JavaCronetEngineBuilderImpl(mContext);
    return new ExperimentalCronetEngine.Builder(impl);
  }

  @Override
  public String getName() {
    return CronetProvider.PROVIDER_NAME_FALLBACK;
  }

  @Override
  public String getVersion() {
    // TODO(carloseltuerto) please fix
    return "ImplVersion.getCronetVersion()";
  }

  @Override
  public boolean isEnabled() {
    return true;
  }

  @Override
  public int hashCode() {
    return Arrays.hashCode(new Object[] {JavaCronetProvider.class, mContext});
  }

  @Override
  public boolean equals(Object other) {
    return other == this || (other instanceof JavaCronetProvider &&
                             this.mContext.equals(((JavaCronetProvider)other).mContext));
  }
}
