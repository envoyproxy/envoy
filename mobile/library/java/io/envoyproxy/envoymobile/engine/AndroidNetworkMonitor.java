package io.envoyproxy.envoymobile.engine;

import io.envoyproxy.envoymobile.engine.types.EnvoyNetworkType;

import android.Manifest;
import android.annotation.SuppressLint;
import android.content.Context;
import android.content.pm.PackageManager;
import android.net.ConnectivityManager;
import android.net.ConnectivityManager.NetworkCallback;
import android.net.Network;
import android.net.NetworkCapabilities;

import androidx.annotation.NonNull;
import androidx.annotation.VisibleForTesting;
import androidx.core.content.ContextCompat;

import java.util.Collections;

/**
 * This class does the following.
 * <ul>
 * <li>When the internet is available: call the
 * <code>InternalEngine::onDefaultNetworkAvailable</code> callback.</li>
 *
 * <li>When the internet is not available: call the
 * <code>InternalEngine::onDefaultNetworkUnavailable</code> callback.</li>
 *
 * <li>When the network is changed: call the
 * <code>EnvoyEngine::onDefaultNetworkChanged</code>.</li>
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

  @VisibleForTesting
  static class DefaultNetworkCallback extends NetworkCallback {
    private static final int[] TRANSPORT_TYPES = new int[] {
        NetworkCapabilities.TRANSPORT_CELLULAR,  NetworkCapabilities.TRANSPORT_WIFI,
        NetworkCapabilities.TRANSPORT_BLUETOOTH, NetworkCapabilities.TRANSPORT_ETHERNET,
        NetworkCapabilities.TRANSPORT_VPN,       NetworkCapabilities.TRANSPORT_WIFI_AWARE,
    };
    private static final int EMPTY_TRANSPORT_TYPE = -1;

    private final EnvoyEngine envoyEngine;
    @VisibleForTesting int transportType = EMPTY_TRANSPORT_TYPE;

    DefaultNetworkCallback(EnvoyEngine envoyEngine) { this.envoyEngine = envoyEngine; }

    @Override
    public void onAvailable(@NonNull Network network) {
      envoyEngine.onDefaultNetworkAvailable();
    }

    @SuppressLint("WrongConstant")
    @Override
    public void onCapabilitiesChanged(@NonNull Network network,
                                      @NonNull NetworkCapabilities networkCapabilities) {
      // `onCapabilities` is guaranteed to be called immediately after `onAvailable`
      // starting with Android O, so this logic may not work on older Android versions.
      // https://developer.android.com/reference/android/net/ConnectivityManager.NetworkCallback#onCapabilitiesChanged(android.net.Network,%20android.net.NetworkCapabilities)
      if (transportType == EMPTY_TRANSPORT_TYPE) {
        // The network was lost previously, see `onLost`.
        onDefaultNetworkChanged(networkCapabilities);
      } else {
        // Only call the `onDefaultNetworkChanged` callback when there is a change in the
        // transport type.
        if (!networkCapabilities.hasTransport(transportType)) {
          onDefaultNetworkChanged(networkCapabilities);
        }
      }
    }

    @Override
    public void onLost(@NonNull Network network) {
      envoyEngine.onDefaultNetworkUnavailable();
      transportType = EMPTY_TRANSPORT_TYPE;
    }

    private static int getTransportType(NetworkCapabilities networkCapabilities) {
      for (int type : TRANSPORT_TYPES) {
        if (networkCapabilities.hasTransport(type)) {
          return type;
        }
      }
      return EMPTY_TRANSPORT_TYPE;
    }

    private void onDefaultNetworkChanged(NetworkCapabilities networkCapabilities) {
      if (networkCapabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)) {
        if (networkCapabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) ||
            networkCapabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI_AWARE)) {
          envoyEngine.onDefaultNetworkChanged(EnvoyNetworkType.WLAN);
        } else if (networkCapabilities.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR)) {
          envoyEngine.onDefaultNetworkChanged(EnvoyNetworkType.WWAN);
        } else {
          envoyEngine.onDefaultNetworkChanged(EnvoyNetworkType.GENERIC);
        }
      }
      transportType = getTransportType(networkCapabilities);
    }
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
    connectivityManager.registerDefaultNetworkCallback(new DefaultNetworkCallback(envoyEngine));
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
