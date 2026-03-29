# Envoy JWT (JSON Web Token) Architecture

## Overview

Envoy's JWT implementation provides comprehensive support for parsing, validating, and verifying JSON Web Tokens (JWTs) according to RFC 7519. It includes support for multiple signature algorithms, JWKS (JSON Web Key Set) management, audience validation, and time-based constraints.

---

## System Architecture

```mermaid
graph TB
    subgraph "JWT Verification Flow"
        JWTString[JWT String<br/>Header.Payload.Signature]
        Parser[JWT Parser]
        JWTObj[Jwt Object]

        JWKS[JWKS<br/>JSON Web Key Set]
        JWKSParser[JWKS Parser]
        Keys[Public Keys]

        Verifier[JWT Verifier]
        AudCheck[Audience Checker]
        TimeCheck[Time Constraint Validator]
        SigVerify[Signature Verifier]
    end

    subgraph "Results"
        Success[Status::Ok]
        Failure[Status::Error]
    end

    JWTString --> Parser
    Parser --> JWTObj

    JWKS --> JWKSParser
    JWKSParser --> Keys

    JWTObj --> Verifier
    Keys --> Verifier

    Verifier --> TimeCheck
    Verifier --> AudCheck
    Verifier --> SigVerify

    TimeCheck --> Success
    TimeCheck --> Failure
    AudCheck --> Success
    AudCheck --> Failure
    SigVerify --> Success
    SigVerify --> Failure

    style Parser fill:#e1f5ff
    style Verifier fill:#ffe1e1
    style Success fill:#ccffcc
    style Failure fill:#ffcccc
```

---

## 1. Core Components

### Component Hierarchy

```mermaid
classDiagram
    class Jwt {
        +jwt_: string
        +header_str_: string
        +header_pb_: Protobuf::Struct
        +payload_str_: string
        +payload_pb_: Protobuf::Struct
        +signature_: string
        +alg_: string
        +kid_: string
        +iss_: string
        +audiences_: vector~string~
        +sub_: string
        +iat_: uint64_t
        +nbf_: uint64_t
        +exp_: uint64_t
        +jti_: string
        +parseFromString(jwt) Status
        +verifyTimeConstraint(now, clock_skew) Status
    }

    class Jwks {
        -keys_: vector~PubkeyPtr~
        +createFrom(pkey, type) unique_ptr~Jwks~
        +createFromPem(pkey, kid, alg) unique_ptr~Jwks~
        +addKeyFromPem(pkey, kid, alg) Status
        +keys() vector~PubkeyPtr~
        -createFromJwksCore(pkey_jwks) void
        -createFromPemCore(pkey_pem) void
    }

    class Pubkey {
        +hmac_key_: string
        +kid_: string
        +kty_: string
        +alg_: string
        +crv_: string
        +rsa_: UniquePtr~RSA~
        +ec_key_: UniquePtr~EC_KEY~
        +okp_key_raw_: string
        +bio_: UniquePtr~BIO~
        +x509_: UniquePtr~X509~
    }

    class CheckAudience {
        -config_audiences_: set~string~
        +CheckAudience(audiences) Constructor
        +areAudiencesAllowed(jwt_audiences) bool
        +empty() bool
    }

    class Status {
        <<enumeration>>
        Ok
        JwtMissed
        JwtNotYetValid
        JwtExpired
        JwtBadFormat
        JwtVerificationFail
        ...70+ error codes
    }

    class WithStatus {
        -status_: Status
        +getStatus() Status
        #updateStatus(status) void
        #resetStatus(status) void
    }

    Jwks --> Pubkey: contains
    Jwks --|> WithStatus
    CheckAudience --> Status: uses
    Jwt --> Status: returns

    note for Jwt "Parses and holds JWT data\nValidates time constraints"
    note for Jwks "Manages public keys for verification"
    note for Pubkey "Holds cryptographic key material"
```

---

## 2. JWT Structure and Parsing

### JWT Format

