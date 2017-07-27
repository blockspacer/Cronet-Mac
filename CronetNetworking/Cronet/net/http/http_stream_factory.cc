// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_stream_factory.h"

#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "net/base/host_mapping_rules.h"
#include "net/base/host_port_pair.h"
#include "net/base/parse_number.h"
#include "net/base/port_util.h"
#include "net/http/http_network_session.h"
#include "net/http/http_response_headers.h"
#if BUILDFLAG(ENABLE_QUIC_SUPPORT)
#include "net/quic/core/quic_packets.h"
#endif
#if BUILDFLAG(ENABLE_SPDY_HTTP2_SUPPORT)
#include "net/spdy/core/spdy_alt_svc_wire_format.h"
#endif
#include "url/gurl.h"

namespace net {

HttpStreamFactory::~HttpStreamFactory() {}

void HttpStreamFactory::ProcessAlternativeServices(
    HttpNetworkSession* session,
    const HttpResponseHeaders* headers,
    const url::SchemeHostPort& http_server) {
  if (!headers->HasHeader(kAlternativeServiceHeader))
    return;

  std::string alternative_service_str;
  headers->GetNormalizedHeader(kAlternativeServiceHeader,
                               &alternative_service_str);
    
#if BUILDFLAG(ENABLE_SPDY_HTTP2_SUPPORT)
  SpdyAltSvcWireFormat::AlternativeServiceVector alternative_service_vector;
  if (!SpdyAltSvcWireFormat::ParseHeaderFieldValue(
          alternative_service_str, &alternative_service_vector)) {
    return;
  }
#endif

  // Convert SpdyAltSvcWireFormat::AlternativeService entries
  // to net::AlternativeServiceInfo.
  AlternativeServiceInfoVector alternative_service_info_vector;
    
#if BUILDFLAG(ENABLE_SPDY_HTTP2_SUPPORT)
  for (const SpdyAltSvcWireFormat::AlternativeService&
           alternative_service_entry : alternative_service_vector) {
    NextProto protocol =
        NextProtoFromString(alternative_service_entry.protocol_id);
    if (!IsAlternateProtocolValid(protocol) ||
        !session->IsProtocolEnabled(protocol) ||
        !IsPortValid(alternative_service_entry.port)) {
      continue;
    }
#if BUILDFLAG(ENABLE_QUIC_SUPPORT)
    // Check if QUIC version is supported. Filter supported QUIC versions.
    QuicVersionVector advertised_versions;
    if (protocol == kProtoQUIC && !alternative_service_entry.version.empty()) {
      bool match_found = false;
      for (QuicVersion supported : session->params().quic_supported_versions) {
        for (uint16_t advertised : alternative_service_entry.version) {
          if (supported == advertised) {
            match_found = true;
            advertised_versions.push_back(supported);
          }
        }
      }
      if (!match_found) {
        continue;
      }
    }
#endif
    AlternativeService alternative_service(protocol,
                                           alternative_service_entry.host,
                                           alternative_service_entry.port);
    base::Time expiration =
        base::Time::Now() +
        base::TimeDelta::FromSeconds(alternative_service_entry.max_age);
    AlternativeServiceInfo alternative_service_info;
    if (protocol == kProtoQUIC) {
#if BUILDFLAG(ENABLE_QUIC_SUPPORT)
      alternative_service_info =
          AlternativeServiceInfo::CreateQuicAlternativeServiceInfo(
              alternative_service, expiration, advertised_versions);
#endif
    } else {
      alternative_service_info =
          AlternativeServiceInfo::CreateHttp2AlternativeServiceInfo(
              alternative_service, expiration);
    }
    alternative_service_info_vector.push_back(alternative_service_info);
  }
#endif
    
  session->http_server_properties()->SetAlternativeServices(
      RewriteHost(http_server), alternative_service_info_vector);
}

HttpStreamFactory::HttpStreamFactory() {}

url::SchemeHostPort HttpStreamFactory::RewriteHost(
    const url::SchemeHostPort& server) {
  HostPortPair host_port_pair(server.host(), server.port());
  const HostMappingRules* mapping_rules = GetHostMappingRules();
  if (mapping_rules)
    mapping_rules->RewriteHost(&host_port_pair);
  return url::SchemeHostPort(server.scheme(), host_port_pair.host(),
                             host_port_pair.port());
}

}  // namespace net
