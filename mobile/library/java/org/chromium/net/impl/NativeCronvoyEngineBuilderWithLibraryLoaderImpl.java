package org.chromium.net.impl;

import android.content.Context;
import org.chromium.net.CronetEngine.Builder.LibraryLoader;
import org.chromium.net.ICronetEngineBuilder;

/**
 * An extension of {@link NativeCronvoyEngineBuilderImpl} that implements
 * {@link ICronetEngineBuilder#setLibraryLoader}.
 */
public class NativeCronvoyEngineBuilderWithLibraryLoaderImpl
    extends NativeCronvoyEngineBuilderImpl {
  private CronvoyVersionSafeCallbacks.LibraryLoader mLibraryLoader;

  /**
   * Constructs a builder for Native Cronet Engine.
   * Default config enables SPDY, disables QUIC and HTTP cache.
   *
   * @param context Android {@link Context} for engine to use.
   */
  public NativeCronvoyEngineBuilderWithLibraryLoaderImpl(Context context) { super(context); }

  @Override
  public CronvoyEngineBuilderImpl setLibraryLoader(LibraryLoader loader) {
    mLibraryLoader = new CronvoyVersionSafeCallbacks.LibraryLoader(loader);
    return this;
  }

  @Override
  CronvoyVersionSafeCallbacks.LibraryLoader libraryLoader() {
    return mLibraryLoader;
  }
}