```
eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2MjM5MDIyfQ.SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c
│                                     │                                                                             │
│          Header (Base64URL)         │                    Payload (Base64URL)                                      │        Signature (Base64URL)
└─────────────────────────────────────┴─────────────────────────────────────────────────────────────────────────────┴──────────────────────────────
```

### Parsing Flow

```mermaid
sequenceDiagram
    participant Client
    participant JWT as Jwt Object
    participant Parser as parseFromString
    participant HeaderParser
    participant PayloadParser

    Client->>JWT: parseFromString(jwt_string)
    JWT->>Parser: Split by '.'

    alt Invalid format
        Parser-->>Client: Status::JwtBadFormat
    end

    Parser->>HeaderParser: Parse header
    HeaderParser->>HeaderParser: Base64URL decode
    HeaderParser->>HeaderParser: JSON parse to Struct
    HeaderParser->>HeaderParser: Extract "alg"
    HeaderParser->>HeaderParser: Extract "kid" (optional)

    alt Header parsing fails
        HeaderParser-->>Client: Status::JwtHeader*Error
    end

    Parser->>PayloadParser: Parse payload
    PayloadParser->>PayloadParser: Base64URL decode
    PayloadParser->>PayloadParser: JSON parse to Struct
    PayloadParser->>PayloadParser: Extract standard claims
    Note over PayloadParser: iss, sub, aud, exp, nbf, iat, jti

    alt Payload parsing fails
        PayloadParser-->>Client: Status::JwtPayload*Error
    end

    Parser->>Parser: Parse signature (Base64URL decode)

    alt Signature parsing fails
        Parser-->>Client: Status::JwtSignatureParseErrorBadBase64
    end

    Parser-->>Client: Status::Ok
```

### Supported Standard Claims

| Claim | Type | Description | Validation |
|-------|------|-------------|------------|
| **alg** | string | Algorithm (header) | Required, must be implemented |
| **kid** | string | Key ID (header) | Optional |
| **iss** | string | Issuer | Optional, must be string if present |
| **sub** | string | Subject | Optional, must be string if present |
| **aud** | string or array | Audience | Optional, string or string array |
| **exp** | number | Expiration time | Optional, must be uint64, validated against current time |
| **nbf** | number | Not before | Optional, must be uint64, validated against current time |
| **iat** | number | Issued at | Optional, must be uint64 |
| **jti** | string | JWT ID | Optional, must be string if present |

---

## 3. Supported Algorithms

### Algorithm Support Matrix

```mermaid
graph TB
    subgraph "RSA Algorithms"
        RS256[RS256<br/>RSASSA-PKCS1-v1_5<br/>SHA-256]
        RS384[RS384<br/>RSASSA-PKCS1-v1_5<br/>SHA-384]
        RS512[RS512<br/>RSASSA-PKCS1-v1_5<br/>SHA-512]
        PS256[PS256<br/>RSASSA-PSS<br/>SHA-256]
        PS384[PS384<br/>RSASSA-PSS<br/>SHA-384]
        PS512[PS512<br/>RSASSA-PSS<br/>SHA-512]
    end

    subgraph "EC Algorithms"
        ES256[ES256<br/>ECDSA P-256<br/>SHA-256]
        ES384[ES384<br/>ECDSA P-384<br/>SHA-384]
        ES512[ES512<br/>ECDSA P-521<br/>SHA-512]
    end

    subgraph "HMAC Algorithms"
        HS256[HS256<br/>HMAC<br/>SHA-256]
        HS384[HS384<br/>HMAC<br/>SHA-384]
        HS512[HS512<br/>HMAC<br/>SHA-512]
    end

    subgraph "EdDSA Algorithm"
        EdDSA[EdDSA<br/>Ed25519<br/>Curve25519]
    end

    style RS256 fill:#e1f5ff
    style ES256 fill:#ffe1e1
    style HS256 fill:#e1ffe1
    style EdDSA fill:#ffe1ff
```

### Algorithm Implementation Details

