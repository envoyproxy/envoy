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
import android.net.NetworkInfo;
import android.os.Build;
import android.telephony.TelephonyManager;

import androidx.annotation.NonNull;
import androidx.annotation.VisibleForTesting;
import androidx.core.content.ContextCompat;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

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

  public static void load(Context context, EnvoyEngine envoyEngine, boolean useNetworkChangeEvent) {
    if (instance != null) {
      return;
    }

    synchronized (AndroidNetworkMonitor.class) {
      if (instance != null) {
        return;
      }
      instance = new AndroidNetworkMonitor(context, envoyEngine, useNetworkChangeEvent);
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

    private final EnvoyEngine envoyEngine;
    private final ConnectivityManager connectivityManager;
    private final boolean useNetworkChangeEvent;
    @VisibleForTesting List<Integer> previousTransportTypes = new ArrayList<>();

    DefaultNetworkCallback(EnvoyEngine envoyEngine, ConnectivityManager connectivityManager,
                           boolean useNetworkChangeEvent) {
      this.envoyEngine = envoyEngine;
      this.connectivityManager = connectivityManager;
      this.useNetworkChangeEvent = useNetworkChangeEvent;
    }

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
      if (previousTransportTypes.isEmpty() || useNetworkChangeEvent) {
        // The network was lost previously, see `onLost`.
        onDefaultNetworkChanged(network, networkCapabilities);
      } else {
        // Only call the `onDefaultNetworkChanged` callback when there is any changes in the
        // transport types.
        List<Integer> currentTransportTypes = getTransportTypes(networkCapabilities);
        if (!previousTransportTypes.equals(currentTransportTypes)) {
          onDefaultNetworkChanged(network, networkCapabilities);
        }
      }
    }

    @Override
    public void onLost(@NonNull Network network) {
      envoyEngine.onDefaultNetworkUnavailable();
      previousTransportTypes.clear();
    }

    private static List<Integer> getTransportTypes(NetworkCapabilities networkCapabilities) {
      List<Integer> transportTypes = new ArrayList<>();
      for (int type : TRANSPORT_TYPES) {
        if (networkCapabilities.hasTransport(type)) {
          transportTypes.add(type);
        }
      }
      return transportTypes;
    }

    private void onDefaultNetworkChanged(Network network, NetworkCapabilities networkCapabilities) {
      if (networkCapabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)) {
        int networkType = 0;
        if (networkCapabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) ||
            networkCapabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI_AWARE)) {
          networkType |= EnvoyNetworkType.WLAN.getValue();
        } else if (networkCapabilities.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR)) {
          networkType |= EnvoyNetworkType.WWAN.getValue();
          NetworkInfo networkInfo = connectivityManager.getNetworkInfo(network);
          if (networkInfo != null) {
            int subtype = networkInfo.getSubtype();
            switch (subtype) {
            case TelephonyManager.NETWORK_TYPE_GPRS:
            case TelephonyManager.NETWORK_TYPE_EDGE:
            case TelephonyManager.NETWORK_TYPE_CDMA:
            case TelephonyManager.NETWORK_TYPE_1xRTT:
            case TelephonyManager.NETWORK_TYPE_IDEN:
            case TelephonyManager.NETWORK_TYPE_GSM:
              networkType |= EnvoyNetworkType.WWAN_2G.getValue();
              break;
            case TelephonyManager.NETWORK_TYPE_UMTS:
            case TelephonyManager.NETWORK_TYPE_EVDO_0:
            case TelephonyManager.NETWORK_TYPE_EVDO_A:
            case TelephonyManager.NETWORK_TYPE_HSDPA:
            case TelephonyManager.NETWORK_TYPE_HSUPA:
            case TelephonyManager.NETWORK_TYPE_HSPA:
            case TelephonyManager.NETWORK_TYPE_EVDO_B:
            case TelephonyManager.NETWORK_TYPE_EHRPD:
            case TelephonyManager.NETWORK_TYPE_HSPAP:
            case TelephonyManager.NETWORK_TYPE_TD_SCDMA:
              networkType |= EnvoyNetworkType.WWAN_3G.getValue();
              break;
            case TelephonyManager.NETWORK_TYPE_LTE:
            case TelephonyManager.NETWORK_TYPE_IWLAN:
              networkType |= EnvoyNetworkType.WWAN_4G.getValue();
              break;
            case TelephonyManager.NETWORK_TYPE_NR:
              networkType |= EnvoyNetworkType.WWAN_5G.getValue();
              break;
            default:
              break;
            }
          }
        } else {
          networkType |= EnvoyNetworkType.GENERIC.getValue();
        }
        // A network can be both VPN and another type, so we need to check for VPN separately.
        if (networkCapabilities.hasTransport(NetworkCapabilities.TRANSPORT_VPN)) {
          networkType |= EnvoyNetworkType.GENERIC.getValue();
        }
        if (useNetworkChangeEvent) {
          envoyEngine.onDefaultNetworkChangeEvent(networkType);
        } else {
          envoyEngine.onDefaultNetworkChanged(networkType);
        }
      }
      previousTransportTypes = getTransportTypes(networkCapabilities);
    }
  }

  private AndroidNetworkMonitor(Context context, EnvoyEngine envoyEngine,
                                boolean useNetworkChangeEvent) {
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
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
      connectivityManager.registerDefaultNetworkCallback(
          new DefaultNetworkCallback(envoyEngine, connectivityManager, useNetworkChangeEvent));
    }
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
