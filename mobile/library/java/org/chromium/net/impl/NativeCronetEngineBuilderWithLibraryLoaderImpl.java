package org.chromium.net.impl;

import android.content.Context;
import org.chromium.net.CronetEngine.Builder.LibraryLoader;
import org.chromium.net.ICronetEngineBuilder;

/**
 * An extension of {@link NativeCronetEngineBuilderImpl} that implements
 * {@link ICronetEngineBuilder#setLibraryLoader}.
 */
public class NativeCronetEngineBuilderWithLibraryLoaderImpl extends NativeCronetEngineBuilderImpl {
  private VersionSafeCallbacks.LibraryLoader mLibraryLoader;

  /**
   * Constructs a builder for Native Cronet Engine.
   * Default config enables SPDY, disables QUIC and HTTP cache.
   *
   * @param context Android {@link Context} for engine to use.
   */
  public NativeCronetEngineBuilderWithLibraryLoaderImpl(Context context) { super(context); }

  @Override
  public CronetEngineBuilderImpl setLibraryLoader(LibraryLoader loader) {
    mLibraryLoader = new VersionSafeCallbacks.LibraryLoader(loader);
    return this;
  }

  @Override
  VersionSafeCallbacks.LibraryLoader libraryLoader() {
    return mLibraryLoader;
  }
}