| Algorithm | Key Type | Hash Function | Padding | Key Size |
|-----------|----------|---------------|---------|----------|
| **RS256** | RSA | SHA-256 | PKCS1 v1.5 | ≥2048 bits recommended |
| **RS384** | RSA | SHA-384 | PKCS1 v1.5 | ≥2048 bits recommended |
| **RS512** | RSA | SHA-512 | PKCS1 v1.5 | ≥2048 bits recommended |
| **PS256** | RSA | SHA-256 | PSS | ≥2048 bits recommended |
| **PS384** | RSA | SHA-384 | PSS | ≥2048 bits recommended |
| **PS512** | RSA | SHA-512 | PSS | ≥2048 bits recommended |
| **ES256** | EC | SHA-256 | N/A | P-256 (secp256r1) |
| **ES384** | EC | SHA-384 | N/A | P-384 (secp384r1) |
| **ES512** | EC | SHA-512 | N/A | P-521 (secp521r1) |
| **HS256** | Symmetric | SHA-256 | N/A | ≥256 bits recommended |
| **HS384** | Symmetric | SHA-384 | N/A | ≥384 bits recommended |
| **HS512** | Symmetric | SHA-512 | N/A | ≥512 bits recommended |
| **EdDSA** | OKP | N/A | N/A | Ed25519 (32 bytes) |

---

## 4. JWKS (JSON Web Key Set)

### JWKS Structure

```json
{
  "keys": [
    {
      "kty": "RSA",
      "use": "sig",
      "kid": "my-key-id",
      "alg": "RS256",
      "n": "0vx7agoebGcQSuuPiLJXZptN9nndr...",
      "e": "AQAB"
    },
    {
      "kty": "EC",
      "use": "sig",
      "kid": "ec-key-id",
      "alg": "ES256",
      "crv": "P-256",
      "x": "WKn-ZIGevcwGIyyrzFoZNBdaq9_TsqzGl96oc0CWuis",
      "y": "y77t-RvAHRKTsSGdIYUfweuOvwrvDD-Q3Hv5J0fSKbE"
    }
  ]
}
```

### JWKS Parsing

```mermaid
flowchart TD
    Start[Parse JWKS] --> ParseJSON{Parse JSON}

    ParseJSON -->|Fail| ErrorJSON[Status::JwksParseError]
    ParseJSON -->|Success| CheckKeys{Has "keys"?}

    CheckKeys -->|No| ErrorNoKeys[Status::JwksNoKeys]
    CheckKeys -->|Yes| CheckArray{Is array?}

    CheckArray -->|No| ErrorBadKeys[Status::JwksBadKeys]
    CheckArray -->|Yes| IterateKeys[Iterate keys]

    IterateKeys --> CheckKty{Has "kty"?}

    CheckKty -->|No| SkipKey[Skip key]
    CheckKty -->|Yes| ParseByType{Key Type}

    ParseByType -->|RSA| ParseRSA[Parse RSA Key]
    ParseByType -->|EC| ParseEC[Parse EC Key]
    ParseByType -->|oct| ParseOct[Parse Symmetric Key]
    ParseByType -->|OKP| ParseOKP[Parse OKP Key]
    ParseByType -->|Unknown| SkipKey

    ParseRSA --> ValidateRSA{Valid RSA?}
    ParseEC --> ValidateEC{Valid EC?}
    ParseOct --> ValidateOct{Valid Oct?}
    ParseOKP --> ValidateOKP{Valid OKP?}

    ValidateRSA -->|Yes| AddKey[Add to keys_]
    ValidateEC -->|Yes| AddKey
    ValidateOct -->|Yes| AddKey
    ValidateOKP -->|Yes| AddKey

    ValidateRSA -->|No| SkipKey
    ValidateEC -->|No| SkipKey
    ValidateOct -->|No| SkipKey
    ValidateOKP -->|No| SkipKey

    SkipKey --> MoreKeys{More keys?}
    AddKey --> MoreKeys

    MoreKeys -->|Yes| IterateKeys
    MoreKeys -->|No| CheckValid{Any valid keys?}

    CheckValid -->|No| ErrorNoValid[Status::JwksNoValidKeys]
    CheckValid -->|Yes| Success[Status::Ok]

    style Success fill:#ccffcc
    style ErrorJSON fill:#ffcccc
    style ErrorNoKeys fill:#ffcccc
    style ErrorBadKeys fill:#ffcccc
    style ErrorNoValid fill:#ffcccc
```

