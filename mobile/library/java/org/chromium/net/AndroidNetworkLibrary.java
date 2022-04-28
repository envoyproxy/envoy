package org.chromium.net;

import android.Manifest;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.net.ConnectivityManager;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkInfo;
import android.net.TrafficStats;
import android.net.TransportInfo;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Build.VERSION_CODES;
import android.os.ParcelFileDescriptor;
import android.os.Process;
import android.telephony.TelephonyManager;
import android.util.Log;

import androidx.annotation.RequiresApi;
import androidx.annotation.VisibleForTesting;

import java.io.FileDescriptor;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.net.Socket;
import java.net.SocketAddress;
import java.net.SocketException;
import java.net.SocketImpl;
import java.net.URLConnection;
import java.security.KeyStoreException;
import java.security.NoSuchAlgorithmException;
import java.security.cert.CertificateException;
import java.util.Enumeration;
import java.util.List;

/**
 * This class implements net utilities required by the net component.
 */
public final class AndroidNetworkLibrary {
  private static final String TAG = "AndroidNetworkLibrary";

  /**
   * Validate the server's certificate chain is trusted. Note that the caller
   * must still verify the name matches that of the leaf certificate.
   *
   * @param certChain The ASN.1 DER encoded bytes for certificates.
   * @param authType The key exchange algorithm name (e.g. RSA).
   * @param host The hostname of the server.
   * @return Android certificate verification result code.
   */
  // TODO(stefanoduo): Hook envoy-mobile JNI.
  //@CalledByNative
  public static AndroidCertVerifyResult verifyServerCertificates(byte[][] certChain,
                                                                 String authType, String host) {
    try {
      return X509Util.verifyServerCertificates(certChain, authType, host);
    } catch (KeyStoreException e) {
      return new AndroidCertVerifyResult(CertVerifyStatusAndroid.FAILED);
    } catch (NoSuchAlgorithmException e) {
      return new AndroidCertVerifyResult(CertVerifyStatusAndroid.FAILED);
    } catch (IllegalArgumentException e) {
      return new AndroidCertVerifyResult(CertVerifyStatusAndroid.FAILED);
    }
  }

  /**
   * Adds a test root certificate to the local trust store.
   * @param rootCert DER encoded bytes of the certificate.
   */
  // TODO(stefanoduo): Hook envoy-mobile JNI.
  //@CalledByNativeUnchecked
  public static void addTestRootCertificate(byte[] rootCert)
      throws CertificateException, KeyStoreException, NoSuchAlgorithmException {
    X509Util.addTestRootCertificate(rootCert);
  }

  /**
   * Removes all test root certificates added by |addTestRootCertificate| calls from the local
   * trust store.
   */
  // TODO(stefanoduo): Hook envoy-mobile JNI.
  //@CalledByNativeUnchecked
  public static void clearTestRootCertificates()
      throws NoSuchAlgorithmException, CertificateException, KeyStoreException {
    X509Util.clearTestRootCertificates();
  }
}
