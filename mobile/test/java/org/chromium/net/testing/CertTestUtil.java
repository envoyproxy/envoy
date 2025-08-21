package org.chromium.net.testing;

import android.util.Base64;
import java.io.BufferedReader;
import java.io.FileReader;
import java.io.IOException;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.security.cert.Certificate;

/**
 * Certificate related utility methods.
 */
public final class CertTestUtil {
  /**
   * The location of the directory that contains certificates for testing.
   */
  public static final String CERTS_DIRECTORY =
      UrlUtils.getIsolatedTestFilePath("net/data/ssl/certificates/");

  private static final String BEGIN_MARKER = "-----BEGIN CERTIFICATE-----";
  private static final String END_MARKER = "-----END CERTIFICATE-----";

  /**
   * Converts a PEM formatted cert in a given file to the binary DER format.
   *
   * @param pemPathname the location of the certificate to convert.
   * @return array of bytes that represent the certificate in DER format.
   * @throws IOException if the file cannot be read.
   */
  public static byte[] pemToDer(String pemPathname) throws IOException {
    BufferedReader reader = new BufferedReader(new FileReader(pemPathname));
    StringBuilder builder = new StringBuilder();

    // Skip past leading junk lines, if any.
    String line = reader.readLine();
    while (line != null && !line.contains(BEGIN_MARKER))
      line = reader.readLine();

    // Then skip the BEGIN_MARKER itself, if present.
    while (line != null && line.contains(BEGIN_MARKER))
      line = reader.readLine();

    // Now gather the data lines into the builder.
    while (line != null && !line.contains(END_MARKER)) {
      builder.append(line.trim());
      line = reader.readLine();
    }

    reader.close();
    return Base64.decode(builder.toString(), Base64.DEFAULT);
  }

  /**
   * Returns SHA256 hash of the public key of a given certificate.
   *
   * @param cert the cert that should be used to retrieve the public key from.
   * @return SHA256 hash of the public key.
   */
  public static byte[] getPublicKeySha256(Certificate cert) {
    try {
      byte[] publicKey = cert.getPublicKey().getEncoded();
      MessageDigest digest = MessageDigest.getInstance("SHA-256");
      return digest.digest(publicKey);
    } catch (NoSuchAlgorithmException ex) {
      // This exception should never happen since SHA-256 is known algorithm
      throw new RuntimeException(ex);
    }
  }

  private CertTestUtil() {}
}
