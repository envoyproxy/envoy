package org.chromium.net;

import androidx.annotation.IntDef;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;

@IntDef({CertVerifyStatusAndroid.OK, CertVerifyStatusAndroid.FAILED,
         CertVerifyStatusAndroid.NO_TRUSTED_ROOT, CertVerifyStatusAndroid.EXPIRED,
         CertVerifyStatusAndroid.NOT_YET_VALID, CertVerifyStatusAndroid.UNABLE_TO_PARSE,
         CertVerifyStatusAndroid.INCORRECT_KEY_USAGE})
@Retention(RetentionPolicy.SOURCE)
public @interface CertVerifyStatusAndroid {
  /**
   * Certificate is trusted.
   */
  int OK = 0;
  /**
   * Certificate verification could not be conducted.
   */
  int FAILED = -1;
  /**
   * Certificate is not trusted due to non-trusted root of the certificate chain.
   */
  int NO_TRUSTED_ROOT = -2;
  /**
   * Certificate is not trusted because it has expired.
   */
  int EXPIRED = -3;
  /**
   * Certificate is not trusted because it is not valid yet.
   */
  int NOT_YET_VALID = -4;
  /**
   * Certificate is not trusted because it could not be parsed.
   */
  int UNABLE_TO_PARSE = -5;
  /**
   * Certificate is not trusted because it has an extendedKeyUsage field, but its value is not
   * correct for a web server.
   */
  int INCORRECT_KEY_USAGE = -6;
}
