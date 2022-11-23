package org.chromium.net;

import android.net.TrafficStats;
import android.os.ParcelFileDescriptor;
import android.os.Build;
import android.os.Build.VERSION_CODES;
import android.security.NetworkSecurityPolicy;

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
import java.net.Socket;

import java.security.KeyStoreException;
import java.security.NoSuchAlgorithmException;
import java.security.cert.CertificateException;

import java.nio.charset.StandardCharsets;

/**
 * This class implements net utilities required by the net component.
 */
public final class AndroidNetworkLibrary {

  private static final String TAG = "AndroidNetworkLibrary";

  private static boolean mUseFakeCertificateVerification;

  /**
   * Whether a fake should be used in place of X509Util. This allows to easily test the JNI
   * call interaction in robolectric tests.
   *
   * @param useFakeCertificateVerification Whether FakeX509Util should be used or not.
   */
  public static synchronized void
  setFakeCertificateVerificationForTesting(boolean useFakeCertificateVerification) {
    mUseFakeCertificateVerification = useFakeCertificateVerification;
  }

  public static synchronized boolean getFakeCertificateVerificationForTesting() {
    return mUseFakeCertificateVerification;
  }

  /**
   * Validate the server's certificate chain is trusted. Note that the caller
   * must still verify the name matches that of the leaf certificate.
   * This is called from native code.
   *
   * @param certChain The ASN.1 DER encoded bytes for certificates.
   * @param authType Bytes representing the UTF-8 encoding of the key exchange algorithm name (e.g.
   *     RSA).
   * @param host Bytes representing the UTF-8 encoding of the hostname of the server.
   * @return Android certificate verification result code.
   */
  public static synchronized AndroidCertVerifyResult verifyServerCertificates(byte[][] certChain,
                                                                              byte[] authTypeBytes,
                                                                              byte[] hostBytes) {
    String authType = new String(authTypeBytes, StandardCharsets.UTF_8);
    String host = new String(hostBytes, StandardCharsets.UTF_8);
    if (mUseFakeCertificateVerification) {
      AndroidCertVerifyResult result =
          FakeX509Util.verifyServerCertificates(certChain, authType, host);
      return result;
    }

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
   * This is called from native code.
   *
   * @param rootCert DER encoded bytes of the certificate.
   */
  public static void addTestRootCertificate(byte[] rootCert)
      throws CertificateException, KeyStoreException, NoSuchAlgorithmException {
    if (mUseFakeCertificateVerification) {
      FakeX509Util.addTestRootCertificate(rootCert);
    } else {
      X509Util.addTestRootCertificate(rootCert);
    }
  }

  /**
   * Removes all test root certificates added by |addTestRootCertificate| calls from the local
   * trust store.
   * This is called from native code.
   */
  public static void clearTestRootCertificates()
      throws NoSuchAlgorithmException, CertificateException, KeyStoreException {
    if (mUseFakeCertificateVerification) {
      FakeX509Util.clearTestRootCertificates();
    } else {
      X509Util.clearTestRootCertificates();
    }
  }

  /*
   * Returns true if cleartext traffic to a given host is allowed by the current app.
   */
  public static boolean isCleartextTrafficPermitted(String host) {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
      // This API was not implemented before Android M.
      return true;
    } else if (Build.VERSION.SDK_INT == Build.VERSION_CODES.M) {
      // M only supported global checks.
      return NetworkSecurityPolicy.getInstance().isCleartextTrafficPermitted();
    } else {
      // The host-specific API was implemented in Android N (which came after Android M).
      return NetworkSecurityPolicy.getInstance().isCleartextTrafficPermitted(host);
    }
  }

  /**
   * Class to wrap FileDescriptor.setInt$() which is hidden and so must be accessed via
   * reflection.
   */
  private static class SetFileDescriptor {
    // Reference to FileDescriptor.setInt$(int fd).
    private static final Method sFileDescriptorSetInt;

    // Get reference to FileDescriptor.setInt$(int fd) via reflection.
    static {
      try {
        sFileDescriptorSetInt = FileDescriptor.class.getMethod("setInt$", Integer.TYPE);
      } catch (NoSuchMethodException | SecurityException e) {
        throw new RuntimeException("Unable to get FileDescriptor.setInt$", e);
      }
    }

    /** Creates a FileDescriptor and calls FileDescriptor.setInt$(int fd) on it. */
    public static FileDescriptor createWithFd(int fd) {
      try {
        FileDescriptor fileDescriptor = new FileDescriptor();
        sFileDescriptorSetInt.invoke(fileDescriptor, fd);
        return fileDescriptor;
      } catch (IllegalAccessException e) {
        throw new RuntimeException("FileDescriptor.setInt$() failed", e);
      } catch (InvocationTargetException e) {
        throw new RuntimeException("FileDescriptor.setInt$() failed", e);
      }
    }
  }

