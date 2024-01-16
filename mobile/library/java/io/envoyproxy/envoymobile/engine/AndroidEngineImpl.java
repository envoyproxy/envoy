package io.envoyproxy.envoymobile.engine;

import android.content.Context;
import io.envoyproxy.envoymobile.engine.types.EnvoyEventTracker;
import io.envoyproxy.envoymobile.engine.types.EnvoyHTTPCallbacks;
import io.envoyproxy.envoymobile.engine.types.EnvoyLogger;
import io.envoyproxy.envoymobile.engine.types.EnvoyNetworkType;
import io.envoyproxy.envoymobile.engine.types.EnvoyOnEngineRunning;
import io.envoyproxy.envoymobile.engine.types.EnvoyStringAccessor;
import io.envoyproxy.envoymobile.engine.types.EnvoyStatus;
import io.envoyproxy.envoymobile.utilities.ContextUtils;

import java.util.Map;

/* Android-specific implementation of the `EnvoyEngine` interface. */
public class AndroidEngineImpl implements EnvoyEngine {
  private final EnvoyEngine envoyEngine;

  /**
   * @param runningCallback Called when the engine finishes its async startup and begins running.
   */
  public AndroidEngineImpl(Context context, EnvoyOnEngineRunning runningCallback,
                           EnvoyLogger logger, EnvoyEventTracker eventTracker,
                           Boolean enableProxying) {
    this.envoyEngine = new EnvoyEngineImpl(runningCallback, logger, eventTracker);
    if (ContextUtils.getApplicationContext() == null) {
      ContextUtils.initApplicationContext(context.getApplicationContext());
    }
    AndroidJniLibrary.load(context);
    AndroidNetworkMonitor.load(context, envoyEngine);
    if (enableProxying) {
      AndroidProxyMonitor.load(context, envoyEngine);
    }
  }

  @Override
  public EnvoyHTTPStream startStream(EnvoyHTTPCallbacks callbacks, boolean explicitFlowControl) {
    return envoyEngine.startStream(callbacks, explicitFlowControl);
  }

  @Override
  public void performRegistration(EnvoyConfiguration envoyConfiguration) {
    envoyEngine.performRegistration(envoyConfiguration);
  }

  @Override
  public EnvoyStatus runWithYaml(String configurationYAML, String logLevel) {
    return envoyEngine.runWithYaml(configurationYAML, logLevel);
  }

  @Override
  public EnvoyStatus runWithConfig(EnvoyConfiguration envoyConfiguration, String logLevel) {
    return envoyEngine.runWithConfig(envoyConfiguration, logLevel);
  }

  @Override
  public void terminate() {
    envoyEngine.terminate();
  }

  @Override
  public String dumpStats() {
    return envoyEngine.dumpStats();
  }

  @Override
  public int recordCounterInc(String elements, Map<String, String> tags, int count) {
    return envoyEngine.recordCounterInc(elements, tags, count);
  }

  @Override
  public int registerStringAccessor(String accessorName, EnvoyStringAccessor accessor) {
    return envoyEngine.registerStringAccessor(accessorName, accessor);
  }

  @Override
  public void resetConnectivityState() {
    envoyEngine.resetConnectivityState();
  }

  @Override
  public void setPreferredNetwork(EnvoyNetworkType network) {
    envoyEngine.setPreferredNetwork(network);
  }

  public void setProxySettings(String host, int port) { envoyEngine.setProxySettings(host, port); }

  public void setLogLevel(LogLevel log_level) { envoyEngine.setLogLevel(log_level); }
}
