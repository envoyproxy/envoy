package io.envoyproxy.envoymobile.engine;

import android.app.Application;
import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks;

/* Android-specific implementation of the `EnvoyEngine` interface. */
public class AndroidEngineImpl implements EnvoyEngine {
  private final Application application;
  private final EnvoyEngine envoyEngine;

  public AndroidEngineImpl(Application application) {
    this.application = application;
    this.envoyEngine = new EnvoyEngineImpl();
    AndroidJniLibrary.load(application.getBaseContext());
    AndroidNetworkMonitor.load(application.getBaseContext());
  }

  @Override
  public EnvoyHTTPStream startStream(EnvoyHTTPCallbacks callbacks) {
    return envoyEngine.startStream(callbacks);
  }

  @Override
  public int runWithConfig(String configurationYAML, String logLevel) {
    // re-enable lifecycle-based stat flushing when https://github.com/lyft/envoy-mobile/issues/748
    // gets fixed. AndroidAppLifecycleMonitor monitor = new AndroidAppLifecycleMonitor();
    // application.registerActivityLifecycleCallbacks(monitor);
    return envoyEngine.runWithConfig(configurationYAML, logLevel);
  }

  @Override
  public int runWithConfig(EnvoyConfiguration envoyConfiguration, String logLevel) {
    // re-enable lifecycle-based stat flushing when https://github.com/lyft/envoy-mobile/issues/748
    // gets fixed. AndroidAppLifecycleMonitor monitor = new AndroidAppLifecycleMonitor();
    // application.registerActivityLifecycleCallbacks(monitor);
    return envoyEngine.runWithConfig(envoyConfiguration, logLevel);
  }
}
