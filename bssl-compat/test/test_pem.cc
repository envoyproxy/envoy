#include <gtest/gtest.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/x509v3.h>
#include "certs/root_ca_cert.pem.h"
#include "certs/root_ca_key.pem.h"

TEST(PEMTest, testPEM_read) {
    // test PEM_read_bio_PUBKEY exists

    std::string cert_data = {
"-----BEGIN CERTIFICATE-----"
"MIIEBzCCAu+gAwIBAgIUNT7DdWdpRF5GXsgldAUwg/w9ofMwDQYJKoZIhvcNAQEL"
"BQAwgYoxCzAJBgNVBAYTAkdCMRYwFAYDVQQIDA1UeW5lIGFuZCBXZWFyMRwwGgYD"
"VQQHDBNOZXdjYXN0bGUgdXBvbiBUeW5lMRAwDgYDVQQKDAdSZWQgSGF0MRwwGgYD"
"VQQLDBNSZWQgSGF0IEVuZ2luZWVyaW5nMRUwEwYDVQQDDAxUZXN0IFJvb3QgQ0Ew"
"HhcNMjMwNDE5MTg0MTQ1WhcNMjUwNDE4MTg0MTQ1WjCBijELMAkGA1UEBhMCR0Ix"
"FjAUBgNVBAgMDVR5bmUgYW5kIFdlYXIxHDAaBgNVBAcME05ld2Nhc3RsZSB1cG9u"
"IFR5bmUxEDAOBgNVBAoMB1JlZCBIYXQxHDAaBgNVBAsME1JlZCBIYXQgRW5naW5l"
"ZXJpbmcxFTATBgNVBAMMDFRlc3QgUm9vdCBDQTCCASIwDQYJKoZIhvcNAQEBBQAD"
"ggEPADCCAQoCggEBAMxXI2ABGv7MVb/vLKtgR6PJQzLIXsQHnIb80JuWAAbDujwO"
"a6YuYypjpbF5sZaxNK2KHZH7IuBRNy8asiz8xjt/BK3+xneAIWYGGYtIR+l2teho"
"eRCBdoIGruVzrHuE+FWJqt7f5eiOtbzzAeoKXMLiTwX8CF9NA0ugDjLZRlSzRS3d"
"chKdgq542sOQ0vwiDtdTovmZT/5RweGumZ1uvAUP8DynrxKdzffv0c30nEpcCbOI"
"SqSRgDUwOwCL2dXshJM3EBV0Ycn+yvvqCvDlWtqILvStw9gTHNdo+fn7g2v+/GZb"
"OnOx1Yp9JlzcN5JchKd47QwvX7Llk7WV4rniv3ECAwEAAaNjMGEwDwYDVR0TAQH/"
"BAUwAwEB/zAOBgNVHQ8BAf8EBAMCAQYwHQYDVR0OBBYEFFG0cchLrL8gNr/H1Ykr"
"XoKcRfeUMB8GA1UdIwQYMBaAFFG0cchLrL8gNr/H1YkrXoKcRfeUMA0GCSqGSIb3"
"DQEBCwUAA4IBAQBonfu+Om04XKWQU6PQlAPoz3vxYec9UYSdL32wYiMEBj8OO/Sv"
"7a9aYRpahkX27pT7J0erL9jR+p+gAtmvCWl7SYzfb0LQzUgPPImb5bcEZ/Q80+Uv"
"IffCl3ChWEqErg0JJdMSc5qj1WpTU9+Y4YXMddviYsQgVNDea7w1v+hFAp4BlyKP"
"OE8h9TQDQErkxI+3kulc2X7OSvGFUDVJpSTPdYd68uSuW4pf7E9l2SbNDaTqNKFI"
"EyhYYl6v/HUBYNiPnRUPHdkz+Ypt/9+YxkdNc680q0mGDjU1tWZX0/XVE3pETp52"
"tysI3R1cdVCam29BtNXUHfhBxjfxtH+zvhjK"
"-----END CERTIFICATE-----\n"
    };

    X509 *cert = nullptr;

    BIO *pem = BIO_new_mem_buf(cert_data.c_str(), static_cast<ssize_t>(cert_data.length()));
//    BIO *pem = BIO_new_mem_buf(root_ca_cert_pem_str, strlen(root_ca_cert_pem_str));
    ASSERT_NE(pem, nullptr);

    cert = PEM_read_bio_X509(pem, NULL, 0, NULL);
    if ( cert ) {
        //  openssl x509 -pubkey -noout -in root_ca_cert.pem > root_pubkey.pem
        EVP_PKEY *result = nullptr;
        PEM_read_bio_PUBKEY(pem, &result, nullptr, nullptr);
        EXPECT_NE(result, nullptr);
        if ( result ) {
            EVP_PKEY_free(result);
        }
        else {
            unsigned long error = ERR_peek_last_error();
            char error_msg [256];
            ERR_error_string_n(error, error_msg, sizeof(error_msg));
            std::cout << "OpenSSL Error: " << error_msg << std::endl;
        }
    }

    BIO_free(pem);
}

TEST(PEMTest, test_PEM_write_bio_X509) {
    bssl::UniquePtr<BIO> pem1 {BIO_new_mem_buf(root_ca_cert_pem_str, strlen(root_ca_cert_pem_str))};
    ASSERT_TRUE(pem1);

    bssl::UniquePtr<X509> cert {PEM_read_bio_X509(pem1.get(), NULL, 0, NULL)};
    ASSERT_TRUE(cert) << ERR_error_string(ERR_get_error(), nullptr);

    bssl::UniquePtr<BIO> pem2 {BIO_new(BIO_s_mem())};
    ASSERT_TRUE(PEM_write_bio_X509(pem2.get(), cert.get()));

    BUF_MEM *m {nullptr};
    BIO_get_mem_ptr(pem2.get(), &m);
    ASSERT_TRUE(m);

    // Check the PEM_write_bio_X509() output is the
    // same as the original root_ca_cert_pem_str input

    ASSERT_EQ(m->length, strlen(root_ca_cert_pem_str));
    for(size_t i = 0; i < strlen(root_ca_cert_pem_str); i++) {
        ASSERT_EQ(m->data[i], root_ca_cert_pem_str[i]);
    }
}