### Key Type Parsing Details

#### RSA Key Parsing
```mermaid
graph LR
    RSA[RSA Key] --> CheckAlg{Check alg}
    CheckAlg -->|RS*/PS*| CheckN{Has "n"?}
    CheckAlg -->|Other| Fail1[Error: Bad alg]

    CheckN -->|Yes| CheckE{Has "e"?}
    CheckN -->|No| Fail2[Error: Missing n]

    CheckE -->|Yes| DecodeN[Base64 decode n]
    CheckE -->|No| Fail3[Error: Missing e]

    DecodeN --> DecodeE[Base64 decode e]
    DecodeE --> CreateRSA[Create RSA object]
    CreateRSA --> SetNE[Set n and e]
    SetNE --> Success[Valid RSA key]

    style Success fill:#ccffcc
    style Fail1 fill:#ffcccc
    style Fail2 fill:#ffcccc
    style Fail3 fill:#ffcccc
```

#### EC Key Parsing
```mermaid
graph LR
    EC[EC Key] --> CheckAlg{Check alg}
    CheckAlg -->|ES*| CheckCrv{Has "crv"?}
    CheckAlg -->|Other| Fail1[Error: Bad alg]

    CheckCrv -->|Yes| ValidateCrv{Validate crv}
    CheckCrv -->|No| Fail2[Error: Missing crv]

    ValidateCrv -->|P-256/P-384/P-521| CheckX{Has "x"?}
    ValidateCrv -->|Other| Fail3[Error: Unsupported crv]

    CheckX -->|Yes| CheckY{Has "y"?}
    CheckX -->|No| Fail4[Error: Missing x]

    CheckY -->|Yes| DecodeXY[Base64 decode x, y]
    CheckY -->|No| Fail5[Error: Missing y]

    DecodeXY --> CreateEC[Create EC_KEY]
    CreateEC --> SetXY[Set x, y coordinates]
    SetXY --> Success[Valid EC key]

    style Success fill:#ccffcc
    style Fail1 fill:#ffcccc
    style Fail2 fill:#ffcccc
    style Fail3 fill:#ffcccc
    style Fail4 fill:#ffcccc
    style Fail5 fill:#ffcccc
```

---

## 5. JWT Verification Process

### Complete Verification Flow

