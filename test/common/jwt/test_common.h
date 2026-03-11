#pragma once

// Copyright 2018 Google LLC
// Copyright Envoy Project Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>
#include <iostream>

#include "source/common/jwt/jwt.h"

namespace Envoy {
namespace JwtVerify {

void fuzzJwtSignatureBits(const Jwt& jwt, std::function<void(const Jwt& jwt)> test_fn) {
  // alter 1 bit
  for (size_t b = 0; b < jwt.signature_.size(); ++b) {
    for (int bit = 0; bit < 8; ++bit) {
      Jwt fuzz_jwt(jwt);
      unsigned char bb = fuzz_jwt.signature_[b];
      bb ^= static_cast<unsigned char>(1 << bit);
      fuzz_jwt.signature_[b] = static_cast<char>(bb);
      test_fn(fuzz_jwt);
    }
  }
}

void fuzzJwtSignatureLength(const Jwt& jwt, std::function<void(const Jwt& jwt)> test_fn) {
  // truncate bytes
  for (size_t count = 1; count < jwt.signature_.size(); ++count) {
    Jwt fuzz_jwt(jwt);
    fuzz_jwt.signature_ = jwt.signature_.substr(0, count);
    test_fn(fuzz_jwt);
  }
}

void fuzzJwtSignature(const Jwt& jwt, std::function<void(const Jwt& jwt)> test_fn) {
  fuzzJwtSignatureBits(jwt, test_fn);
  fuzzJwtSignatureLength(jwt, test_fn);
}

// copy from ESP:
// https://github.com/cloudendpoints/esp/blob/master/src/api_manager/auth/lib/auth_jwt_validator_test.cc
const char kPublicKeyX509[] =
    "{\"62a93512c9ee4c7f8067b5a216dade2763d32a47\": \"-----BEGIN "
    "CERTIFICATE-----"
    "\\nMIIDYDCCAkigAwIBAgIIEzRv3yOFGvcwDQYJKoZIhvcNAQEFBQAwUzFRME8GA1UE\\nAxNI"
    "NjI4NjQ1NzQxODgxLW5vYWJpdTIzZjVhOG04b3ZkOHVjdjY5OGxqNzh2djBs\\nLmFwcHMuZ29"
    "vZ2xldXNlcmNvbnRlbnQuY29tMB4XDTE1MDkxMTIzNDg0OVoXDTI1\\nMDkwODIzNDg0OVowUz"
    "FRME8GA1UEAxNINjI4NjQ1NzQxODgxLW5vYWJpdTIzZjVh\\nOG04b3ZkOHVjdjY5OGxqNzh2d"
    "jBsLmFwcHMuZ29vZ2xldXNlcmNvbnRlbnQuY29t\\nMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8A"
    "MIIBCgKCAQEA0YWnm/eplO9BFtXszMRQ\\nNL5UtZ8HJdTH2jK7vjs4XdLkPW7YBkkm/"
    "2xNgcaVpkW0VT2l4mU3KftR+6s3Oa5R\\nnz5BrWEUkCTVVolR7VYksfqIB2I/"
    "x5yZHdOiomMTcm3DheUUCgbJRv5OKRnNqszA\\n4xHn3tA3Ry8VO3X7BgKZYAUh9fyZTFLlkeA"
    "h0+bLK5zvqCmKW5QgDIXSxUTJxPjZ\\nCgfx1vmAfGqaJb+"
    "nvmrORXQ6L284c73DUL7mnt6wj3H6tVqPKA27j56N0TB1Hfx4\\nja6Slr8S4EB3F1luYhATa1"
    "PKUSH8mYDW11HolzZmTQpRoLV8ZoHbHEaTfqX/"
    "aYah\\nIwIDAQABozgwNjAMBgNVHRMBAf8EAjAAMA4GA1UdDwEB/"
    "wQEAwIHgDAWBgNVHSUB\\nAf8EDDAKBggrBgEFBQcDAjANBgkqhkiG9w0BAQUFAAOCAQEAP4gk"
    "DCrPMI27/"
    "QdN\\nwW0mUSFeDuM8VOIdxu6d8kTHZiGa2h6nTz5E+"
    "twCdUuo6elGit3i5H93kFoaTpex\\nj/eDNoULdrzh+cxNAbYXd8XgDx788/"
    "jm06qkwXd0I5s9KtzDo7xxuBCyGea2LlpM\\n2HOI4qFunjPjFX5EFdaT/Rh+qafepTKrF/"
    "GQ7eGfWoFPbZ29Hs5y5zATJCDkstkY\\npnAya8O8I+"
    "tfKjOkcra9nOhtck8BK94tm3bHPdL0OoqKynnoRCJzN5KPlSGqR/h9\\nSMBZzGtDOzA2sX/"
    "8eyU6Rm4MV6/1/53+J6EIyarR5g3IK1dWmz/YT/YMCt6LhHTo\\n3yfXqQ==\\n-----END "
    "CERTIFICATE-----\\n\",\"b3319a147514df7ee5e4bcdee51350cc890cc89e\": "
    "\"-----BEGIN "
    "CERTIFICATE-----"
    "\\nMIIDYDCCAkigAwIBAgIICjE9gZxAlu8wDQYJKoZIhvcNAQEFBQAwUzFRME8GA1UE\\nAxNI"
    "NjI4NjQ1NzQxODgxLW5vYWJpdTIzZjVhOG04b3ZkOHVjdjY5OGxqNzh2djBs\\nLmFwcHMuZ29"
    "vZ2xldXNlcmNvbnRlbnQuY29tMB4XDTE1MDkxMzAwNTAyM1oXDTI1\\nMDkxMDAwNTAyM1owUz"
    "FRME8GA1UEAxNINjI4NjQ1NzQxODgxLW5vYWJpdTIzZjVh\\nOG04b3ZkOHVjdjY5OGxqNzh2d"
    "jBsLmFwcHMuZ29vZ2xldXNlcmNvbnRlbnQuY29t\\nMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8A"
    "MIIBCgKCAQEAqDi7Tx4DhNvPQsl1ofxx\\nc2ePQFcs+L0mXYo6TGS64CY/"
    "2WmOtvYlcLNZjhuddZVV2X88m0MfwaSA16wE+"
    "RiK\\nM9hqo5EY8BPXj57CMiYAyiHuQPp1yayjMgoE1P2jvp4eqF+"
    "BTillGJt5W5RuXti9\\nuqfMtCQdagB8EC3MNRuU/"
    "KdeLgBy3lS3oo4LOYd+74kRBVZbk2wnmmb7IhP9OoLc\\n1+7+"
    "9qU1uhpDxmE6JwBau0mDSwMnYDS4G/"
    "ML17dC+ZDtLd1i24STUw39KH0pcSdf\\nFbL2NtEZdNeam1DDdk0iUtJSPZliUHJBI/"
    "pj8M+2Mn/"
    "oA8jBuI8YKwBqYkZCN1I9\\n5QIDAQABozgwNjAMBgNVHRMBAf8EAjAAMA4GA1UdDwEB/"
    "wQEAwIHgDAWBgNVHSUB\\nAf8EDDAKBggrBgEFBQcDAjANBgkqhkiG9w0BAQUFAAOCAQEAHSPR"
    "7fDAWyZ825IZ\\n86hEsQZCvmC0QbSzy62XisM/uHUO75BRFIAvC+zZAePCcNo/"
    "nh6FtEM19wZpxLiK\\n0m2nqDMpRdw3Qt6BNhjJMozTxA2Xdipnfq+fGpa+"
    "bMkVpnRZ53qAuwQpaKX6vagr\\nj83Bdx2b5WPQCg6xrQWsf79Vjj2U1hdw7+"
    "klcF7tLef1p8qA/ezcNXmcZ4BpbpaO\\nN9M4/kQOA3Y2F3ISAaOJzCB25F259whjW+Uuqd/"
    "L9Lb4gPPSUMSKy7Zy4Sn4il1U\\nFc94Mi9j13oeGvLOduNOStGu5XROIxDtCEjjn2y2SL2bPw"
    "0qAlIzBeniiApkmYw/\\no6OLrg==\\n-----END CERTIFICATE-----\\n\"}";

// A real X509 public key from
// https://www.googleapis.com/service_accounts/v1/metadata/x509/[SERVICE_ACCOUNT]@[PROJECT_ID].iam.gserviceaccount.com
const std::string kRealX509Jwks = R"(
{
  "82cfd797903063a0b78ce1cbf5e2fe036a6de242": "-----BEGIN CERTIFICATE-----\nMIIC+jCCAeKgAwIBAgIIEN2Xgd3Y1CMwDQYJKoZIhvcNAQEFBQAwIDEeMBwGA1UE\nAxMVMTA2OTQ3MDEyMjYwNDg4NzM2MTU3MB4XDTE5MDIyNzE3NTA1N1oXDTI5MDIy\nNDE3NTA1N1owIDEeMBwGA1UEAxMVMTA2OTQ3MDEyMjYwNDg4NzM2MTU3MIIBIjAN\nBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA00bLFfPv/jeyVU6xuStcwHdSBa+m\nlOX/9oWFwMsQucENe+QYKJmkAqdATz3BKJ354iknMy556Y8cBHbZa9X6gxi2BIPW\nzkuKTruDJrQrg6cgR6RHZ9WNoxGLRtyhq8PimV8DVtMSLYVy3p/gMwEtuQY4jiXS\nhhvCZxuJZIJnabNqTU5AGWfduQgDcLRd25cShKxDNOtfcBWQ+ZQWt5qkZGz5XFQ/\nt1+bND+hA3dC3bwLc9yFrgU+Z+XEDQErq4OG9MVezw6h6Imn6gkrdSyG1k9BjPsf\n4senqDXgtK2Iz9MuGIWcG62wV2a7qJYjnGBJfI4QKQBEdsYbuUel2wB0wQIDAQAB\nozgwNjAMBgNVHRMBAf8EAjAAMA4GA1UdDwEB/wQEAwIHgDAWBgNVHSUBAf8EDDAK\nBggrBgEFBQcDAjANBgkqhkiG9w0BAQUFAAOCAQEArrvMP0yrPQlCC/QB0iPxb4TY\nPPiDTuY4fPytUQgvSdQ4rMPSNZafe7tIS+0KDhZtblepaS5whVobVh9lS2bK+rDH\nRsM/H9XRGpyh2rJ6NYUbiyEMQ4jfNh99A02Nsz4Gaed3IE8Hml2pWLcCbp2VGDEN\nr6qrBVVWsaT736/kwVNp14S6FNhVIx1pZeKJrtOsJD+Y4f21WKlWdKdu4QVlxJoE\n9LtFur56aLhDA64D5GPjQnatRyShcWXvgEvUk5YUuBkjTDL1HSNTeqTdG6j8OEZo\nBuyfyPz4yV6BjnJWl2fk8v+9sB1B6m5LoR7ETHlWwh+elmaejFQCJN1+ED8k0w==\n-----END CERTIFICATE-----\n",
  "08fcbb64ace10689705c063c0f5a165da5952acd": "-----BEGIN CERTIFICATE-----\nMIIC+jCCAeKgAwIBAgIILmYDGTHFClgwDQYJKoZIhvcNAQEFBQAwIDEeMBwGA1UE\nAxMVMTA2OTQ3MDEyMjYwNDg4NzM2MTU3MB4XDTE1MTExMTAwMjMyMVoXDTI1MTEw\nODAwMjMyMVowIDEeMBwGA1UEAxMVMTA2OTQ3MDEyMjYwNDg4NzM2MTU3MIIBIjAN\nBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAmixY2zuGgr6Lhed4knYbHNCGSj/t\n8uV1PDhJORUhHd0JnhAJNtRTU3KDYC88mQZqgcRmn2HCvmi5Koc2iMDLrBomBC8p\naFJn8Mo6ZPZNMY9rp0MUkY4IZgJ4I0LLl0fPmPwupkVMOXGIAvTCLfs/eibJJtgQ\nuxUZepMoiMxmBJL3iP7Bg3K6nKJVcalBpMfOibMKUMxk/J8/ud1IGQA0JhCo2WxS\nF7sQR7j5qC9R5x1OIZi0kpa/8WseBx2Y2oI6/EwHKM2glA/mkBqTIODT1sgCddGf\nBKY+yTfKw8ZNSqNRjg2tUYmxLkSSUELCyl4CSSFM2ZDo1LIzbtiKSZjOLQIDAQAB\nozgwNjAMBgNVHRMBAf8EAjAAMA4GA1UdDwEB/wQEAwIHgDAWBgNVHSUBAf8EDDAK\nBggrBgEFBQcDAjANBgkqhkiG9w0BAQUFAAOCAQEAgChPhXk5vx3Ni8zcM5XU6pYv\nT7qxiYTHqt2ZTA63LA5k4jdCuaqQgJNZO8MmmlYzdihV2MrxTgUsq+GjPDfC8DX/\nGCKnBVBXBUxNJFQHs+D/o3twcoWKDE0/dmECWGhKuzyhs2CuE61mCphnvq/s0Cep\nYtwZTf9DzP0meHW6INXMxJaHbRjDJIC7Eg7F8JqAXsUi1LLt3xKiFuKApVNkWX+F\nEEOUPOkwLN/OTghJKG1xqR+OVs+9yniDflbW246tx+o+7eOJd0qk2GmMd9O3ZWwD\noynUQnXfZRpJZPTEfP5Z2XMMSfQBSB4zVgDFVhryD7X2C9lGUBymVEf2fZ4TFQ==\n-----END CERTIFICATE-----\n",
  "018cf5a21706e2b707e4104c33206c58e7262bf5": "-----BEGIN CERTIFICATE-----\nMIIDVDCCAjygAwIBAgIIPqS1LlT90NAwDQYJKoZIhvcNAQEFBQAwTTFLMEkGA1UE\nAxNCc2VydmljZS1jb250cm9sLWluLWxvYWQtdGVzdC5lc3AtbG9hZC10ZXN0Lmlh\nbS5nc2VydmljZWFjY291bnQuY29tMB4XDTIwMDEwNTA1MzAzNloXDTIwMDEyMTE3\nNDUzNlowTTFLMEkGA1UEAxNCc2VydmljZS1jb250cm9sLWluLWxvYWQtdGVzdC5l\nc3AtbG9hZC10ZXN0LmlhbS5nc2VydmljZWFjY291bnQuY29tMIIBIjANBgkqhkiG\n9w0BAQEFAAOCAQ8AMIIBCgKCAQEAyuGy/yh+zX1Prk7CPmTYOzXB6OdpBzw90nZV\nIbg7wY3cZ/ab82bHHwJqfRmbHzPFXg0OZ7mWtQJo292kcr+nAfQx0iRpsZkvcyyB\nV4o1IXWQNfpnWlJBmdp5AZZ2Ag3e6MzDAWLCUvYOnU2z8lhWzmrhhxPuToIkkSay\nKOrgThS2iGhHSyRl7lfCGG6/Ml0xnOKfb73pGPzRKAzHn3ML6rN2Z6/iznZZeFSS\n12SIpVa3wqOzACTyFuybAmcr6CI482FwWLnSabWqMBrQcOue2UbPtXF+faOUN9Dc\nlR3JiASOtfsWieYf/R2y1M5BuOffhcBIHzKEvNiHwJ7/8KZhnQIDAQABozgwNjAM\nBgNVHRMBAf8EAjAAMA4GA1UdDwEB/wQEAwIHgDAWBgNVHSUBAf8EDDAKBggrBgEF\nBQcDAjANBgkqhkiG9w0BAQUFAAOCAQEApz5t9q3aBD18M0pSmJMW+zndXXQP5lR8\nubKuUm8hJ96J2hq/udw9ngbbPkREFp23xqCZZtGqrUlwR9irnWG9fuJ2j4RXoUFy\nkf4L+vD1bXFxwj5p7AbX6V/ns5+JD4wlxAPcnbdeetwy7OIQowWWWjbFNgkASJ2U\nMOrO1Shd9GxbHX+I5KR8jvP1z7Ok+dIOe7oOtd7Bypz+OTnf7ZdCrSiUQ+OOc3X+\nHMxc9rtMt0c630x92oyZAtVYbeZN7InAB8aJjAc21zdfJ+hlmU5cdKYRqXmKYfEH\nEmrRygDj37XnUtQ1akkCvZzXt8Qd/IjEazpKaR9s1YLNTf+NwV94qQ==\n-----END CERTIFICATE-----\n",
  "0a03383fa5654d0e87ee6495a8500320522eb5c9": "-----BEGIN CERTIFICATE-----\nMIIC+jCCAeKgAwIBAgIILoG3DNlfB/QwDQYJKoZIhvcNAQEFBQAwIDEeMBwGA1UE\nAxMVMTA2OTQ3MDEyMjYwNDg4NzM2MTU3MB4XDTE5MDcwOTAxMzQyOVoXDTI5MDcw\nNjAxMzQyOVowIDEeMBwGA1UEAxMVMTA2OTQ3MDEyMjYwNDg4NzM2MTU3MIIBIjAN\nBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAsILt7Eo/k1nU6hWmWl8maMw42o5Z\n5Ajqop4rYAAbO7uaWo686KX3t3bBLyVSebCYU2+bmMzEr8uvOF6eu+1mgArUsw7F\n4jAFLK71hnwB8uXz14s7hWvIYvm5GVa4HDhOGZM5iMOsXzDjXWH9dB8u2+wWAp/5\nGtwse3mx89WOTcNiJkO/ZdekG18LXSidRQqROTYBmDMTy/FU/4xhPsqB4ZzH2ueG\norwJHLfZZTzPdrBmo+jGwoSMb069FGdUIP8RMnLCJUmEFdqT2cQn28nEXFMaTOI/\naWuuuM/5vopsKWXMVT3yjjDOXJbw1rar/zJoYOT9VnWAkkcNP5fY6tQbLwIDAQAB\nozgwNjAMBgNVHRMBAf8EAjAAMA4GA1UdDwEB/wQEAwIHgDAWBgNVHSUBAf8EDDAK\nBggrBgEFBQcDAjANBgkqhkiG9w0BAQUFAAOCAQEASJTehr0WmoSALKKDHza/vW5O\nOENvxjRaYiViM1lAKzPVxTBI/mdjCI+9k97ZfZuF2MTu41VWz9RcfjAvBsbkNS8f\n9IJnFU7N/gXKPM/gfG6GvFxr450fhFewcPIlutmFUrXkoJhXUhtb2nwxjTa0za1C\nIe8FqNo2/swL1cN5YqlO7eUk37g50etHHnWyFpHcPmQuKlJdSGfD5IT4/KE2Gi0T\nLVLcNWU/KUJEk62GziCDuWbLzcnynGjeMVzYOHN+4ZyWcuLZGsypBgCK0q3kFkkv\nu4uYcxJVI6J2nRTcMFC2zdqggmkAVTe0wX8HQ5Ns8IRHCUNNTu8jFrKj7IHp0w==\n-----END CERTIFICATE-----\n",
  "aeb0d379d4a71b561b77e1e05fe5a8a0ef2cf089": "-----BEGIN CERTIFICATE-----\nMIIC+jCCAeKgAwIBAgIIJWEK8Mx3fIgwDQYJKoZIhvcNAQEFBQAwIDEeMBwGA1UE\nAxMVMTA2OTQ3MDEyMjYwNDg4NzM2MTU3MB4XDTE2MDMwMTE3NDExM1oXDTI2MDIy\nNzE3NDExM1owIDEeMBwGA1UEAxMVMTA2OTQ3MDEyMjYwNDg4NzM2MTU3MIIBIjAN\nBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAt/D2PDPvrMb4KPJM712b9J3CQDLe\nBN5FxNh0/C72QngZFRyVSdarwen2MlddZGD6zsNSBwDVM0mkGi+fFHOgEe6WRlRT\nYnKUFibhR0Oh+G4jKVeMbed8wWGBhq4Fxo5i3230NMK0Iqkvc83wy1IpCHtK5dU1\natuOunh1DoKV9pM0bZwevHigLoNBjbyzpu6MaMh9WIGZ1JRTBtN1TdXI+yBvoNCA\nG284CjeTy1NTOynj11Nw8FevzeVWsOyKH5kzbvNZICfxeELD3GyQVF7WG5U80c8R\nxpcvm6wkYXoEoeQ9uOey9cJpXfvz1FLMts5IpafEf2STqjfl6vRdfhOOuQIDAQAB\nozgwNjAMBgNVHRMBAf8EAjAAMA4GA1UdDwEB/wQEAwIHgDAWBgNVHSUBAf8EDDAK\nBggrBgEFBQcDAjANBgkqhkiG9w0BAQUFAAOCAQEAWTWrwEoWodbxXBtCrlHTAyrv\nigJio3mMZFVcEQJ23/H83R4+sk2Pri4QnR575Jyz/ddOn/b6QU7j/oYf5PUK93Gg\nTQ/5BZBXGTRno8YM2tO/joiIV3hwKtZNh51uiB+/0zkq76yEQ38lu9ZrRMMlsr85\nZ8tdMf6qlxIlbU46X1Jq0x8ETuhS5qtxKovqo6XwSAvcZaUNf5PgZ7lXEV3i/Og4\nQJGVjh9vuAfanZWMvoaMOtEiYyaRdpKVcXA+Tsrq13XUlk5cUgxrOj0gUO85WiPk\nFGSx3g1sIdsD/bUpy5XGV5CDFN4QUXDvTzwdeFPh8WH/alf1jp2xMoiYYx5HeQ==\n-----END CERTIFICATE-----\n"
}
)";

/**
 * Provide an overloaded << to output a Status to std::ostream for better error
 * messages in test output
 * @param os the std::ostream to write to
 * @param status is the enum status.
 * @return the std::ostream os
 */
std::ostream& operator<<(std::ostream& os, const Status& status) {
  return os << "Status(" << static_cast<int>(status) << ", " << getStatusString(status) << ")";
}

} // namespace JwtVerify
} // namespace Envoy