TEST(PEMTest, test_PEM_read_bio_X509_AUX) {
    // This certificate has extra trust information (serverAuth) which is only
    // processed by the "AUX" versions of the PEM_read/write_bio_X509() functions.
    const char *cert_pem_str {
        "-----BEGIN TRUSTED CERTIFICATE-----\n"
        "MIIEBzCCAu+gAwIBAgIUNT7DdWdpRF5GXsgldAUwg/w9ofMwDQYJKoZIhvcNAQEL"
        "BQAwgYoxCzAJBgNVBAYTAkdCMRYwFAYDVQQIDA1UeW5lIGFuZCBXZWFyMRwwGgYD"
        "VQQHDBNOZXdjYXN0bGUgdXBvbiBUeW5lMRAwDgYDVQQKDAdSZWQgSGF0MRwwGgYD"
        "VQQLDBNSZWQgSGF0IEVuZ2luZWVyaW5nMRUwEwYDVQQDDAxUZXN0IFJvb3QgQ0Ew"
        "HhcNMjMwNDE5MTg0MTQ1WhcNMjUwNDE4MTg0MTQ1WjCBijELMAkGA1UEBhMCR0Ix"
        "FjAUBgNVBAgMDVR5bmUgYW5kIFdlYXIxHDAaBgNVBAcME05ld2Nhc3RsZSB1cG9u"
        "IFR5bmUxEDAOBgNVBAoMB1JlZCBIYXQxHDAaBgNVBAsME1JlZCBIYXQgRW5naW5l"
        "ZXJpbmcxFTATBgNVBAMMDFRlc3QgUm9vdCBDQTCCASIwDQYJKoZIhvcNAQEBBQAD"
        "ggEPADCCAQoCggEBAMxXI2ABGv7MVb/vLKtgR6PJQzLIXsQHnIb80JuWAAbDujwO"
        "a6YuYypjpbF5sZaxNK2KHZH7IuBRNy8asiz8xjt/BK3+xneAIWYGGYtIR+l2teho"
        "eRCBdoIGruVzrHuE+FWJqt7f5eiOtbzzAeoKXMLiTwX8CF9NA0ugDjLZRlSzRS3d"
        "chKdgq542sOQ0vwiDtdTovmZT/5RweGumZ1uvAUP8DynrxKdzffv0c30nEpcCbOI"
        "SqSRgDUwOwCL2dXshJM3EBV0Ycn+yvvqCvDlWtqILvStw9gTHNdo+fn7g2v+/GZb"
        "OnOx1Yp9JlzcN5JchKd47QwvX7Llk7WV4rniv3ECAwEAAaNjMGEwDwYDVR0TAQH/"
        "BAUwAwEB/zAOBgNVHQ8BAf8EBAMCAQYwHQYDVR0OBBYEFFG0cchLrL8gNr/H1Ykr"
        "XoKcRfeUMB8GA1UdIwQYMBaAFFG0cchLrL8gNr/H1YkrXoKcRfeUMA0GCSqGSIb3"
        "DQEBCwUAA4IBAQBonfu+Om04XKWQU6PQlAPoz3vxYec9UYSdL32wYiMEBj8OO/Sv"
        "7a9aYRpahkX27pT7J0erL9jR+p+gAtmvCWl7SYzfb0LQzUgPPImb5bcEZ/Q80+Uv"
        "IffCl3ChWEqErg0JJdMSc5qj1WpTU9+Y4YXMddviYsQgVNDea7w1v+hFAp4BlyKP"
        "OE8h9TQDQErkxI+3kulc2X7OSvGFUDVJpSTPdYd68uSuW4pf7E9l2SbNDaTqNKFI"
        "EyhYYl6v/HUBYNiPnRUPHdkz+Ypt/9+YxkdNc680q0mGDjU1tWZX0/XVE3pETp52"
        "tysI3R1cdVCam29BtNXUHfhBxjfxtH+zvhjKMAwwCgYIKwYBBQUHAwE=\n"
        "-----END TRUSTED CERTIFICATE-----\n"
    };

    bssl::UniquePtr<BIO> pem1 {BIO_new_mem_buf(cert_pem_str, strlen(cert_pem_str))};
    ASSERT_TRUE(pem1);

    // Reading the certificate with the "AUX" PEM_read_bio_X509_AUX() function
    // will read in the additional trust information from the PEM string.
    bssl::UniquePtr<X509> cert {PEM_read_bio_X509_AUX(pem1.get(), nullptr, nullptr, nullptr)};
    ASSERT_TRUE(cert) << ERR_error_string(ERR_get_error(), nullptr);

    // Writing the certificate with the "normal" PEM_write_bio_X509() function
    // will omit the additional trust information from the resulting PEM string
    bssl::UniquePtr<BIO> pem2 {BIO_new(BIO_s_mem())};
    ASSERT_TRUE(PEM_write_bio_X509(pem2.get(), cert.get()));

    BUF_MEM *m1 {nullptr};
    BIO_get_mem_ptr(pem2.get(), &m1);
    ASSERT_TRUE(m1);

    // Check the PEM_write_bio_X509() output is shorter than the original input
    ASSERT_TRUE(m1->length < strlen(cert_pem_str));
}