```mermaid
sequenceDiagram
    participant Client
    participant Verify as verifyJwt
    participant JWT as Jwt Object
    participant JWKS as Jwks Object
    participant SigVerify as Signature Verifier
    participant TimeCheck as Time Validator
    participant AudCheck as Audience Checker

    Client->>Verify: verifyJwt(jwt, jwks, audiences, now)

    Note over Verify: Step 1: Signature Verification

    Verify->>JWKS: Get keys()
    Verify->>JWT: Get alg_, kid_, signature_

    loop For each key in JWKS
        alt kid matches or not specified
            alt alg matches
                Verify->>SigVerify: Verify signature
                SigVerify->>SigVerify: Select algorithm
                alt RSA (RS*, PS*)
                    SigVerify->>SigVerify: verifySignatureRSA/PSS
                else EC (ES*)
                    SigVerify->>SigVerify: verifySignatureEC
                else HMAC (HS*)
                    SigVerify->>SigVerify: verifySignatureOct
                else EdDSA
                    SigVerify->>SigVerify: verifySignatureEd25519
                end

                alt Signature valid
                    Note over SigVerify: Found matching key
                    SigVerify-->>Verify: Success
                else Continue to next key
                    SigVerify-->>Verify: Try next
                end
            end
        end
    end

    alt No matching key found
        Verify-->>Client: Status::JwksKidAlgMismatch
    end

    Note over Verify: Step 2: Time Validation

    Verify->>TimeCheck: verifyTimeConstraint(now, clock_skew)
    TimeCheck->>TimeCheck: Check nbf (not before)

    alt now + clock_skew < nbf
        TimeCheck-->>Verify: Status::JwtNotYetValid
        Verify-->>Client: Status::JwtNotYetValid
    end

    TimeCheck->>TimeCheck: Check exp (expiration)

    alt exp > 0 && now > exp + clock_skew
        TimeCheck-->>Verify: Status::JwtExpired
        Verify-->>Client: Status::JwtExpired
    end

    TimeCheck-->>Verify: Status::Ok

    Note over Verify: Step 3: Audience Validation

    alt audiences not empty
        Verify->>AudCheck: areAudiencesAllowed(jwt.audiences_)
        AudCheck->>AudCheck: Check intersection

        alt No match found
            AudCheck-->>Verify: false
            Verify-->>Client: Status::JwtAudienceNotAllowed
        end

        AudCheck-->>Verify: true
    end

    Verify-->>Client: Status::Ok
```

---

## 6. Time Constraint Validation

### Clock Skew Handling

```mermaid
graph TB
    subgraph "Timeline"
        Past[Past] --> NotBefore[nbf - clock_skew]
        NotBefore --> Valid1[Valid Period]
        Valid1 --> Issued[iat - issued at]
        Issued --> Valid2[Valid Period]
        Valid2 --> Expiry[exp + clock_skew]
        Expiry --> Future[Future]
    end

    subgraph "Validation Rules"
        Rule1[now < nbf - clock_skew<br/>→ JwtNotYetValid]
        Rule2[nbf - clock_skew ≤ now ≤ exp + clock_skew<br/>→ Valid]
        Rule3[now > exp + clock_skew<br/>→ JwtExpired]
    end

    style Valid1 fill:#ccffcc
    style Valid2 fill:#ccffcc
    style Past fill:#ffcccc
    style Future fill:#ffcccc
    style Rule2 fill:#ccffcc
```

### Time Validation Logic

```cpp
// Default clock skew: 60 seconds
constexpr uint64_t kClockSkewInSecond = 60;

Status verifyTimeConstraint(uint64_t now, uint64_t clock_skew) {
    // Check Not Before (nbf)
    if (now + clock_skew < nbf_) {
        return Status::JwtNotYetValid;
    }

    // Check Expiration (exp)
    if (exp_ && now > exp_ + clock_skew) {
        return Status::JwtExpired;
    }

    return Status::Ok;
}
```

**Examples:**

| Scenario | nbf | exp | now | clock_skew | Result |
|----------|-----|-----|-----|------------|--------|
| Valid | 1000 | 2000 | 1500 | 60 | Ok |
| Too early | 1000 | 2000 | 930 | 60 | JwtNotYetValid |
| Just before nbf | 1000 | 2000 | 950 | 60 | Ok (within skew) |
| Expired | 1000 | 2000 | 2070 | 60 | JwtExpired |
| Just after exp | 1000 | 2000 | 2050 | 60 | Ok (within skew) |
| No expiration | 1000 | 0 | 9999 | 60 | Ok |

---

## 7. Audience Validation

### Audience Matching Logic

```mermaid
flowchart TD
    Start[Check Audience] --> Empty{Config audiences<br/>empty?}

    Empty -->|Yes| AllowAll[Allow all<br/>Return true]
    Empty -->|No| NormalizeConfig[Normalize config audiences<br/>Remove scheme & trailing slash]

    NormalizeConfig --> GetJWT[Get JWT audiences]
    GetJWT --> NormalizeJWT[Normalize JWT audiences<br/>Remove scheme & trailing slash]

    NormalizeJWT --> Compare{Any intersection?}

    Compare -->|Yes| Allow[Return true]
    Compare -->|No| Deny[Return false]

    style AllowAll fill:#ccffcc
    style Allow fill:#ccffcc
    style Deny fill:#ffcccc
```

