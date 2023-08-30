package org.chromium.net.impl;

import android.content.Context;
import java.util.Arrays;
import org.chromium.net.CronetEngine;
import org.chromium.net.CronvoyProvider;
import org.chromium.net.ExperimentalCronetEngine;
import org.chromium.net.ICronetEngineBuilder;

/**
 * Implementation of {@link CronvoyProvider} that creates {@link CronetEngine.Builder}
 * for building the native implementation of {@link CronetEngine}.
 */
public class NativeCronvoyProvider extends CronvoyProvider {
  /**
   * Constructor.
   *
   * @param context Android context to use.
   */
  // TODO(carloseltuerto) find something similar to @UsedByReflection("CronetProvider.java")
  public NativeCronvoyProvider(Context context) { super(context); }

  @Override
  public CronetEngine.Builder createBuilder() {
    ICronetEngineBuilder impl = new NativeCronvoyEngineBuilderWithLibraryLoaderImpl(mContext);
    return new ExperimentalCronetEngine.Builder(impl);
  }

  @Override
  public String getName() {
    return CronvoyProvider.PROVIDER_NAME_APP_PACKAGED;
  }

  @Override
  public String getVersion() {
    return CronvoyImplVersion.getCronetVersion();
  }

  @Override
  public boolean isEnabled() {
    return true;
  }

  @Override
  public int hashCode() {
    return Arrays.hashCode(new Object[] {NativeCronvoyProvider.class, mContext});
  }

  @Override
  public boolean equals(Object other) {
    return other == this || (other instanceof NativeCronvoyProvider &&
                             this.mContext.equals(((NativeCronvoyProvider)other).mContext));
  }
}
