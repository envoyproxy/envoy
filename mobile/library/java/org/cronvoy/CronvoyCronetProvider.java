package org.cronvoy;

import android.content.Context;
import java.util.Arrays;
import org.chromium.net.CronetEngine;
import org.chromium.net.CronetProvider;
import org.chromium.net.ExperimentalCronetEngine;
import org.chromium.net.ICronetEngineBuilder;

/**
 * Implementation of {@link CronetProvider} that creates {@link CronetEngine.Builder} for building
 * the Java-based implementation of {@link CronetEngine}.
 */
public final class CronvoyCronetProvider extends CronetProvider {

  public static final String CRONVOY_PROVIDER_NAME = "Cronvoy-Cronet-Provider";

  /**
   * Constructor.
   *
   * @param context Android context to use.
   */
  public CronvoyCronetProvider(Context context) { super(context); }

  @Override
  public CronetEngine.Builder createBuilder() {
    ICronetEngineBuilder impl = new CronvoyEngineBuilderImpl(mContext);
    return new ExperimentalCronetEngine.Builder(impl);
  }

  @Override
  public String getName() {
    return CRONVOY_PROVIDER_NAME;
  }

  @Override
  public String getVersion() {
    // TODO(carloseltuerto) hook this to envoy-mobile/VERSION
    return "0.4.0.04272021";
  }

  @Override
  public boolean isEnabled() {
    return true;
  }

  @Override
  public int hashCode() {
    return Arrays.hashCode(new Object[] {CronvoyCronetProvider.class, mContext});
  }

  @Override
  public boolean equals(Object other) {
    return other == this || (other instanceof CronvoyCronetProvider &&
                             this.mContext.equals(((CronvoyCronetProvider)other).mContext));
  }
}
