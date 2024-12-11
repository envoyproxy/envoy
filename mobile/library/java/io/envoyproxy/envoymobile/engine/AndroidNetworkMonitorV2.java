package io.envoyproxy.envoymobile.engine;

import io.envoyproxy.envoymobile.engine.types.EnvoyConnectionType;

import static android.net.ConnectivityManager.TYPE_VPN;
import static android.net.NetworkCapabilities.NET_CAPABILITY_INTERNET;
import static android.net.NetworkCapabilities.NET_CAPABILITY_NOT_VPN;
import static android.net.NetworkCapabilities.TRANSPORT_VPN;

import android.Manifest;
import android.annotation.SuppressLint;
import android.content.Context;
import android.content.pm.PackageManager;
import android.net.ConnectivityManager;
import android.net.ConnectivityManager.NetworkCallback;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.LinkProperties;
import android.net.NetworkInfo;
import android.net.NetworkRequest;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.telephony.TelephonyManager;
import android.util.Log;

import androidx.annotation.NonNull;
import androidx.annotation.VisibleForTesting;
import androidx.core.content.ContextCompat;

import java.io.IOException;
import java.net.Socket;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Arrays;

/** Immutable class representing the state of a device's network. */
class NetworkState {
  private final boolean mConnected;
  private final int mType;
  private final int mSubtype;
  private final boolean mIsMetered;
  // WIFI SSID of the connection on pre-Marshmallow, NetID starting with Marshmallow. Always
  // non-null (i.e. instead of null it'll be an empty string) to facilitate .equals().
  private final String mNetworkIdentifier;
  // Indicates if this network is using DNS-over-TLS.
  private final boolean mIsPrivateDnsActive;
  // Indicates the DNS-over-TLS server in use, if specified.
  private final String mPrivateDnsServerName;

  // Consolidate network type and subtype into one enum.
  public static EnvoyConnectionType convertToEnvoyConnectionType(int type, int subtype) {
    switch (type) {
    case ConnectivityManager.TYPE_ETHERNET:
      return EnvoyConnectionType.CONNECTION_ETHERNET;
    case ConnectivityManager.TYPE_WIFI:
      return EnvoyConnectionType.CONNECTION_WIFI;
    case ConnectivityManager.TYPE_WIMAX:
      return EnvoyConnectionType.CONNECTION_4G;
    case ConnectivityManager.TYPE_BLUETOOTH:
      return EnvoyConnectionType.CONNECTION_BLUETOOTH;
    case ConnectivityManager.TYPE_MOBILE:
    case ConnectivityManager.TYPE_MOBILE_DUN:
    case ConnectivityManager.TYPE_MOBILE_HIPRI:
      // Use information from TelephonyManager to classify the connection.
      switch (subtype) {
      case TelephonyManager.NETWORK_TYPE_GPRS:
      case TelephonyManager.NETWORK_TYPE_EDGE:
      case TelephonyManager.NETWORK_TYPE_CDMA:
      case TelephonyManager.NETWORK_TYPE_1xRTT:
      case TelephonyManager.NETWORK_TYPE_IDEN:
        return EnvoyConnectionType.CONNECTION_2G;
      case TelephonyManager.NETWORK_TYPE_UMTS:
      case TelephonyManager.NETWORK_TYPE_EVDO_0:
      case TelephonyManager.NETWORK_TYPE_EVDO_A:
      case TelephonyManager.NETWORK_TYPE_HSDPA:
      case TelephonyManager.NETWORK_TYPE_HSUPA:
      case TelephonyManager.NETWORK_TYPE_HSPA:
      case TelephonyManager.NETWORK_TYPE_EVDO_B:
      case TelephonyManager.NETWORK_TYPE_EHRPD:
      case TelephonyManager.NETWORK_TYPE_HSPAP:
        return EnvoyConnectionType.CONNECTION_3G;
      case TelephonyManager.NETWORK_TYPE_LTE:
        return EnvoyConnectionType.CONNECTION_4G;
      case TelephonyManager.NETWORK_TYPE_NR:
        return EnvoyConnectionType.CONNECTION_5G;
      default:
        return EnvoyConnectionType.CONNECTION_UNKNOWN;
      }
    default:
      return EnvoyConnectionType.CONNECTION_UNKNOWN;
    }
  }
  public NetworkState(boolean connected, int type, int subtype, boolean isMetered,
                      String networkIdentifier, boolean isPrivateDnsActive,
                      String privateDnsServerName) {
    mConnected = connected;
    mType = type;
    mSubtype = subtype;
    mIsMetered = isMetered;
    mNetworkIdentifier = networkIdentifier == null ? "" : networkIdentifier;
    mIsPrivateDnsActive = isPrivateDnsActive;
    mPrivateDnsServerName = privateDnsServerName == null ? "" : privateDnsServerName;
  }