### Audience Normalization

```
Original: "https://api.example.com/v1/"
Normalized: "api.example.com/v1"

Original: "http://service.local/"
Normalized: "service.local"

Original: "urn:example:service"
Normalized: "urn:example:service" (no change)
```

**Why normalize?**
- Users often add wrong scheme (http vs https)
- Trailing slashes cause mismatches
- Improves user experience without compromising security
- Still case-sensitive comparison per RFC 7519

---

## 8. Signature Verification Details

### RSA Signature Verification

```mermaid
sequenceDiagram
    participant Verifier
    participant OpenSSL
    participant RSA as RSA Key

    Verifier->>OpenSSL: Create EVP_PKEY from RSA
    Verifier->>OpenSSL: Create EVP_MD_CTX

    alt PKCS1 v1.5 (RS256/384/512)
        Verifier->>OpenSSL: EVP_DigestVerifyInit(md_ctx, md, pkey)
        Verifier->>OpenSSL: EVP_DigestVerifyUpdate(signed_data)
        Verifier->>OpenSSL: EVP_DigestVerifyFinal(signature)
    else PSS (PS256/384/512)
        Verifier->>OpenSSL: EVP_DigestVerifyInit(md_ctx, &pctx, md, pkey)
        Verifier->>OpenSSL: EVP_PKEY_CTX_set_rsa_padding(RSA_PKCS1_PSS_PADDING)
        Verifier->>OpenSSL: EVP_PKEY_CTX_set_rsa_mgf1_md(md)
        Verifier->>OpenSSL: EVP_DigestVerify(signature, signed_data)
    end

    OpenSSL-->>Verifier: Verification result
```

### EC Signature Verification

```mermaid
sequenceDiagram
    participant Verifier
    participant OpenSSL
    participant EC as EC_KEY

    Note over Verifier: EC signature is r || s

    Verifier->>OpenSSL: Create EVP_MD_CTX
    Verifier->>OpenSSL: EVP_DigestInit(md)
    Verifier->>OpenSSL: EVP_DigestUpdate(signed_data)
    Verifier->>OpenSSL: EVP_DigestFinal(digest)

    Verifier->>OpenSSL: Create ECDSA_SIG
    Verifier->>OpenSSL: BN_bin2bn(signature[:len/2]) → r
    Verifier->>OpenSSL: BN_bin2bn(signature[len/2:]) → s
    Verifier->>OpenSSL: ECDSA_SIG_set0(ecdsa_sig, r, s)

    Verifier->>OpenSSL: ECDSA_do_verify(digest, ecdsa_sig, ec_key)

    OpenSSL-->>Verifier: Verification result
```

### HMAC Signature Verification

```mermaid
sequenceDiagram
    participant Verifier
    participant OpenSSL

    Verifier->>OpenSSL: HMAC(md, key, signed_data)
    OpenSSL-->>Verifier: computed_hmac

    Verifier->>Verifier: Compare lengths
    alt Lengths differ
        Verifier->>Verifier: Return false
    end

    Verifier->>OpenSSL: CRYPTO_memcmp(computed, signature)
    OpenSSL-->>Verifier: Comparison result

    Note over Verifier: Constant-time comparison<br/>Prevents timing attacks
```

### EdDSA (Ed25519) Verification

```mermaid
flowchart TD
    Start[Verify Ed25519] --> CheckLen{Signature length<br/>== 64 bytes?}

    CheckLen -->|No| Error[Status::JwtEd25519SignatureWrongLength]
    CheckLen -->|Yes| Verify[ED25519_verify signature, signed_data, key]

    Verify --> Result{Result == 1?}

    Result -->|Yes| Success[Status::Ok]
    Result -->|No| Fail[Status::JwtVerificationFail]

    style Success fill:#ccffcc
    style Error fill:#ffcccc
    style Fail fill:#ffcccc
```

