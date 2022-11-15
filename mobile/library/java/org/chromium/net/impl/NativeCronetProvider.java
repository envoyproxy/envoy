package org.chromium.net.impl;

import android.content.Context;
import java.util.Arrays;
import org.chromium.net.CronetEngine;
import org.chromium.net.CronetProvider;
import org.chromium.net.ExperimentalCronetEngine;
import org.chromium.net.ICronetEngineBuilder;

/**
 * Implementation of {@link CronetProvider} that creates {@link CronetEngine.Builder}
 * for building the native implementation of {@link CronetEngine}.
 */
public class NativeCronetProvider extends CronetProvider {
  /**
   * Constructor.
   *
   * @param context Android context to use.
   */
  // TODO(carloseltuerto) find something similar to @UsedByReflection("CronetProvider.java")
  public NativeCronetProvider(Context context) { super(context); }

  @Override
  public CronetEngine.Builder createBuilder() {
    ICronetEngineBuilder impl = new NativeCronetEngineBuilderWithLibraryLoaderImpl(mContext);
    return new ExperimentalCronetEngine.Builder(impl);
  }

  @Override
  public String getName() {
    return CronetProvider.PROVIDER_NAME_APP_PACKAGED;
  }

  @Override
  public String getVersion() {
    return ImplVersion.getCronetVersion();
  }

  @Override
  public boolean isEnabled() {
    return true;
  }

  @Override
  public int hashCode() {
    return Arrays.hashCode(new Object[] {NativeCronetProvider.class, mContext});
  }

  @Override
  public boolean equals(Object other) {
    return other == this || (other instanceof NativeCronetProvider &&
                             this.mContext.equals(((NativeCronetProvider)other).mContext));
  }
}