  public boolean isConnected() { return mConnected; }

  public int getNetworkType() { return mType; }

  public boolean isMetered() { return mIsMetered; }

  public int getNetworkSubType() { return mSubtype; }

  // Always non-null to facilitate .equals().
  public String getNetworkIdentifier() { return mNetworkIdentifier; }

  /** Returns the connection type for the given NetworkState. */
  public EnvoyConnectionType getEnvoyConnectionType() {
    if (!isConnected()) {
      return EnvoyConnectionType.CONNECTION_NONE;
    }
    return convertToEnvoyConnectionType(mType, mSubtype);
  }

  /** Returns boolean indicating if this network uses DNS-over-TLS. */
  public boolean isPrivateDnsActive() { return mIsPrivateDnsActive; }

  /** Returns the DNS-over-TLS server in use, if specified. */
  public String getPrivateDnsServerName() { return mPrivateDnsServerName; }
}

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
 * <code>EnvoyEngine::onDefaultNetworkChangedV2</code>.</li>
 * </ul>
 */
public class AndroidNetworkMonitor {
  private static final String TAG = AndroidNetworkMonitor.class.getSimpleName();
  private static final String PERMISSION_DENIED_STATS_ELEMENT =
      "android_permissions.network_state_denied";
  private static volatile AndroidNetworkMonitor mInstance = null;
  private ConnectivityManager mConnectivityManager;
  private EnvoyEngine mEnvoyEngine;
  // {@link Looper} for the thread this object lives on.
  private Looper mLooper;
  // Used to post to the thread this object lives on.
  private Handler mHandler;
  // Starting with Android O, used to detect changes in default network.
  private NetworkCallback mDefaultNetworkCallback;
  // Will be null if ConnectivityManager.registerNetworkCallback() ever fails.
  private MyNetworkCallback mNetworkCallback;
  private NetworkRequest mNetworkRequest;
  private NetworkState mNetworkState;

  public static void load(Context context, EnvoyEngine envoyEngine) {
    if (mInstance != null) {
      return;
    }

    synchronized (AndroidNetworkMonitor.class) {
      if (mInstance != null) {
        return;
      }
      mInstance = new AndroidNetworkMonitor(context, envoyEngine);
    }
  }

  /**
   * Sets the {@link AndroidNetworkMonitor} singleton mInstance to null, so that it can be recreated
   * when a new EnvoyEngine is created.
   */
  @VisibleForTesting
  public static void shutdown() {
    mInstance = null;
  }

  /** @returns The singleton mInstance of {@link AndroidNetworkMonitor}. */
  public static AndroidNetworkMonitor getInstance() {
    assert mInstance != null;
    return mInstance;
  }

  /**
   * Returns true if there is an internet connectivity.
   */
  public boolean isOnline() {
    NetworkCapabilities networkCapabilities =
        mConnectivityManager.getNetworkCapabilities(mConnectivityManager.getActiveNetwork());
    return networkCapabilities != null &&
        networkCapabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
  }

  /** Expose connectivityManager only for testing */
  @VisibleForTesting
  public ConnectivityManager getConnectivityManager() {
    return mConnectivityManager;
  }

  private boolean onThread() { return mLooper == Looper.myLooper(); }

  private void runOnThread(Runnable r) {
    if (onThread()) {
      r.run();
    } else {
      // Once execution begins on the correct thread, make sure unregister() hasn't
      // been called in the mean time.
      mHandler.post(() -> { r.run(); });
    }
  }

