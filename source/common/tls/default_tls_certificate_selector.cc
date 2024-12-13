#include "source/common/tls/default_tls_certificate_selector.h"

#include "source/common/tls/utility.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

DefaultTlsCertificateSelector::DefaultTlsCertificateSelector(
    const Ssl::ServerContextConfig& config, Ssl::TlsCertificateSelectorContext& selector_ctx)
    : server_ctx_(dynamic_cast<ServerContextImpl&>(selector_ctx)),
      tls_contexts_(selector_ctx.getTlsContexts()), ocsp_staple_policy_(config.ocspStaplePolicy()),
      full_scan_certs_on_sni_mismatch_(config.fullScanCertsOnSNIMismatch()) {
  for (auto& ctx : tls_contexts_) {
    if (ctx.cert_chain_ == nullptr) {
      continue;
    }
    bssl::UniquePtr<EVP_PKEY> public_key(X509_get_pubkey(ctx.cert_chain_.get()));
    const int pkey_id = EVP_PKEY_id(public_key.get());
    // Load DNS SAN entries and Subject Common Name as server name patterns after certificate
    // chain loaded, and populate ServerNamesMap which will be used to match SNI.
    has_rsa_ |= (pkey_id == EVP_PKEY_RSA);
    populateServerNamesMap(ctx, pkey_id);
  }
};

void DefaultTlsCertificateSelector::populateServerNamesMap(const Ssl::TlsContext& ctx,
                                                           int pkey_id) {
  if (ctx.cert_chain_ == nullptr) {
    return;
  }

  auto populate = [&](const std::string& sn) {
    std::string sn_pattern = sn;
    if (absl::StartsWith(sn, "*.")) {
      sn_pattern = sn.substr(1);
    }
    PkeyTypesMap pkey_types_map;
    // Multiple certs with different key type are allowed for one server name pattern.
    auto sn_match = server_names_map_.try_emplace(sn_pattern, pkey_types_map).first;
    auto pt_match = sn_match->second.find(pkey_id);
    if (pt_match != sn_match->second.end()) {
      // When there are duplicate names, prefer the earlier one.
      //
      // If all of the SANs in a certificate are unused due to duplicates, it could be useful
      // to issue a warning, but that would require additional tracking that hasn't been
      // implemented.
      return;
    }
    sn_match->second.emplace(
        std::pair<int, std::reference_wrapper<const Ssl::TlsContext>>(pkey_id, ctx));
  };

  bssl::UniquePtr<GENERAL_NAMES> san_names(static_cast<GENERAL_NAMES*>(
      X509_get_ext_d2i(ctx.cert_chain_.get(), NID_subject_alt_name, nullptr, nullptr)));
  if (san_names != nullptr) {
    auto dns_sans = Utility::getSubjectAltNames(*ctx.cert_chain_, GEN_DNS);
    // https://www.rfc-editor.org/rfc/rfc6066#section-3
    // Currently, the only server names supported are DNS hostnames, so we
    // only save dns san entries to match SNI.
    for (const auto& san : dns_sans) {
      populate(san);
    }
  } else {
    // https://www.rfc-editor.org/rfc/rfc6125#section-6.4.4
    // As noted, a client MUST NOT seek a match for a reference identifier
    // of CN-ID if the presented identifiers include a DNS-ID, SRV-ID,
    // URI-ID, or any application-specific identifier types supported by the
    // client.
    X509_NAME* cert_subject = X509_get_subject_name(ctx.cert_chain_.get());
    const int cn_index = X509_NAME_get_index_by_NID(cert_subject, NID_commonName, -1);
    if (cn_index >= 0) {
      X509_NAME_ENTRY* cn_entry = X509_NAME_get_entry(cert_subject, cn_index);
      if (cn_entry) {
        ASN1_STRING* cn_asn1 = X509_NAME_ENTRY_get_data(cn_entry);
        if (ASN1_STRING_length(cn_asn1) > 0) {
          std::string subject_cn(reinterpret_cast<const char*>(ASN1_STRING_data(cn_asn1)),
                                 ASN1_STRING_length(cn_asn1));
          populate(subject_cn);
        }
      }
    }
  }
}

