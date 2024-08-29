package io.envoyproxy.envoymobile.engine;

import io.envoyproxy.envoymobile.engine.types.EnvoyNetworkType;

import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.net.ConnectivityManager;
import android.net.ConnectivityManager.NetworkCallback;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;

import androidx.annotation.NonNull;
import androidx.annotation.VisibleForTesting;
import androidx.core.content.ContextCompat;

import java.util.Collections;

/**
 * This class does the following.
 * <ul>
 * <li>When the internet is available: call the <code>InternalEngine::onNetworkAvailable</code>
 * callback.</li>
 *
 * <li>When the internet is not available: call the
 * <code>InternalEngine::onNetworkUnavailable</code> callback.</li>
 *
 * <li>When the capabilities are changed: call the
 * <code>EnvoyEngine::setPreferredNetwork</code>.</li>
 * </ul>
 */
public class AndroidNetworkMonitor {
  private static final String PERMISSION_DENIED_STATS_ELEMENT =
      "android_permissions.network_state_denied";
  private static volatile AndroidNetworkMonitor instance = null;
  private ConnectivityManager connectivityManager;

  public static void load(Context context, EnvoyEngine envoyEngine) {
    if (instance != null) {
      return;
    }

    synchronized (AndroidNetworkMonitor.class) {
      if (instance != null) {
        return;
      }
      instance = new AndroidNetworkMonitor(context, envoyEngine);
    }
  }

  /**
   * Sets the {@link AndroidNetworkMonitor} singleton instance to null, so that it can be recreated
   * when a new EnvoyEngine is created.
   */
  @VisibleForTesting
  public static void shutdown() {
    instance = null;
  }

  private AndroidNetworkMonitor(Context context, EnvoyEngine envoyEngine) {
    int permission =
        ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_NETWORK_STATE);
    if (permission == PackageManager.PERMISSION_DENIED) {
      try {
        envoyEngine.recordCounterInc(PERMISSION_DENIED_STATS_ELEMENT, Collections.emptyMap(), 1);
      } catch (Throwable t) {
        // no-op if this errors out and return
      }
      return;
    }

    connectivityManager =
        (ConnectivityManager)context.getSystemService(Context.CONNECTIVITY_SERVICE);
    NetworkRequest networkRequest = new NetworkRequest.Builder()
                                        .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                                        .build();

    NetworkCallback networkCallback = new NetworkCallback() {
      @Override
      public void onAvailable(@NonNull Network network) {
        envoyEngine.onNetworkAvailable();
      }

      @Override
      public void onCapabilitiesChanged(@NonNull Network network,
                                        @NonNull NetworkCapabilities networkCapabilities) {
        if (networkCapabilities.hasCapability(NetworkCapabilities.TRANSPORT_WIFI)) {
          envoyEngine.setPreferredNetwork(EnvoyNetworkType.WLAN);
        } else if (networkCapabilities.hasCapability(NetworkCapabilities.TRANSPORT_CELLULAR)) {
          envoyEngine.setPreferredNetwork(EnvoyNetworkType.WWAN);
        } else {
          envoyEngine.setPreferredNetwork(EnvoyNetworkType.GENERIC);
        }
      }

      @Override
      public void onLost(@NonNull Network network) {
        envoyEngine.onNetworkUnavailable();
      }
    };
    connectivityManager.registerNetworkCallback(networkRequest, networkCallback);
  }

  /** @returns The singleton instance of {@link AndroidNetworkMonitor}. */
  public static AndroidNetworkMonitor getInstance() {
    assert instance != null;
    return instance;
  }

  /**
   * Returns true if there is an internet connectivity.
   */
  public boolean isOnline() {
    NetworkCapabilities networkCapabilities =
        connectivityManager.getNetworkCapabilities(connectivityManager.getActiveNetwork());
    return networkCapabilities != null &&
        networkCapabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
  }

  /** Expose connectivityManager only for testing */
  @VisibleForTesting
  public ConnectivityManager getConnectivityManager() {
    return connectivityManager;
  }
}