  private static boolean vpnAccessible(Network network) {
    // Determine if the VPN applies to the current user by seeing if a socket can be bound
    // to the VPN.
    Socket s = new Socket();
    try {
      // Avoid using network.getSocketFactory().createSocket() because it leaks.
      // https://crbug.com/805424
      network.bindSocket(s);
    } catch (IOException e) {
      // Failed to bind so this VPN isn't for the current user to use.
      return false;
    } finally {
      try {
        s.close();
      } catch (IOException e) {
        // Not worth taking action on a failed close.
      }
    }
    return true;
  }

  /**
   * Returns all connected networks that are useful and accessible to Chrome.
   * @param ignoreNetwork ignore this network as if it is not connected.
   */
  private Network[] getAllNetworksFiltered(Network ignoreNetwork) {
    Network[] networks = mConnectivityManager.getAllNetworks();
    // Very rarely this API inexplicably returns {@code null}, crbug.com/721116.
    networks = networks == null ? new Network[0] : networks;
    // Whittle down |networks| into just the list of networks useful to us.
    int filteredIndex = 0;
    for (Network network : networks) {
      if (network.equals(ignoreNetwork)) {
        continue;
      }
      final NetworkCapabilities capabilities = mConnectivityManager.getNetworkCapabilities(network);
      if (capabilities == null || !capabilities.hasCapability(NET_CAPABILITY_INTERNET)) {
        continue;
      }
      if (capabilities.hasTransport(TRANSPORT_VPN)) {
        // If we can access the VPN then...
        if (vpnAccessible(network)) {
          // ...we cannot access any other network, so return just the VPN.
          return new Network[] {network};
        } else {
          // ...otherwise ignore it as we cannot use it.
          continue;
        }
      }
      networks[filteredIndex++] = network;
    }
    return Arrays.copyOf(networks, filteredIndex);
  }

  /**
   * Returns network handle of device's current default connected network used for
   * communication.
   * Returns -1 when not implemented.
   */
  public long getDefaultNetId() {
    Network network = getDefaultNetwork();
    return network == null ? -1 : network.getNetworkHandle();
  }

  /** Returns the current default {@link Network}, or {@code null} if disconnected. */
  private Network getDefaultNetwork() {
    Network defaultNetwork = null;
    defaultNetwork = mConnectivityManager.getActiveNetwork();
    if (defaultNetwork != null) {
      return defaultNetwork;
    }
    // getActiveNetwork() returning null cannot be trusted to indicate disconnected
    // as it suffers from https://crbug.com/677365.
    // Check another API to return the NetworkInfo for the default network. To
    // determine the default network one can find the network with
    // type matching that of the default network.
    final NetworkInfo defaultNetworkInfo = mConnectivityManager.getActiveNetworkInfo();
    if (defaultNetworkInfo == null) {
      return null;
    }
    final Network[] networks = getAllNetworksFiltered(null);
    for (Network network : networks) {
      final NetworkInfo networkInfo = getRawNetworkInfo(network);
      if (networkInfo != null &&
          (networkInfo.getType() == defaultNetworkInfo.getType()
           // getActiveNetworkInfo() will not return TYPE_VPN types due to
           // https://android.googlesource.com/platform/frameworks/base/+/d6a7980d
           // so networkInfo.getType() can't be matched against
           // defaultNetworkInfo.getType() but networkInfo.getType() should
           // be TYPE_VPN. In the case of a VPN, getAllNetworks() will have
           // returned just this VPN if it applies.
           || networkInfo.getType() == TYPE_VPN)) {
        // Android 10+ devices occasionally return multiple networks
        // of the same type that are stuck in the CONNECTING state.
        // Now that Java asserts are enabled, ignore these zombie
        // networks here to avoid hitting the assert below. crbug.com/1361170
        if (defaultNetwork != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
          // If `network` is CONNECTING, ignore it.
          if (networkInfo.getDetailedState() == NetworkInfo.DetailedState.CONNECTING) {
            continue;
          }
          // If `defaultNetwork` is CONNECTING, ignore it.
          NetworkInfo prevDefaultNetworkInfo = getRawNetworkInfo(defaultNetwork);
          if (prevDefaultNetworkInfo != null &&
              prevDefaultNetworkInfo.getDetailedState() == NetworkInfo.DetailedState.CONNECTING) {
            defaultNetwork = null;
          }
        }
        if (defaultNetwork != null) {
          // TODO(crbug.com/40060873): Investigate why there are multiple
          // connected networks.
          Log.e(TAG, "There should not be multiple connected "
                         + "networks of the same type. At least as of Android "
                         + "Marshmallow this is not supported. If this becomes "
                         + "supported this assertion may trigger.");
        }
        defaultNetwork = network;
      }
    }
    return defaultNetwork;
  }