---

## 9. Error Handling

### Status Codes Hierarchy

```mermaid
graph TB
    Status[Status Enum<br/>70+ error codes]

    subgraph "JWT Errors"
        JwtMissed
        JwtNotYetValid
        JwtExpired
        JwtBadFormat
        JwtHeader[Header Errors<br/>5 types]
        JwtPayload[Payload Errors<br/>14 types]
        JwtSignature[Signature Errors<br/>2 types]
        JwtVerification[Verification Errors<br/>4 types]
    end

    subgraph "JWKS Errors"
        JwksParseError
        JwksNoKeys
        JwksBadKeys
        JwksRSA[RSA Key Errors<br/>8 types]
        JwksEC[EC Key Errors<br/>11 types]
        JwksHMAC[HMAC Key Errors<br/>4 types]
        JwksOKP[OKP Key Errors<br/>7 types]
        JwksX509[X509 Errors<br/>3 types]
        JwksPEM[PEM Errors<br/>3 types]
    end

    Status --> JWT Errors
    Status --> JWKS Errors

    style Status fill:#e1f5ff
```

### Error Classification

| Category | Count | Examples |
|----------|-------|----------|
| **JWT Format** | 15 | JwtBadFormat, JwtHeaderBadAlg, JwtPayloadParseErrorExpNotInteger |
| **Time Validation** | 2 | JwtNotYetValid, JwtExpired |
| **Verification** | 4 | JwtVerificationFail, JwtAudienceNotAllowed, JwtUnknownIssuer |
| **JWKS Parsing** | 3 | JwksParseError, JwksNoKeys, JwksBadKeys |
| **Key Validation** | 45+ | Algorithm-specific errors for RSA, EC, HMAC, OKP |

---

## 10. LRU Cache for JWKS

### SimpleLRUCache Usage

```mermaid
graph LR
    subgraph "JWKS Caching"
        Request[JWKS Request]
        Cache[SimpleLRUCache]
        Remote[Remote JWKS Server]
    end

    Request --> Check{In cache?}
    Check -->|Hit| Return[Return cached JWKS]
    Check -->|Miss| Fetch[Fetch from remote]

    Fetch --> Remote
    Remote --> Parse[Parse JWKS]
    Parse --> Store[Store in cache]
    Store --> Return

    Return --> Evict{Cache full?}
    Evict -->|Yes| LRU[Evict least recently used]
    Evict -->|No| Done[Done]

    style Return fill:#ccffcc
    style Fetch fill:#ffffcc
```

### Cache Characteristics

- **LRU (Least Recently Used)** eviction policy
- **Thread-safe** implementation
- **Configurable size** limit
- **Custom hash functions** support
- **Custom deleters** for cleanup

---

## 11. Utility Components

### StructUtils - Protobuf Helper

```mermaid
classDiagram
    class StructUtils {
        -struct_pb_: Protobuf::Struct
        +GetString(name, value) FindResult
        +GetUInt64(name, value) FindResult
        +GetDouble(name, value) FindResult
        +GetBoolean(name, value) FindResult
        +GetStringList(name, list) FindResult
        +GetValue(nested_names, found) FindResult
    }

    class FindResult {
        <<enumeration>>
        OK
        MISSING
        WRONG_TYPE
        OUT_OF_RANGE
    }

    StructUtils --> FindResult
```

**Purpose:**
- Safe extraction from Protobuf Struct
- Type checking
- Range validation
- Support for nested fields

---

## 12. Integration Example

### Typical Usage Pattern