  /**
   * This class provides an implementation of {@link java.net.Socket} that serves only as a
   * conduit to pass a file descriptor integer to {@link android.net.TrafficStats#tagSocket}
   * when called by {@link #tagSocket}. This class does not take ownership of the file descriptor,
   * so calling {@link #close} will not actually close the file descriptor.
   */
  private static class SocketFd extends Socket {
    /**
     * This class provides an implementation of {@link java.net.SocketImpl} that serves only as
     * a conduit to pass a file descriptor integer to {@link android.net.TrafficStats#tagSocket}
     * when called by {@link #tagSocket}. This class does not take ownership of the file
     * descriptor, so calling {@link #close} will not actually close the file descriptor.
     */
    private static class SocketImplFd extends SocketImpl {
      /**
       * Create a {@link java.net.SocketImpl} that sets {@code fd} as the underlying file
       * descriptor. Does not take ownership of the file descriptor, so calling {@link #close}
       * will not actually close the file descriptor.
       */
      SocketImplFd(FileDescriptor fd) { this.fd = fd; }

      protected void accept(SocketImpl s) { throw new RuntimeException("accept not implemented"); }
      protected int available() { throw new RuntimeException("accept not implemented"); }
      protected void bind(InetAddress host, int port) {
        throw new RuntimeException("accept not implemented");
      }
      protected void close() {}
      protected void connect(InetAddress address, int port) {
        throw new RuntimeException("connect not implemented");
      }
      protected void connect(SocketAddress address, int timeout) {
        throw new RuntimeException("connect not implemented");
      }
      protected void connect(String host, int port) {
        throw new RuntimeException("connect not implemented");
      }
      protected void create(boolean stream) {}
      protected InputStream getInputStream() {
        throw new RuntimeException("getInputStream not implemented");
      }
      protected OutputStream getOutputStream() {
        throw new RuntimeException("getOutputStream not implemented");
      }
      protected void listen(int backlog) { throw new RuntimeException("listen not implemented"); }
      protected void sendUrgentData(int data) {
        throw new RuntimeException("sendUrgentData not implemented");
      }
      public Object getOption(int optID) {
        throw new RuntimeException("getOption not implemented");
      }
      public void setOption(int optID, Object value) {
        throw new RuntimeException("setOption not implemented");
      }
    }

    /**
     * Create a {@link java.net.Socket} that sets {@code fd} as the underlying file
     * descriptor. Does not take ownership of the file descriptor, so calling {@link #close}
     * will not actually close the file descriptor.
     */
    SocketFd(FileDescriptor fd) throws IOException { super(new SocketImplFd(fd)); }
  }

  /**
   * Tag socket referenced by {@code ifd} with {@code tag} for UID {@code uid}.
   *
   * Assumes thread UID tag isn't set upon entry, and ensures thread UID tag isn't set upon exit.
   * Unfortunately there is no TrafficStatis.getThreadStatsUid().
   */
  private static void tagSocket(int ifd, int uid, int tag) throws IOException {
    // Set thread tags.
    int oldTag = TrafficStats.getThreadStatsTag();
    if (tag != oldTag) {
      TrafficStats.setThreadStatsTag(tag);
    }
    if (uid != -1) {
      ThreadStatsUid.set(uid);
    }

    // Apply thread tags to socket.

    // First, convert integer file descriptor (ifd) to FileDescriptor.
    final ParcelFileDescriptor pfd;
    final FileDescriptor fd;
    // The only supported way to generate a FileDescriptor from an integer file
    // descriptor is via ParcelFileDescriptor.adoptFd(). Unfortunately prior to Android
    // Marshmallow ParcelFileDescriptor.detachFd() didn't actually detach from the
    // FileDescriptor, so use reflection to set {@code fd} into the FileDescriptor for
    // versions prior to Marshmallow. Here's the fix that went into Marshmallow:
    // https://android.googlesource.com/platform/frameworks/base/+/b30ad6f
    if (Build.VERSION.SDK_INT < VERSION_CODES.M) {
      pfd = null;
      fd = SetFileDescriptor.createWithFd(ifd);
    } else {
      pfd = ParcelFileDescriptor.adoptFd(ifd);
      fd = pfd.getFileDescriptor();
    }
    // Second, convert FileDescriptor to Socket.
    Socket s = new SocketFd(fd);
    // Third, tag the Socket.
    TrafficStats.tagSocket(s);
    s.close(); // No-op but always good to close() Closeables.
    // Have ParcelFileDescriptor relinquish ownership of the file descriptor.
    if (pfd != null) {
      pfd.detachFd();
    }

    // Restore prior thread tags.
    if (tag != oldTag) {
      TrafficStats.setThreadStatsTag(oldTag);
    }
    if (uid != -1) {
      ThreadStatsUid.clear();
    }
  }
}