Ssl::SelectionResult
DefaultTlsCertificateSelector::selectTlsContext(const SSL_CLIENT_HELLO& ssl_client_hello,
                                                Ssl::CertificateSelectionCallbackPtr) {
  absl::string_view sni =
      absl::NullSafeStringView(SSL_get_servername(ssl_client_hello.ssl, TLSEXT_NAMETYPE_host_name));
  const Ssl::CurveNIDVector client_ecdsa_capabilities =
      server_ctx_.getClientEcdsaCapabilities(ssl_client_hello);
  const bool client_ocsp_capable = server_ctx_.isClientOcspCapable(ssl_client_hello);

  auto [selected_ctx, ocsp_staple_action] =
      findTlsContext(sni, client_ecdsa_capabilities, client_ocsp_capable, nullptr);

  auto stats = server_ctx_.stats();
  if (client_ocsp_capable) {
    stats.ocsp_staple_requests_.inc();
  }

  switch (ocsp_staple_action) {
  case Ssl::OcspStapleAction::Staple:
    stats.ocsp_staple_responses_.inc();
    break;
  case Ssl::OcspStapleAction::NoStaple:
    stats.ocsp_staple_omitted_.inc();
    break;
  case Ssl::OcspStapleAction::Fail:
    stats.ocsp_staple_failed_.inc();
    return {Ssl::SelectionResult::SelectionStatus::Failed, nullptr, false};
  case Ssl::OcspStapleAction::ClientNotCapable:
    // This happens when client does not support OCSP, do nothing.
    break;
  }

  return {Ssl::SelectionResult::SelectionStatus::Success, &selected_ctx,
          ocsp_staple_action == Ssl::OcspStapleAction::Staple};
}