```cpp
// 1. Parse JWT
Jwt jwt;
Status status = jwt.parseFromString(jwt_string);
if (status != Status::Ok) {
    return status;
}

// 2. Load JWKS
auto jwks = Jwks::createFrom(jwks_json, Jwks::JWKS);
if (jwks->getStatus() != Status::Ok) {
    return jwks->getStatus();
}

// 3. Verify with all checks
std::vector<std::string> audiences = {"my-api", "my-service"};
uint64_t now = absl::ToUnixSeconds(absl::Now());

status = verifyJwt(jwt, *jwks, audiences, now);
if (status != Status::Ok) {
    return status;
}

// 4. Use JWT claims
std::string issuer = jwt.iss_;
std::string subject = jwt.sub_;
// Access custom claims from jwt.payload_pb_
```

---

## 13. Security Considerations

### Built-in Security Features

```mermaid
graph TB
    subgraph "Security Measures"
        Timing[Constant-Time Comparison<br/>HMAC verification]
        ClockSkew[Clock Skew Tolerance<br/>60 second default]
        Normalization[Audience Normalization<br/>Scheme/slash removal]
        TypeCheck[Strict Type Checking<br/>All claim types validated]
        AlgCheck[Algorithm Validation<br/>Only implemented algs allowed]
        KeyMatch[Key Matching<br/>kid and alg verification]
    end

    subgraph "Protections Against"
        TimingAttack[Timing Attacks]
        ClockDrift[Clock Drift Issues]
        UserErrors[Configuration Errors]
        TypeConfusion[Type Confusion]
        AlgSubstitution[Algorithm Substitution]
        KeyConfusion[Key Confusion Attacks]
    end

    Timing --> TimingAttack
    ClockSkew --> ClockDrift
    Normalization --> UserErrors
    TypeCheck --> TypeConfusion
    AlgCheck --> AlgSubstitution
    KeyMatch --> KeyConfusion

    style TimingAttack fill:#ffcccc
    style TypeConfusion fill:#ffcccc
    style AlgSubstitution fill:#ffcccc
```

### Best Practices

1. **Algorithm Selection**
   - Prefer RS256 or ES256 for asymmetric
   - Use HS256 only with strong keys (≥256 bits)
   - Avoid deprecated algorithms

2. **Key Management**
   - Rotate keys regularly
   - Use kid to identify keys
   - Store keys securely

3. **Time Validation**
   - Always validate exp claim
   - Use appropriate clock skew
   - Consider nbf for future-dated tokens

4. **Audience Validation**
   - Always validate aud claim
   - Use specific audiences (not wildcards)
   - Normalize to reduce config errors

---

## 14. Performance Characteristics

### Parsing Performance

| Operation | Complexity | Typical Time |
|-----------|-----------|--------------|
| JWT Parse | O(n) | ~50-100 μs |
| JWKS Parse | O(k) | ~1-10 ms |
| Signature Verify | O(1) | ~100-500 μs |
| Time Check | O(1) | ~1 μs |
| Audience Check | O(n*m) | ~10 μs |

**Where:**
- n = JWT size
- k = number of keys in JWKS
- m = number of audiences

### Memory Usage

| Component | Size | Notes |
|-----------|------|-------|
| Jwt object | ~2-5 KB | Includes parsed structures |
| Pubkey | ~1-2 KB | Per key in JWKS |
| Cache entry | ~3-7 KB | Jwt + metadata |

---

## Summary

Envoy's JWT implementation provides:

1. **Comprehensive Support**
   - 13 signature algorithms (RSA, EC, HMAC, EdDSA)
   - Complete RFC 7519 compliance
   - JWKS and PEM key formats

2. **Robust Validation**
   - Signature verification with OpenSSL
   - Time constraint checking with clock skew
   - Audience validation with normalization
   - Type-safe claim extraction

3. **Production Ready**
   - 70+ specific error codes
   - Constant-time HMAC comparison
   - Graceful error handling
   - LRU caching support

4. **Developer Friendly**
   - Clear status codes
   - Flexible verification APIs
   - Support for multiple keys
   - PEM and JWKS formats

5. **Performance Optimized**
   - Efficient parsing
   - OpenSSL for crypto
   - Optional caching
   - Minimal allocations

This implementation enables Envoy to securely authenticate and authorize requests using industry-standard JWT tokens with high performance and comprehensive error handling.