  /**
   * @param networkInfo The NetworkInfo for the active network.
   * @return the info of the network that is available to this app.
   */
  private NetworkInfo processActiveNetworkInfo(NetworkInfo networkInfo) {
    if (networkInfo == null) {
      return null;
    }

    if (networkInfo.isConnected()) {
      return networkInfo;
    }

    if (networkInfo.getDetailedState() != NetworkInfo.DetailedState.BLOCKED) {
      // Network state is not blocked which implies that network access is
      // unavailable (not just blocked to this app).
      return null;
    }

    // If |networkInfo| is BLOCKED, but the app is in the foreground, then it's likely that
    // Android hasn't finished updating the network access permissions as BLOCKED is only
    // meant for apps in the background. See https://crbug.com/677365 for more details.
    // TODO(danzh) check whether application is in the foreground or not.
    return null;
    /*
    // fork
    https://source.chromium.org/chromium/chromium/src/+/main:base/android/java/src/org/chromium/base/ApplicationStatus.java
    if (ApplicationStatus.getStateForApplication()
            != ApplicationState.HAS_RUNNING_ACTIVITIES) {
        // The app is not in the foreground.
        return null;
    }

    return networkInfo;
    */
  }

  /**
   * Returns connection type and status information about the current
   * default network.
   */
  NetworkState getNetworkStateOfDefaultNetwork() {
    Network network = getDefaultNetwork();
    NetworkInfo networkInfo = getNetworkInfo(network);
    networkInfo = processActiveNetworkInfo(networkInfo);
    if (networkInfo == null) {
      return new NetworkState(false, -1, -1, false, null, false, "");
    }

    assert network != null;
    final NetworkCapabilities capabilities = mConnectivityManager.getNetworkCapabilities(network);
    boolean isMetered =
        (capabilities != null &&
         !capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED));
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
      try {
        LinkProperties linkProperties = mConnectivityManager.getLinkProperties(network);
        if (linkProperties != null) {
          return new NetworkState(true, networkInfo.getType(), networkInfo.getSubtype(), isMetered,
                                  String.valueOf(network.getNetworkHandle()),
                                  linkProperties.isPrivateDnsActive(),
                                  linkProperties.getPrivateDnsServerName());
        }
      } catch (RuntimeException e) {
      }
    }
    return new NetworkState(true, networkInfo.getType(), networkInfo.getSubtype(), isMetered,
                            String.valueOf(network.getNetworkHandle()), false, "");
  }

  /**
   * Fetches NetworkInfo for |network|. Does not account for underlying VPNs; see
   * getNetworkInfo(Network) for a method that does.
   */
  private NetworkInfo getRawNetworkInfo(Network network) {
    try {
      return mConnectivityManager.getNetworkInfo(network);
    } catch (NullPointerException firstException) {
      // Rarely this unexpectedly throws. Retry or just return {@code null} if it fails.
      try {
        return mConnectivityManager.getNetworkInfo(network);
      } catch (NullPointerException secondException) {
        return null;
      }
    }
  }

  /** Fetches NetworkInfo for |network|. */
  private NetworkInfo getNetworkInfo(Network network) {
    NetworkInfo networkInfo = getRawNetworkInfo(network);
    if (networkInfo != null && networkInfo.getType() == TYPE_VPN) {
      // When a VPN is in place the underlying network type can be queried via
      // getActiveNetworkInfo() thanks to
      // https://android.googlesource.com/platform/frameworks/base/+/d6a7980d
      networkInfo = mConnectivityManager.getActiveNetworkInfo();
    }
    return networkInfo;
  }

  private EnvoyConnectionType getEnvoyConnectionType(Network network) {
    NetworkInfo networkInfo = getNetworkInfo(network);
    if (networkInfo != null && networkInfo.isConnected()) {
      return NetworkState.convertToEnvoyConnectionType(networkInfo.getType(),
                                                       networkInfo.getSubtype());
    }
    return EnvoyConnectionType.CONNECTION_NONE;
  }

  @VisibleForTesting
  class DefaultNetworkCallback extends NetworkCallback {
    LinkProperties mLinkProperties;
    NetworkCapabilities mNetworkCapabilities;

    @Override
    public void onAvailable(@NonNull Network network) {
      // Clear accumulated state and wait for new state to be received.
      // Android guarantees we receive onLinkPropertiesChanged and
      // onNetworkCapabilities calls after onAvailable:
      // https://developer.android.com/reference/android/net/ConnectivityManager.NetworkCallback#onCapabilitiesChanged(android.net.Network,%20android.net.NetworkCapabilities)
      // so the call to onNetworkStateChangedTo() is done when we have received the
      // LinkProperties and NetworkCapabilities.
      mLinkProperties = null;
      mNetworkCapabilities = null;
      mEnvoyEngine.onDefaultNetworkAvailable();
    }

    @Override
    public void onLost(@NonNull Network network) {
      mLinkProperties = null;
      mNetworkCapabilities = null;
      onNetworkStateChangedTo(new NetworkState(false, -1, -1, false, null, false, ""));
      mEnvoyEngine.onDefaultNetworkUnavailable();
    }

    // LinkProperties changes include enabling/disabling DNS-over-TLS.
    @Override
    public void onLinkPropertiesChanged(Network network, LinkProperties linkProperties) {
      mLinkProperties = linkProperties;
      if (mLinkProperties != null && mNetworkCapabilities != null) {
        onNetworkStateChangedTo(createNetworkState(network));
      }
    }

    @SuppressLint("WrongConstant")
    // CapabilitiesChanged includes cellular connections switching in and out of SUSPENDED.
    @Override
    public void onCapabilitiesChanged(@NonNull Network network,
                                      @NonNull NetworkCapabilities networkCapabilities) {
      mNetworkCapabilities = networkCapabilities;
      // onCapabilities is guaranteed to be called immediately after `onAvailable`
      // starting with Android O, so this logic may not work on older Android versions.
      // https://developer.android.com/reference/android/net/ConnectivityManager.NetworkCallback#onCapabilitiesChanged(android.net.Network,%20android.net.NetworkCapabilities)
      if (mLinkProperties != null && mNetworkCapabilities != null) {
        onNetworkStateChangedTo(createNetworkState(network));
      }
    }

    // Calculate the given NetworkState. Unlike getNetworkStateOfDefaultNetwork(), this method
    // avoids calling synchronous ConnectivityManager methods which is prohibited inside
    // NetworkCallbacks see "Do NOT call" here:
    // https://developer.android.com/reference/android/net/ConnectivityManager.NetworkCallback#onAvailable(android.net.Network)
    private NetworkState createNetworkState(Network network) {
      // Initialize to unknown values then extract more accurate info
      int type = -1;
      int subtype = -1;
      if (mNetworkCapabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) ||
          mNetworkCapabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI_AWARE)) {
        type = ConnectivityManager.TYPE_WIFI;
      } else if (mNetworkCapabilities.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR)) {
        type = ConnectivityManager.TYPE_MOBILE;
        // To get the subtype we need to make a synchronous ConnectivityManager call
        // unfortunately. It's recommended to use TelephonyManager.getDataNetworkType()
        // but that requires an additional permission. Worst case this might be inaccurate
        // but getting the correct subtype is much much less important than getting the
        // correct type. Incorrect type could make Envoy Mobile behave like it's offline,
        // incorrect subtype will just make cellular bandwidth estimates incorrect.
        NetworkInfo networkInfo = getRawNetworkInfo(network);
        if (networkInfo != null) {
          subtype = networkInfo.getSubtype();
        }
      } else if (mNetworkCapabilities.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)) {
        type = ConnectivityManager.TYPE_ETHERNET;
      } else if (mNetworkCapabilities.hasTransport(NetworkCapabilities.TRANSPORT_BLUETOOTH)) {
        type = ConnectivityManager.TYPE_BLUETOOTH;
      } else if (mNetworkCapabilities.hasTransport(NetworkCapabilities.TRANSPORT_VPN)) {
        // Make a synchronous ConnectivityManager call to find underlying network which has a more
        // useful transport type. crbug.com/1208022
        NetworkInfo networkInfo = getNetworkInfo(network);
        type = networkInfo != null ? networkInfo.getType() : ConnectivityManager.TYPE_VPN;
      }
      boolean isMetered =
          !mNetworkCapabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED);
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
        return new NetworkState(
            true, type, subtype, isMetered,
            String.valueOf(network.getNetworkHandle()), // NetworkHandle is supported on Android
                                                        // version M and above
            mLinkProperties.isPrivateDnsActive(), mLinkProperties.getPrivateDnsServerName());
      }
      return new NetworkState(
          true, type, subtype, isMetered,
          String.valueOf(network.getNetworkHandle()), // NetworkHandle is supported on Android
                                                      // version M and above
          false, "");
    }
  }

  // This class gets called back by ConnectivityManager whenever networks come
  // and go. It gets called back on a special handler thread
  // ConnectivityManager creates for making the callbacks. The callbacks in
  // turn post to mLooper where mObserver lives.
  private class MyNetworkCallback extends NetworkCallback {
    // If non-null, this indicates a VPN is in place for the current user, and no other
    // networks are accessible.
    private Network mVpnInPlace;

    // Initialize mVpnInPlace.
    void initializeVpnInPlace() {
      final Network[] networks = getAllNetworksFiltered(null);
      mVpnInPlace = null;
      // If the filtered list of networks contains just a VPN, then that VPN is in place.
      if (networks.length == 1) {
        final NetworkCapabilities capabilities =
            mConnectivityManager.getNetworkCapabilities(networks[0]);
        if (capabilities != null && capabilities.hasTransport(TRANSPORT_VPN)) {
          mVpnInPlace = networks[0];
        }
      }
    }

    /**
     * Should changes to network {@code network} be ignored due to a VPN being in place
     * and blocking direct access to {@code network}?
     * @param network Network to possibly consider ignoring changes to.
     */
    private boolean ignoreNetworkDueToVpn(Network network) {
      return mVpnInPlace != null && !mVpnInPlace.equals(network);
    }

    /**
     * Should changes to connected network {@code network} be ignored?
     *
     * @param network Network to possibly consider ignoring changes to.
     * @param capabilities {@code NetworkCapabilities} for {@code network} if known, otherwise
     *     {@code null}.
     * @return {@code true} when either: {@code network} is an inaccessible VPN, or has already
     *     disconnected.
     */
    private boolean ignoreConnectedInaccessibleVpn(Network network,
                                                   NetworkCapabilities capabilities) {
      // Ignore inaccessible VPNs as they don't apply to Envoy Mobile.
      return capabilities == null ||
          (capabilities.hasTransport(TRANSPORT_VPN) && !vpnAccessible(network));
    }

    /**
     * Should changes to connected network {@code network} be ignored?
     * @param network Network to possible consider ignoring changes to.
     * @param capabilities {@code NetworkCapabilities} for {@code network} if known, otherwise
     *         {@code null}.
     */
    private boolean ignoreConnectedNetwork(Network network, NetworkCapabilities capabilities) {
      return ignoreNetworkDueToVpn(network) ||
          ignoreConnectedInaccessibleVpn(network, capabilities);
    }

    @Override
    public void onAvailable(Network network) {
      final NetworkCapabilities capabilities = mConnectivityManager.getNetworkCapabilities(network);
      if (ignoreConnectedNetwork(network, capabilities)) {
        return;
      }
      final boolean makeVpnDefault = capabilities.hasTransport(TRANSPORT_VPN) &&
                                     // Only make the VPN the default if it isn't already.
                                     (mVpnInPlace == null || !network.equals(mVpnInPlace));
      if (makeVpnDefault) {
        mVpnInPlace = network;
      }
      final long netId = network.getNetworkHandle();
      final EnvoyConnectionType connectionType = getEnvoyConnectionType(network);
      runOnThread(new Runnable() {
        @Override
        public void run() {
          mEnvoyEngine.onNetworkConnect(connectionType, netId);
          if (makeVpnDefault) {
            // Make VPN the default network.
            mEnvoyEngine.onDefaultNetworkChangedV2(connectionType, netId);
            // Purge all other networks as they're inaccessible to Chrome
            // now.
            mEnvoyEngine.purgeActiveNetworkList(new long[] {netId});
          }
        }
      });
    }

    @Override
    public void onCapabilitiesChanged(Network network, NetworkCapabilities networkCapabilities) {
      if (ignoreConnectedNetwork(network, networkCapabilities)) {
        return;
      }
      // A capabilities change may indicate the ConnectionType has changed,
      // so forward the new ConnectionType along to observer.
      final long netId = network.getNetworkHandle();
      final EnvoyConnectionType connectionType = getEnvoyConnectionType(network);
      runOnThread(new Runnable() {
        @Override
        public void run() {
          mEnvoyEngine.onNetworkConnect(connectionType, netId);
        }
      });
    }

    @Override
    public void onLost(final Network network) {
      if (ignoreNetworkDueToVpn(network)) {
        return;
      }
      runOnThread(new Runnable() {
        @Override
        public void run() {
          mEnvoyEngine.onNetworkDisconnect(network.getNetworkHandle());
        }
      });
      // If the VPN is going away, signal that other networks that were
      // previously hidden by ignoreNetworkDueToVpn() are now available for use, now that
      // this user's traffic is not forced into the VPN.
      if (mVpnInPlace != null) {
        assert network.equals(mVpnInPlace);
        mVpnInPlace = null;
        for (Network newNetwork : getAllNetworksFiltered(network)) {
          onAvailable(newNetwork);
        }

        mNetworkState = getNetworkStateOfDefaultNetwork();
        final EnvoyConnectionType newConnectionType = mNetworkState.getEnvoyConnectionType();
        runOnThread(new Runnable() {
          @Override
          public void run() {
            mEnvoyEngine.onDefaultNetworkChangedV2(newConnectionType, getDefaultNetId());
          }
        });
      }
    }
  }

  private void onNetworkStateChangedTo(NetworkState networkState) {
    if (networkState.getEnvoyConnectionType() != mNetworkState.getEnvoyConnectionType() ||
        !networkState.getNetworkIdentifier().equals(mNetworkState.getNetworkIdentifier()) ||
        networkState.isPrivateDnsActive() != mNetworkState.isPrivateDnsActive() ||
        !networkState.getPrivateDnsServerName().equals(mNetworkState.getPrivateDnsServerName())) {
      mEnvoyEngine.onDefaultNetworkChangedV2(networkState.getEnvoyConnectionType(),
                                             getDefaultNetId());
    }
    mNetworkState = networkState;
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

    mEnvoyEngine = envoyEngine;
    mConnectivityManager =
        (ConnectivityManager)context.getSystemService(Context.CONNECTIVITY_SERVICE);
    mLooper = Looper.myLooper();
    mHandler = new Handler(mLooper);
    onNetworkStateChangedTo(getNetworkStateOfDefaultNetwork());
    try {
      mDefaultNetworkCallback = new DefaultNetworkCallback();
      mConnectivityManager.registerDefaultNetworkCallback(mDefaultNetworkCallback, mHandler);
    } catch (RuntimeException e) {
      mDefaultNetworkCallback = null;
    }

    mNetworkCallback = new MyNetworkCallback();
    mNetworkRequest = new NetworkRequest.Builder()
                          .addCapability(NET_CAPABILITY_INTERNET)
                          // Need to hear about VPNs too.
                          .removeCapability(NET_CAPABILITY_NOT_VPN)
                          .build();
    mNetworkCallback.initializeVpnInPlace();
    try {
      mConnectivityManager.registerNetworkCallback(mNetworkRequest, mNetworkCallback, mHandler);
    } catch (RuntimeException e) {
      // If Android thinks this app has used up all available NetworkRequests, don't
      // bother trying to register any more callbacks as Android will still think
      // all available NetworkRequests are used up and fail again needlessly.
      // Also don't bother unregistering as this call didn't actually register.
      // See crbug.com/791025 for more info.
      mNetworkCallback = null;
    }
    if (mNetworkCallback != null) {
      // registerNetworkCallback() will rematch the NetworkRequest
      // against active networks, so a cached list of active networks
      // will be repopulated immediately after this. However we need to
      // purge any cached networks as they may have been disconnected
      // while mNetworkCallback was unregistered.
      final Network[] networks = getAllNetworksFiltered(null);
      // Convert Networks to NetIDs.
      final long[] netIds = new long[networks.length];
      for (int i = 0; i < networks.length; i++) {
        netIds[i] = networks[i].getNetworkHandle();
      }
      mEnvoyEngine.purgeActiveNetworkList(netIds);
    }
  }
}