Ssl::OcspStapleAction DefaultTlsCertificateSelector::ocspStapleAction(const Ssl::TlsContext& ctx,
                                                                      bool client_ocsp_capable) {
  if (!client_ocsp_capable) {
    return Ssl::OcspStapleAction::ClientNotCapable;
  }

  auto& response = ctx.ocsp_response_;

  auto policy = ocsp_staple_policy_;
  if (ctx.is_must_staple_) {
    // The certificate has the must-staple extension, so upgrade the policy to match.
    policy = Ssl::ServerContextConfig::OcspStaplePolicy::MustStaple;
  }

  const bool valid_response = response && !response->isExpired();

  switch (policy) {
  case Ssl::ServerContextConfig::OcspStaplePolicy::LenientStapling:
    if (!valid_response) {
      return Ssl::OcspStapleAction::NoStaple;
    }
    return Ssl::OcspStapleAction::Staple;

  case Ssl::ServerContextConfig::OcspStaplePolicy::StrictStapling:
    if (valid_response) {
      return Ssl::OcspStapleAction::Staple;
    }
    if (response) {
      // Expired response.
      return Ssl::OcspStapleAction::Fail;
    }
    return Ssl::OcspStapleAction::NoStaple;

  case Ssl::ServerContextConfig::OcspStaplePolicy::MustStaple:
    if (!valid_response) {
      return Ssl::OcspStapleAction::Fail;
    }
    return Ssl::OcspStapleAction::Staple;
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

std::pair<const Ssl::TlsContext&, Ssl::OcspStapleAction>
DefaultTlsCertificateSelector::findTlsContext(absl::string_view sni,
                                              const Ssl::CurveNIDVector& client_ecdsa_capabilities,
                                              bool client_ocsp_capable, bool* cert_matched_sni) {
  bool unused = false;
  if (cert_matched_sni == nullptr) {
    // Avoid need for nullptr checks when this is set.
    cert_matched_sni = &unused;
  }

  // selected_ctx represents the final selected certificate, it should meet all requirements or pick
  // a candidate.
  const Ssl::TlsContext* selected_ctx = nullptr;
  const Ssl::TlsContext* candidate_ctx = nullptr;
  Ssl::OcspStapleAction ocsp_staple_action;

  // If the capabilities list vector is not empty, then the client is ECDSA-capable.
  const bool client_ecdsa_capable = !client_ecdsa_capabilities.empty();

  auto selected = [&](const Ssl::TlsContext& ctx) -> bool {
    auto action = ocspStapleAction(ctx, client_ocsp_capable);
    if (action == Ssl::OcspStapleAction::Fail) {
      // The selected ctx must adhere to OCSP policy
      return false;
    }
    // If the client is ECDSA-capable and the context is ECDSA, we check if it is capable of
    // handling the curves in the cert in a given TlsContext. If the client is not ECDSA-capable,
    // we will not std::find anything here and move on. If the context is RSA, we will not find
    // the value of `EC_CURVE_INVALID_NID` in an vector of ECDSA capabilities.
    // If we have a matching curve NID in our client capabilities, return `true`.
    if (std::find(client_ecdsa_capabilities.begin(), client_ecdsa_capabilities.end(),
                  ctx.ec_group_curve_name_) != client_ecdsa_capabilities.end()) {
      selected_ctx = &ctx;
      ocsp_staple_action = action;
      return true;
    }

    // If the client is not ECDSA-capable and the `ctx` is non-ECDSA, then select this `ctx`.
    if (!client_ecdsa_capable && ctx.ec_group_curve_name_ == Ssl::EC_CURVE_INVALID_NID) {
      selected_ctx = &ctx;
      ocsp_staple_action = action;
      return true;
    }

    if (client_ecdsa_capable && ctx.ec_group_curve_name_ == Ssl::EC_CURVE_INVALID_NID &&
        candidate_ctx == nullptr) {
      // ECDSA cert is preferred if client is ECDSA capable, so RSA cert is marked as a candidate,
      // searching will continue until exhausting all certs or find a exact match.
      candidate_ctx = &ctx;
      ocsp_staple_action = action;
      return false;
    }

    return false;
  };

  auto select_from_map = [this, &selected](absl::string_view server_name) -> void {
    auto it = server_names_map_.find(server_name);
    if (it == server_names_map_.end()) {
      return;
    }
    const auto& pkey_types_map = it->second;
    for (const auto& entry : pkey_types_map) {
      if (selected(entry.second.get())) {
        break;
      }
    }
  };

  auto tail_select = [&](bool go_to_next_phase) {
    if (selected_ctx == nullptr) {
      selected_ctx = candidate_ctx;
    }

    if (selected_ctx == nullptr && !go_to_next_phase) {
      selected_ctx = &tls_contexts_[0];
      ocsp_staple_action = ocspStapleAction(*selected_ctx, client_ocsp_capable);
    }
  };

  // Select cert based on SNI if SNI is provided by client.
  if (!sni.empty()) {
    // Match on exact server name, i.e. "www.example.com" for "www.example.com".
    select_from_map(sni);
    tail_select(true);

    if (selected_ctx == nullptr) {
      // Match on wildcard domain, i.e. ".example.com" for "www.example.com".
      // https://datatracker.ietf.org/doc/html/rfc6125#section-6.4
      size_t pos = sni.find('.', 1);
      if (pos < sni.size() - 1 && pos != std::string::npos) {
        absl::string_view wildcard = sni.substr(pos);
        select_from_map(wildcard);
      }
    }
    *cert_matched_sni = (selected_ctx != nullptr || candidate_ctx != nullptr);
    // tail_select(full_scan_certs_on_sni_mismatch_);
    tail_select(full_scan_certs_on_sni_mismatch_);
  }
  // Full scan certs if SNI is not provided by client;
  // Full scan certs if client provides SNI but no cert matches to it,
  // it requires full_scan_certs_on_sni_mismatch is enabled.
  if (selected_ctx == nullptr) {
    candidate_ctx = nullptr;
    // Skip loop when there is no cert compatible to key type
    if (client_ecdsa_capable || (!client_ecdsa_capable && has_rsa_)) {
      for (const auto& ctx : tls_contexts_) {
        if (selected(ctx)) {
          break;
        }
      }
    }
    tail_select(false);
  }

  ASSERT(selected_ctx != nullptr);
  return {*selected_ctx, ocsp_staple_action};
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
