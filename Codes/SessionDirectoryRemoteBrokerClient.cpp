#include "SessionDirectoryRemoteBrokerClient.h"
#include "NetworkSessionCore.h"
#include <atomic>
#include <map>
#include <random>
#include <sstream>
#include <utility>

#ifdef _WIN32
#include <Windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#endif

namespace ToolKit::ToolKitNetworking {
namespace {
using KeyValueMap = std::map<String, String>;
constexpr const char *BrokerWireProtocolVersion = "1";

String BoolToWire(bool value) { return value ? "1" : "0"; }

bool FillSecureRandomBytes(unsigned char *buffer, size_t size) {
#ifdef _WIN32
  return BCryptGenRandom(nullptr, buffer, static_cast<ULONG>(size),
                         BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
  std::random_device randomDevice;
  for (size_t idx = 0; idx < size; ++idx) {
    buffer[idx] = static_cast<unsigned char>(randomDevice() & 0xFF);
  }
  return true;
#endif
}

char NibbleToHex(unsigned char value) {
  return value < 10 ? static_cast<char>('0' + value)
                    : static_cast<char>('A' + (value - 10));
}

bool HexToNibble(char ch, unsigned char &value) {
  if (ch >= '0' && ch <= '9') {
    value = static_cast<unsigned char>(ch - '0');
    return true;
  }
  if (ch >= 'A' && ch <= 'F') {
    value = static_cast<unsigned char>(10 + (ch - 'A'));
    return true;
  }
  if (ch >= 'a' && ch <= 'f') {
    value = static_cast<unsigned char>(10 + (ch - 'a'));
    return true;
  }

  return false;
}

String EncodeWireValue(const String &value) {
  String encoded;
  encoded.reserve(value.size());
  for (unsigned char ch : value) {
    const bool isAlphaNumeric =
        (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9');
    const bool isSafePunctuation = ch == '-' || ch == '_' || ch == '.' ||
                                   ch == '~' || ch == ':';
    if (isAlphaNumeric || isSafePunctuation) {
      encoded += static_cast<char>(ch);
      continue;
    }

    encoded += '%';
    encoded += NibbleToHex(static_cast<unsigned char>((ch >> 4) & 0x0F));
    encoded += NibbleToHex(static_cast<unsigned char>(ch & 0x0F));
  }

  return encoded;
}

bool DecodeWireValue(const String &encoded, String &decoded) {
  decoded.clear();
  decoded.reserve(encoded.size());
  for (size_t idx = 0; idx < encoded.size(); ++idx) {
    const unsigned char ch = static_cast<unsigned char>(encoded[idx]);
    if (ch != '%') {
      decoded += static_cast<char>(ch);
      continue;
    }

    if (idx + 2 >= encoded.size()) {
      return false;
    }

    unsigned char high = 0;
    unsigned char low = 0;
    if (!HexToNibble(encoded[idx + 1], high) ||
        !HexToNibble(encoded[idx + 2], low)) {
      return false;
    }

    decoded += static_cast<char>((high << 4) | low);
    idx += 2;
  }

  return true;
}

String HostingModeToWire(HostingMode mode) {
  switch (mode) {
  case HostingMode::Client:
    return "Client";
  case HostingMode::DedicatedServer:
    return "DedicatedServer";
  case HostingMode::ListenServer:
    return "ListenServer";
  case HostingMode::None:
  default:
    return "None";
  }
}

HostingMode HostingModeFromWire(const String &value) {
  if (value == "Client") {
    return HostingMode::Client;
  }
  if (value == "DedicatedServer") {
    return HostingMode::DedicatedServer;
  }
  if (value == "ListenServer") {
    return HostingMode::ListenServer;
  }
  return HostingMode::None;
}

bool TryParseHostingMode(const String &value, HostingMode &mode) {
  if (value == "None") {
    mode = HostingMode::None;
    return true;
  }
  if (value == "Client") {
    mode = HostingMode::Client;
    return true;
  }
  if (value == "DedicatedServer") {
    mode = HostingMode::DedicatedServer;
    return true;
  }
  if (value == "ListenServer") {
    mode = HostingMode::ListenServer;
    return true;
  }

  return false;
}

String JoinMethodToWire(JoinMethod method) {
  switch (method) {
  case JoinMethod::DirectAddress:
    return "DirectAddress";
  case JoinMethod::SessionDirectory:
    return "SessionDirectory";
  case JoinMethod::LanDiscovery:
    return "LanDiscovery";
  case JoinMethod::BrokeredHostedSession:
    return "BrokeredHostedSession";
  default:
    return "SessionDirectory";
  }
}

bool TryParseJoinMethod(const String &value, JoinMethod &method) {
  if (value == "DirectAddress") {
    method = JoinMethod::DirectAddress;
    return true;
  }
  if (value == "SessionDirectory") {
    method = JoinMethod::SessionDirectory;
    return true;
  }
  if (value == "LanDiscovery") {
    method = JoinMethod::LanDiscovery;
    return true;
  }
  if (value == "BrokeredHostedSession") {
    method = JoinMethod::BrokeredHostedSession;
    return true;
  }

  return false;
}

bool TryParseResolvedRouteKind(const String &value, ResolvedRouteKind &kind) {
  if (value == "Direct") {
    kind = ResolvedRouteKind::Direct;
    return true;
  }
  if (value == "Unknown") {
    kind = ResolvedRouteKind::Unknown;
    return true;
  }

  return false;
}

SessionDirectoryBrokerError BrokerErrorFromWire(const String &value) {
  if (value == "InvalidRequest") {
    return SessionDirectoryBrokerError::InvalidRequest;
  }
  if (value == "SessionNotFound") {
    return SessionDirectoryBrokerError::SessionNotFound;
  }
  if (value == "SessionAlreadyExists") {
    return SessionDirectoryBrokerError::SessionAlreadyExists;
  }
  if (value == "VersionMismatch") {
    return SessionDirectoryBrokerError::VersionMismatch;
  }
  if (value == "Unauthorized") {
    return SessionDirectoryBrokerError::Unauthorized;
  }
  if (value == "RateLimited") {
    return SessionDirectoryBrokerError::RateLimited;
  }
  if (value == "Timeout") {
    return SessionDirectoryBrokerError::Timeout;
  }
  if (value == "ServiceUnavailable") {
    return SessionDirectoryBrokerError::ServiceUnavailable;
  }
  if (value == "ProtocolError") {
    return SessionDirectoryBrokerError::ProtocolError;
  }
  return SessionDirectoryBrokerError::None;
}

bool TryParseBrokerError(const String &value, SessionDirectoryBrokerError &error) {
  if (value == "None") {
    error = SessionDirectoryBrokerError::None;
    return true;
  }
  if (value == "InvalidRequest") {
    error = SessionDirectoryBrokerError::InvalidRequest;
    return true;
  }
  if (value == "SessionNotFound") {
    error = SessionDirectoryBrokerError::SessionNotFound;
    return true;
  }
  if (value == "SessionAlreadyExists") {
    error = SessionDirectoryBrokerError::SessionAlreadyExists;
    return true;
  }
  if (value == "VersionMismatch") {
    error = SessionDirectoryBrokerError::VersionMismatch;
    return true;
  }
  if (value == "Unauthorized") {
    error = SessionDirectoryBrokerError::Unauthorized;
    return true;
  }
  if (value == "RateLimited") {
    error = SessionDirectoryBrokerError::RateLimited;
    return true;
  }
  if (value == "Timeout") {
    error = SessionDirectoryBrokerError::Timeout;
    return true;
  }
  if (value == "ServiceUnavailable") {
    error = SessionDirectoryBrokerError::ServiceUnavailable;
    return true;
  }
  if (value == "ProtocolError") {
    error = SessionDirectoryBrokerError::ProtocolError;
    return true;
  }

  return false;
}

SessionDirectoryBrokerError
MapTransportError(SessionDirectoryBrokerTransportError error) {
  switch (error) {
  case SessionDirectoryBrokerTransportError::Timeout:
    return SessionDirectoryBrokerError::Timeout;
  case SessionDirectoryBrokerTransportError::Unreachable:
    return SessionDirectoryBrokerError::ServiceUnavailable;
  case SessionDirectoryBrokerTransportError::InvalidResponse:
    return SessionDirectoryBrokerError::ProtocolError;
  case SessionDirectoryBrokerTransportError::None:
  default:
    return SessionDirectoryBrokerError::ServiceUnavailable;
  }
}

SessionDirectoryBrokerError MapStatusCode(int statusCode) {
  switch (statusCode) {
  case 400:
    return SessionDirectoryBrokerError::InvalidRequest;
  case 401:
  case 403:
    return SessionDirectoryBrokerError::Unauthorized;
  case 404:
    return SessionDirectoryBrokerError::SessionNotFound;
  case 408:
    return SessionDirectoryBrokerError::Timeout;
  case 409:
    return SessionDirectoryBrokerError::SessionAlreadyExists;
  case 412:
    return SessionDirectoryBrokerError::VersionMismatch;
  case 429:
    return SessionDirectoryBrokerError::RateLimited;
  case 503:
    return SessionDirectoryBrokerError::ServiceUnavailable;
  default:
    return statusCode >= 500 ? SessionDirectoryBrokerError::ServiceUnavailable
                             : SessionDirectoryBrokerError::ProtocolError;
  }
}

bool TryParseStrictBool(const String &value, bool &parsed) {
  if (value == "1" || value == "true" || value == "True") {
    parsed = true;
    return true;
  }
  if (value == "0" || value == "false" || value == "False") {
    parsed = false;
    return true;
  }

  return false;
}

String GenerateRequestId() {
  static const char *hexDigits = "0123456789abcdef";
  static std::atomic<uint64_t> fallbackCounter{0};
  unsigned char randomBytes[8] = {};
  if (!FillSecureRandomBytes(randomBytes, sizeof(randomBytes))) {
    return "session-directory-request-" +
           std::to_string(fallbackCounter.fetch_add(1, std::memory_order_relaxed));
  }

  String requestId = "session-directory-request-";
  for (unsigned char byte : randomBytes) {
    requestId += hexDigits[(byte >> 4) & 0x0F];
    requestId += hexDigits[byte & 0x0F];
  }

  return requestId;
}

bool TryParseUint16(const String &value, uint16_t &parsed) {
  try {
    const int asInt = std::stoi(value);
    if (asInt <= 0 || asInt > 65535) {
      return false;
    }
    parsed = static_cast<uint16_t>(asInt);
    return true;
  } catch (...) {
    return false;
  }
}

bool TryParseUint64(const String &value, uint64_t &parsed) {
  try {
    parsed = std::stoull(value);
    return true;
  } catch (...) {
    return false;
  }
}

void AppendLine(String &body, const String &key, const String &value) {
  body += key;
  body += "=";
  body += EncodeWireValue(value);
  body += "\n";
}

bool ParseBody(const String &body, KeyValueMap &values) {
  values.clear();
  std::istringstream stream(body);
  String line;
  while (std::getline(stream, line)) {
    if (line.empty()) {
      continue;
    }

    const size_t separator = line.find('=');
    if (separator == String::npos) {
      return false;
    }

    const String key = line.substr(0, separator);
    if (key.empty() || values.find(key) != values.end()) {
      return false;
    }

    String decodedValue;
    if (!DecodeWireValue(line.substr(separator + 1), decodedValue)) {
      return false;
    }

    values[key] = decodedValue;
  }

  return true;
}

template <typename T> const T *FindValue(const KeyValueMap &values,
                                         const char *key) {
  const auto it = values.find(key);
  return it == values.end() ? nullptr : &it->second;
}

bool TryParseSuccess(const KeyValueMap &values, bool &success) {
  const String *successValue = FindValue<String>(values, "success");
  if (successValue == nullptr) {
    return false;
  }

  if (*successValue == "1" || *successValue == "true" ||
      *successValue == "True") {
    success = true;
    return true;
  }
  if (*successValue == "0" || *successValue == "false" ||
      *successValue == "False") {
    success = false;
    return true;
  }

  return false;
}

String ResolveDetail(const String &preferred, const String &fallback) {
  return SessionCore::SanitizeDiagnosticDetail(preferred.empty() ? fallback
                                                                 : preferred);
}

String ResolveDetail(const String &preferred, const String &fallback,
                     const std::vector<String> &secrets) {
  return SessionCore::SanitizeDiagnosticDetail(
      preferred.empty() ? fallback : preferred, secrets);
}

std::vector<String> CollectBrokerRequestSecrets(
    const SessionDirectoryBrokerRegisterRequest &request) {
  std::vector<String> secrets;
  if (!request.requestedJoinCredential.empty()) {
    secrets.push_back(request.requestedJoinCredential);
  }
  return secrets;
}

std::vector<String> CollectBrokerRequestSecrets(
    const SessionDirectoryBrokerLookupRequest &) {
  return {};
}

std::vector<String> CollectBrokerRequestSecrets(
    const SessionDirectoryBrokerRefreshRequest &request) {
  if (request.registrationHandle.empty()) {
    return {};
  }
  return {request.registrationHandle};
}

std::vector<String> CollectBrokerRequestSecrets(
    const SessionDirectoryBrokerUnregisterRequest &request) {
  if (request.registrationHandle.empty()) {
    return {};
  }
  return {request.registrationHandle};
}

SessionDirectoryBrokerRegisterResponse
MakeRegisterFailure(SessionDirectoryBrokerError errorCode,
                    const String &detail) {
  SessionDirectoryBrokerRegisterResponse response;
  response.errorCode = errorCode;
  response.detailMessage = detail;
  return response;
}

SessionDirectoryBrokerLookupResponse
MakeLookupFailure(SessionDirectoryBrokerError errorCode, const String &detail) {
  SessionDirectoryBrokerLookupResponse response;
  response.errorCode = errorCode;
  response.detailMessage = detail;
  return response;
}

String SerializeRegisterRequest(
    const SessionDirectoryBrokerRegisterRequest &request) {
  String body;
  AppendLine(body, "protocolVersion", BrokerWireProtocolVersion);
  AppendLine(body, "hostingMode", HostingModeToWire(request.hostingMode));
  AppendLine(body, "requestedSessionId", request.requestedSessionId);
  AppendLine(body, "requestedJoinCredential", request.requestedJoinCredential);
  AppendLine(body, "bindHost", request.bindEndpoint.host);
  AppendLine(body, "bindPort", std::to_string(request.bindEndpoint.port));
  AppendLine(body, "advertisedHost", request.advertisedEndpoint.host);
  AppendLine(body, "advertisedPort",
             std::to_string(request.advertisedEndpoint.port));
  AppendLine(body, "buildCompatibilityId", request.buildCompatibilityId);
  AppendLine(body, "requireJoinCredential",
             BoolToWire(request.requireJoinCredential));
  return body;
}

String SerializeLookupRequest(const SessionDirectoryBrokerLookupRequest &request) {
  String body;
  AppendLine(body, "protocolVersion", BrokerWireProtocolVersion);
  AppendLine(body, "sessionId", request.sessionId);
  AppendLine(body, "buildCompatibilityId", request.buildCompatibilityId);
  return body;
}

String SerializeRefreshRequest(
    const SessionDirectoryBrokerRefreshRequest &request) {
  String body;
  AppendLine(body, "protocolVersion", BrokerWireProtocolVersion);
  AppendLine(body, "registrationHandle", request.registrationHandle);
  return body;
}

String SerializeUnregisterRequest(
    const SessionDirectoryBrokerUnregisterRequest &request) {
  String body;
  AppendLine(body, "protocolVersion", BrokerWireProtocolVersion);
  AppendLine(body, "registrationHandle", request.registrationHandle);
  return body;
}

bool PopulateSessionDescriptor(const KeyValueMap &values,
                               SessionDescriptor &session,
                               NetworkEndpoint &resolvedJoinRoute,
                               ResolvedRouteKind &resolvedRouteKind,
                               uint64_t &resolvedRouteExpiresAtMs,
                               String &joinCredential,
                               String &providerName,
                               uint64_t &joinCredentialExpiresAtMs) {
  const auto sessionIdIt = values.find("sessionId");
  const auto resolvedHostIt = values.find("resolvedHost");
  const auto resolvedPortIt = values.find("resolvedPort");
  if (sessionIdIt == values.end() || resolvedHostIt == values.end() ||
      resolvedPortIt == values.end()) {
    return false;
  }
  if (sessionIdIt->second.empty() || resolvedHostIt->second.empty()) {
    return false;
  }

  uint16_t resolvedPort = 0;
  if (!TryParseUint16(resolvedPortIt->second, resolvedPort)) {
    return false;
  }

  session.sessionId = sessionIdIt->second;
  if (const String *hostingModeValue = FindValue<String>(values, "hostingMode")) {
    if (!TryParseHostingMode(*hostingModeValue, session.hostingMode)) {
      return false;
    }
  } else {
    session.hostingMode = HostingMode::None;
  }
  if (const String *joinMethodValue = FindValue<String>(values, "joinMethod")) {
    if (!TryParseJoinMethod(*joinMethodValue, session.joinMethod)) {
      return false;
    }
  } else {
    session.joinMethod = JoinMethod::SessionDirectory;
  }
  session.buildCompatibilityId =
      values.count("buildCompatibilityId") ? values.at("buildCompatibilityId")
                                           : String{};
  bool relayRequired = false;
  if (const String *relayRequiredValue =
          FindValue<String>(values, "relayRequired")) {
    if (!TryParseStrictBool(*relayRequiredValue, relayRequired)) {
      return false;
    }
  }
  session.relayRequired = relayRequired;

  bool requireJoinCredential = false;
  if (const String *requireJoinCredentialValue =
          FindValue<String>(values, "requireJoinCredential")) {
    if (!TryParseStrictBool(*requireJoinCredentialValue,
                            requireJoinCredential)) {
      return false;
    }
  }
  session.isJoinCredentialRequired = requireJoinCredential;

  if (values.count("bindHost")) {
    session.bindEndpoint.host = values.at("bindHost");
  }
  if (values.count("bindPort")) {
    uint16_t bindPort = 0;
    if (!TryParseUint16(values.at("bindPort"), bindPort)) {
      return false;
    }
    session.bindEndpoint.port = bindPort;
    session.bindEndpoint.usage = EndpointUsage::Bind;
  }

  if (values.count("advertisedHost")) {
    session.advertisedEndpoint.host = values.at("advertisedHost");
  }
  if (values.count("advertisedPort")) {
    uint16_t advertisedPort = 0;
    if (!TryParseUint16(values.at("advertisedPort"), advertisedPort)) {
      return false;
    }
    session.advertisedEndpoint.port = advertisedPort;
    session.advertisedEndpoint.usage = EndpointUsage::Advertised;
  }

  resolvedJoinRoute.host = resolvedHostIt->second;
  resolvedJoinRoute.port = resolvedPort;
  resolvedJoinRoute.usage = EndpointUsage::ResolvedTransport;
  session.resolvedEndpoint = resolvedJoinRoute;

  if (const String *routeKindValue = FindValue<String>(values, "resolvedRouteKind")) {
    if (!TryParseResolvedRouteKind(*routeKindValue, resolvedRouteKind)) {
      return false;
    }
  } else {
    resolvedRouteKind = ResolvedRouteKind::Direct;
  }
  if (resolvedRouteKind != ResolvedRouteKind::Direct) {
    return false;
  }
  if (values.count("resolvedRouteExpiresAtMs")) {
    if (!TryParseUint64(values.at("resolvedRouteExpiresAtMs"),
                        resolvedRouteExpiresAtMs)) {
      return false;
    }
  } else {
    resolvedRouteExpiresAtMs = 0;
  }
  session.resolvedRouteKind = resolvedRouteKind;
  session.resolvedRouteExpiresAtMs = resolvedRouteExpiresAtMs;

  joinCredential =
      values.count("joinCredential") ? values.at("joinCredential") : String{};
  providerName = values.count("providerName") ? values.at("providerName")
                                              : String{"RemoteSessionDirectory"};
  const bool requiresCredentialMetadata =
      !joinCredential.empty() || session.isJoinCredentialRequired;
  if (values.count("joinCredentialExpiresAtMs")) {
    if (!TryParseUint64(values.at("joinCredentialExpiresAtMs"),
                        joinCredentialExpiresAtMs)) {
      return false;
    }
    if (requiresCredentialMetadata && joinCredentialExpiresAtMs == 0) {
      return false;
    }
  } else if (requiresCredentialMetadata) {
    return false;
  } else {
    joinCredentialExpiresAtMs = 0;
  }

  return true;
}

bool TryParseRequiredPositiveUint64Field(const KeyValueMap &values,
                                         const char *key,
                                         uint64_t &parsed) {
  const String *value = FindValue<String>(values, key);
  return value != nullptr && !value->empty() && TryParseUint64(*value, parsed) &&
         parsed != 0;
}

bool ValidateProtocolVersion(const KeyValueMap &values) {
  const String *protocolVersion = FindValue<String>(values, "protocolVersion");
  return protocolVersion != nullptr &&
         *protocolVersion == BrokerWireProtocolVersion;
}

template <typename ResponseT>
ResponseT MakeProtocolFailure(const String &detail) {
  ResponseT response;
  response.errorCode = SessionDirectoryBrokerError::ProtocolError;
  response.detailMessage = detail;
  return response;
}

template <typename ResponseT>
ResponseT MakeCorrelationFailure() {
  return MakeProtocolFailure<ResponseT>(
      "Broker response correlation identifier was missing or did not match the request.");
}

template <typename ResponseT>
ResponseT ValidateResponseCorrelation(
    const SessionDirectoryBrokerTransportResponse &transportResponse,
    const SessionDirectoryBrokerTransportRequest &transportRequest) {
  if (transportResponse.correlationId.empty() ||
      transportResponse.correlationId != transportRequest.requestId) {
    return MakeCorrelationFailure<ResponseT>();
  }

  return ResponseT{};
}

template <typename ResponseT>
ResponseT ValidateTransportSecurityState(
    const SessionDirectoryBrokerTransportSecurity &securityState) {
  if (!securityState.authenticatedBroker) {
    ResponseT response;
    response.errorCode = SessionDirectoryBrokerError::Unauthorized;
    response.detailMessage =
        "Session directory broker transport is missing authenticated broker access.";
    return response;
  }
  if (!securityState.secureChannel &&
      !securityState.allowInsecureLocalDevelopment) {
    return MakeProtocolFailure<ResponseT>(
        "Session directory broker transport is not running over a trusted secure channel.");
  }
  if (securityState.secureChannel && !securityState.hostnameValidated) {
    return MakeProtocolFailure<ResponseT>(
        "Session directory broker transport did not validate the broker hostname.");
  }
  return ResponseT{};
}

SessionDirectoryBrokerRegisterResponse ParseRegisterErrorResponse(
    const SessionDirectoryBrokerTransportResponse &transportResponse,
    SessionDirectoryBrokerError fallbackError) {
  if (transportResponse.body.empty()) {
    return MakeRegisterFailure(
        fallbackError,
        ResolveDetail(transportResponse.detailMessage,
                      "Session directory broker rejected the register request."));
  }

  KeyValueMap values;
  if (!ParseBody(transportResponse.body, values)) {
    return MakeRegisterFailure(fallbackError,
                               ResolveDetail(transportResponse.detailMessage,
                                             "Broker returned an invalid error response."));
  }
  if (!ValidateProtocolVersion(values)) {
    return MakeRegisterFailure(
        SessionDirectoryBrokerError::ProtocolError,
        "Broker error response used an incompatible session directory protocol version.");
  }

  bool success = false;
  if (!TryParseSuccess(values, success) || success) {
    return MakeRegisterFailure(fallbackError,
                               ResolveDetail(transportResponse.detailMessage,
                                             "Broker returned an invalid error response."));
  }

  SessionDirectoryBrokerError errorCode = fallbackError;
  if (const String *errorValue = FindValue<String>(values, "errorCode")) {
    if (!TryParseBrokerError(*errorValue, errorCode) ||
        errorCode == SessionDirectoryBrokerError::None) {
      errorCode = fallbackError;
    }
  }

  return MakeRegisterFailure(
      errorCode,
      ResolveDetail(values.count("detailMessage") ? values.at("detailMessage")
                                                  : String{},
                    transportResponse.detailMessage));
}

SessionDirectoryBrokerLookupResponse ParseLookupErrorResponse(
    const SessionDirectoryBrokerTransportResponse &transportResponse,
    SessionDirectoryBrokerError fallbackError) {
  if (transportResponse.body.empty()) {
    return MakeLookupFailure(
        fallbackError,
        ResolveDetail(transportResponse.detailMessage,
                      "Session directory broker rejected the lookup request."));
  }

  KeyValueMap values;
  if (!ParseBody(transportResponse.body, values)) {
    return MakeLookupFailure(fallbackError,
                             ResolveDetail(transportResponse.detailMessage,
                                           "Broker returned an invalid error response."));
  }
  if (!ValidateProtocolVersion(values)) {
    return MakeLookupFailure(
        SessionDirectoryBrokerError::ProtocolError,
        "Broker error response used an incompatible session directory protocol version.");
  }

  bool success = false;
  if (!TryParseSuccess(values, success) || success) {
    return MakeLookupFailure(fallbackError,
                             ResolveDetail(transportResponse.detailMessage,
                                           "Broker returned an invalid error response."));
  }

  SessionDirectoryBrokerError errorCode = fallbackError;
  if (const String *errorValue = FindValue<String>(values, "errorCode")) {
    if (!TryParseBrokerError(*errorValue, errorCode) ||
        errorCode == SessionDirectoryBrokerError::None) {
      errorCode = fallbackError;
    }
  }

  return MakeLookupFailure(
      errorCode,
      ResolveDetail(values.count("detailMessage") ? values.at("detailMessage")
                                                  : String{},
                    transportResponse.detailMessage));
}

SessionDirectoryBrokerRefreshResponse ParseRefreshErrorResponse(
    const SessionDirectoryBrokerTransportResponse &transportResponse,
    SessionDirectoryBrokerError fallbackError) {
  if (transportResponse.body.empty()) {
    SessionDirectoryBrokerRefreshResponse response;
    response.errorCode = fallbackError;
    response.detailMessage = ResolveDetail(
        transportResponse.detailMessage,
        "Session directory broker rejected the refresh request.");
    return response;
  }

  KeyValueMap values;
  if (!ParseBody(transportResponse.body, values)) {
    SessionDirectoryBrokerRefreshResponse response;
    response.errorCode = fallbackError;
    response.detailMessage = ResolveDetail(
        transportResponse.detailMessage,
        "Broker returned an invalid refresh error response.");
    return response;
  }
  if (!ValidateProtocolVersion(values)) {
    SessionDirectoryBrokerRefreshResponse response;
    response.errorCode = SessionDirectoryBrokerError::ProtocolError;
    response.detailMessage = "Broker error response used an incompatible "
                             "session directory protocol version.";
    return response;
  }

  bool success = false;
  if (!TryParseSuccess(values, success) || success) {
    SessionDirectoryBrokerRefreshResponse response;
    response.errorCode = SessionDirectoryBrokerError::ProtocolError;
    response.detailMessage = ResolveDetail(
        transportResponse.detailMessage,
        "Broker returned an invalid refresh error response.");
    return response;
  }

  SessionDirectoryBrokerRefreshResponse response;
  response.errorCode = fallbackError;
  if (const String *errorValue = FindValue<String>(values, "errorCode")) {
    SessionDirectoryBrokerError parsedError = SessionDirectoryBrokerError::None;
    if (TryParseBrokerError(*errorValue, parsedError) &&
        parsedError != SessionDirectoryBrokerError::None) {
      response.errorCode = parsedError;
    }
  }
  response.detailMessage = ResolveDetail(
      values.count("detailMessage") ? values.at("detailMessage") : String{},
      transportResponse.detailMessage);
  return response;
}

SessionDirectoryBrokerUnregisterResponse ParseUnregisterErrorResponse(
    const SessionDirectoryBrokerTransportResponse &transportResponse,
    SessionDirectoryBrokerError fallbackError) {
  if (transportResponse.body.empty()) {
    SessionDirectoryBrokerUnregisterResponse response;
    response.errorCode = fallbackError;
    response.detailMessage = ResolveDetail(
        transportResponse.detailMessage,
        "Session directory broker rejected the unregister request.");
    return response;
  }

  KeyValueMap values;
  if (!ParseBody(transportResponse.body, values)) {
    SessionDirectoryBrokerUnregisterResponse response;
    response.errorCode = fallbackError;
    response.detailMessage = ResolveDetail(
        transportResponse.detailMessage,
        "Broker returned an invalid unregister error response.");
    return response;
  }
  if (!ValidateProtocolVersion(values)) {
    SessionDirectoryBrokerUnregisterResponse response;
    response.errorCode = SessionDirectoryBrokerError::ProtocolError;
    response.detailMessage = "Broker error response used an incompatible "
                             "session directory protocol version.";
    return response;
  }

  bool success = false;
  if (!TryParseSuccess(values, success) || success) {
    SessionDirectoryBrokerUnregisterResponse response;
    response.errorCode = SessionDirectoryBrokerError::ProtocolError;
    response.detailMessage = ResolveDetail(
        transportResponse.detailMessage,
        "Broker returned an invalid unregister error response.");
    return response;
  }

  SessionDirectoryBrokerUnregisterResponse response;
  response.errorCode = fallbackError;
  if (const String *errorValue = FindValue<String>(values, "errorCode")) {
    SessionDirectoryBrokerError parsedError = SessionDirectoryBrokerError::None;
    if (TryParseBrokerError(*errorValue, parsedError) &&
        parsedError != SessionDirectoryBrokerError::None) {
      response.errorCode = parsedError;
    }
  }
  response.detailMessage = ResolveDetail(
      values.count("detailMessage") ? values.at("detailMessage") : String{},
      transportResponse.detailMessage);
  return response;
}

SessionDirectoryBrokerRegisterResponse ParseRegisterResponse(
    const SessionDirectoryBrokerTransportResponse &transportResponse) {
  KeyValueMap values;
  if (!ParseBody(transportResponse.body, values)) {
    return MakeProtocolFailure<SessionDirectoryBrokerRegisterResponse>(
        "Broker register response could not be parsed.");
  }
  if (!ValidateProtocolVersion(values)) {
    return MakeProtocolFailure<SessionDirectoryBrokerRegisterResponse>(
        "Broker register response used an incompatible session directory protocol version.");
  }

  bool success = false;
  if (!TryParseSuccess(values, success)) {
    return MakeProtocolFailure<SessionDirectoryBrokerRegisterResponse>(
        "Broker register response did not declare an explicit success state.");
  }
  if (!success) {
    SessionDirectoryBrokerError errorCode = SessionDirectoryBrokerError::ProtocolError;
    if (const String *errorValue = FindValue<String>(values, "errorCode")) {
      if (!TryParseBrokerError(*errorValue, errorCode) ||
          errorCode == SessionDirectoryBrokerError::None) {
        errorCode = SessionDirectoryBrokerError::ProtocolError;
      }
    }
    return MakeRegisterFailure(
        errorCode,
        values.count("detailMessage") ? values.at("detailMessage") : String{});
  }

  SessionDirectoryBrokerRegisterResponse response;
  response.success = true;
  response.registrationHandle =
      values.count("registrationHandle") ? values.at("registrationHandle")
                                         : String{};
  if (response.registrationHandle.empty()) {
    return MakeRegisterFailure(SessionDirectoryBrokerError::ProtocolError,
                               "Broker register response was missing the "
                               "registration handle.");
  }
  if (!TryParseRequiredPositiveUint64Field(values, "registrationExpiresAtMs",
                                           response.registrationExpiresAtMs)) {
    return MakeRegisterFailure(
        SessionDirectoryBrokerError::ProtocolError,
        "Broker register response was missing the registration expiry.");
  }
  if (!PopulateSessionDescriptor(values, response.session,
                                 response.resolvedJoinRoute,
                                 response.resolvedRouteKind,
                                 response.resolvedRouteExpiresAtMs,
                                 response.joinCredential, response.providerName,
                                 response.joinCredentialExpiresAtMs)) {
    return MakeRegisterFailure(SessionDirectoryBrokerError::ProtocolError,
                               "Broker register response was missing required "
                               "session directory fields.");
  }

  return response;
}

SessionDirectoryBrokerLookupResponse ParseLookupResponse(
    const SessionDirectoryBrokerTransportResponse &transportResponse) {
  KeyValueMap values;
  if (!ParseBody(transportResponse.body, values)) {
    return MakeProtocolFailure<SessionDirectoryBrokerLookupResponse>(
        "Broker lookup response could not be parsed.");
  }
  if (!ValidateProtocolVersion(values)) {
    return MakeProtocolFailure<SessionDirectoryBrokerLookupResponse>(
        "Broker lookup response used an incompatible session directory protocol version.");
  }

  bool success = false;
  if (!TryParseSuccess(values, success)) {
    return MakeProtocolFailure<SessionDirectoryBrokerLookupResponse>(
        "Broker lookup response did not declare an explicit success state.");
  }
  if (!success) {
    SessionDirectoryBrokerError errorCode = SessionDirectoryBrokerError::ProtocolError;
    if (const String *errorValue = FindValue<String>(values, "errorCode")) {
      if (!TryParseBrokerError(*errorValue, errorCode) ||
          errorCode == SessionDirectoryBrokerError::None) {
        errorCode = SessionDirectoryBrokerError::ProtocolError;
      }
    }
    return MakeLookupFailure(
        errorCode,
        values.count("detailMessage") ? values.at("detailMessage") : String{});
  }

  SessionDirectoryBrokerLookupResponse response;
  response.success = true;
  if (!PopulateSessionDescriptor(values, response.session,
                                 response.resolvedJoinRoute,
                                 response.resolvedRouteKind,
                                 response.resolvedRouteExpiresAtMs,
                                 response.joinCredential, response.providerName,
                                 response.joinCredentialExpiresAtMs)) {
    return MakeLookupFailure(SessionDirectoryBrokerError::ProtocolError,
                             "Broker lookup response was missing required "
                             "session directory fields.");
  }

  return response;
}

SessionDirectoryBrokerRefreshResponse ParseRefreshResponse(
    const SessionDirectoryBrokerTransportResponse &transportResponse) {
  KeyValueMap values;
  if (!ParseBody(transportResponse.body, values) ||
      !ValidateProtocolVersion(values)) {
    SessionDirectoryBrokerRefreshResponse response;
    response.errorCode = SessionDirectoryBrokerError::ProtocolError;
    response.detailMessage =
        "Broker refresh response could not be parsed.";
    return response;
  }

  bool success = false;
  if (!TryParseSuccess(values, success)) {
    SessionDirectoryBrokerRefreshResponse response;
    response.errorCode = SessionDirectoryBrokerError::ProtocolError;
    response.detailMessage =
        "Broker refresh response did not declare an explicit success state.";
    return response;
  }

  if (!success) {
    SessionDirectoryBrokerRefreshResponse response;
    response.errorCode = SessionDirectoryBrokerError::ProtocolError;
    if (const String *errorValue = FindValue<String>(values, "errorCode")) {
      SessionDirectoryBrokerError parsedError = SessionDirectoryBrokerError::None;
      if (TryParseBrokerError(*errorValue, parsedError) &&
          parsedError != SessionDirectoryBrokerError::None) {
        response.errorCode = parsedError;
      }
    }
    response.detailMessage =
        values.count("detailMessage") ? values.at("detailMessage") : String{};
    return response;
  }

  SessionDirectoryBrokerRefreshResponse response;
  response.success = true;
  if (!TryParseRequiredPositiveUint64Field(values, "registrationExpiresAtMs",
                                           response.registrationExpiresAtMs)) {
    response.success = false;
    response.errorCode = SessionDirectoryBrokerError::ProtocolError;
    response.detailMessage =
        "Broker refresh response was missing the registration expiry.";
  }
  return response;
}

SessionDirectoryBrokerUnregisterResponse ParseUnregisterResponse(
    const SessionDirectoryBrokerTransportResponse &transportResponse) {
  KeyValueMap values;
  if (!ParseBody(transportResponse.body, values) ||
      !ValidateProtocolVersion(values)) {
    SessionDirectoryBrokerUnregisterResponse response;
    response.errorCode = SessionDirectoryBrokerError::ProtocolError;
    response.detailMessage =
        "Broker unregister response could not be parsed.";
    return response;
  }

  bool success = false;
  if (!TryParseSuccess(values, success)) {
    SessionDirectoryBrokerUnregisterResponse response;
    response.errorCode = SessionDirectoryBrokerError::ProtocolError;
    response.detailMessage =
        "Broker unregister response did not declare an explicit success state.";
    return response;
  }

  if (!success) {
    SessionDirectoryBrokerUnregisterResponse response;
    response.errorCode = SessionDirectoryBrokerError::ProtocolError;
    if (const String *errorValue = FindValue<String>(values, "errorCode")) {
      SessionDirectoryBrokerError parsedError = SessionDirectoryBrokerError::None;
      if (TryParseBrokerError(*errorValue, parsedError) &&
          parsedError != SessionDirectoryBrokerError::None) {
        response.errorCode = parsedError;
      }
    }
    response.detailMessage =
        values.count("detailMessage") ? values.at("detailMessage") : String{};
    return response;
  }

  SessionDirectoryBrokerUnregisterResponse response;
  response.success = true;
  return response;
}
} // namespace

SessionDirectoryRemoteBrokerClient::SessionDirectoryRemoteBrokerClient(
    SessionDirectoryBrokerTransportPtr transport)
    : m_transport(std::move(transport)) {}

SessionDirectoryBrokerRegisterResponse
SessionDirectoryRemoteBrokerClient::RegisterSession(
    const SessionDirectoryBrokerRegisterRequest &request) {
  if (!m_transport) {
    return MakeRegisterFailure(SessionDirectoryBrokerError::ServiceUnavailable,
                               "No session directory broker transport is configured.");
  }

  const SessionDirectoryBrokerTransportSecurity securityState =
      m_transport->GetSecurityState();
  const SessionDirectoryBrokerRegisterResponse securityFailure =
      ValidateTransportSecurityState<SessionDirectoryBrokerRegisterResponse>(
          securityState);
  if (!securityFailure.detailMessage.empty()) {
    return securityFailure;
  }

  const std::vector<String> secrets = CollectBrokerRequestSecrets(request);
  SessionDirectoryBrokerTransportRequest transportRequest;
  transportRequest.method = "POST";
  transportRequest.path = "/v1/session-directory/register";
  transportRequest.body = SerializeRegisterRequest(request);
  transportRequest.requestId = EncodeWireValue(GenerateRequestId());
  transportRequest.headers["X-ToolKit-Session-Directory-Version"] =
      BrokerWireProtocolVersion;

  const SessionDirectoryBrokerTransportResponse transportResponse =
      m_transport->Send(transportRequest);
  if (!transportResponse.success) {
    return MakeRegisterFailure(
        MapTransportError(transportResponse.transportError),
        ResolveDetail(transportResponse.detailMessage,
                      "Session directory broker transport request failed.",
                      secrets));
  }
  const SessionDirectoryBrokerRegisterResponse correlationFailure =
      ValidateResponseCorrelation<SessionDirectoryBrokerRegisterResponse>(
          transportResponse, transportRequest);
  if (!correlationFailure.detailMessage.empty()) {
    return correlationFailure;
  }

  if (transportResponse.statusCode < 200 || transportResponse.statusCode >= 300) {
    return ParseRegisterErrorResponse(transportResponse,
                                      MapStatusCode(transportResponse.statusCode));
  }

  return ParseRegisterResponse(transportResponse);
}

SessionDirectoryBrokerLookupResponse
SessionDirectoryRemoteBrokerClient::LookupSession(
    const SessionDirectoryBrokerLookupRequest &request) const {
  if (!m_transport) {
    return MakeLookupFailure(SessionDirectoryBrokerError::ServiceUnavailable,
                             "No session directory broker transport is configured.");
  }

  const SessionDirectoryBrokerTransportSecurity securityState =
      m_transport->GetSecurityState();
  const SessionDirectoryBrokerLookupResponse securityFailure =
      ValidateTransportSecurityState<SessionDirectoryBrokerLookupResponse>(
          securityState);
  if (!securityFailure.detailMessage.empty()) {
    return securityFailure;
  }

  const std::vector<String> secrets = CollectBrokerRequestSecrets(request);
  SessionDirectoryBrokerTransportRequest transportRequest;
  transportRequest.method = "POST";
  transportRequest.path = "/v1/session-directory/lookup";
  transportRequest.body = SerializeLookupRequest(request);
  transportRequest.requestId = EncodeWireValue(GenerateRequestId());
  transportRequest.headers["X-ToolKit-Session-Directory-Version"] =
      BrokerWireProtocolVersion;

  const SessionDirectoryBrokerTransportResponse transportResponse =
      m_transport->Send(transportRequest);
  if (!transportResponse.success) {
    return MakeLookupFailure(
        MapTransportError(transportResponse.transportError),
        ResolveDetail(transportResponse.detailMessage,
                      "Session directory broker transport request failed.",
                      secrets));
  }
  const SessionDirectoryBrokerLookupResponse correlationFailure =
      ValidateResponseCorrelation<SessionDirectoryBrokerLookupResponse>(
          transportResponse, transportRequest);
  if (!correlationFailure.detailMessage.empty()) {
    return correlationFailure;
  }

  if (transportResponse.statusCode < 200 || transportResponse.statusCode >= 300) {
    return ParseLookupErrorResponse(transportResponse,
                                    MapStatusCode(transportResponse.statusCode));
  }

  return ParseLookupResponse(transportResponse);
}

SessionDirectoryBrokerRefreshResponse
SessionDirectoryRemoteBrokerClient::RefreshSessionRegistration(
    const SessionDirectoryBrokerRefreshRequest &request) {
  if (!m_transport) {
    SessionDirectoryBrokerRefreshResponse response;
    response.errorCode = SessionDirectoryBrokerError::ServiceUnavailable;
    response.detailMessage =
        "No session directory broker transport is configured.";
    return response;
  }

  const SessionDirectoryBrokerTransportSecurity securityState =
      m_transport->GetSecurityState();
  const SessionDirectoryBrokerRefreshResponse securityFailure =
      ValidateTransportSecurityState<SessionDirectoryBrokerRefreshResponse>(
          securityState);
  if (!securityFailure.detailMessage.empty()) {
    return securityFailure;
  }

  const std::vector<String> secrets = CollectBrokerRequestSecrets(request);
  SessionDirectoryBrokerTransportRequest transportRequest;
  transportRequest.method = "POST";
  transportRequest.path = "/v1/session-directory/refresh";
  transportRequest.body = SerializeRefreshRequest(request);
  transportRequest.requestId = EncodeWireValue(GenerateRequestId());
  transportRequest.headers["X-ToolKit-Session-Directory-Version"] =
      BrokerWireProtocolVersion;

  const SessionDirectoryBrokerTransportResponse transportResponse =
      m_transport->Send(transportRequest);
  if (!transportResponse.success) {
    SessionDirectoryBrokerRefreshResponse response;
    response.errorCode = MapTransportError(transportResponse.transportError);
    response.detailMessage = ResolveDetail(
        transportResponse.detailMessage,
        "Session directory broker transport request failed.", secrets);
    return response;
  }
  const SessionDirectoryBrokerRefreshResponse correlationFailure =
      ValidateResponseCorrelation<SessionDirectoryBrokerRefreshResponse>(
          transportResponse, transportRequest);
  if (!correlationFailure.detailMessage.empty()) {
    return correlationFailure;
  }

  if (transportResponse.statusCode < 200 || transportResponse.statusCode >= 300) {
    return ParseRefreshErrorResponse(transportResponse,
                                     MapStatusCode(transportResponse.statusCode));
  }

  return ParseRefreshResponse(transportResponse);
}

SessionDirectoryBrokerUnregisterResponse
SessionDirectoryRemoteBrokerClient::UnregisterSession(
    const SessionDirectoryBrokerUnregisterRequest &request) {
  if (!m_transport) {
    SessionDirectoryBrokerUnregisterResponse response;
    response.errorCode = SessionDirectoryBrokerError::ServiceUnavailable;
    response.detailMessage =
        "No session directory broker transport is configured.";
    return response;
  }

  const SessionDirectoryBrokerTransportSecurity securityState =
      m_transport->GetSecurityState();
  const SessionDirectoryBrokerUnregisterResponse securityFailure =
      ValidateTransportSecurityState<SessionDirectoryBrokerUnregisterResponse>(
          securityState);
  if (!securityFailure.detailMessage.empty()) {
    return securityFailure;
  }

  const std::vector<String> secrets = CollectBrokerRequestSecrets(request);
  SessionDirectoryBrokerTransportRequest transportRequest;
  transportRequest.method = "POST";
  transportRequest.path = "/v1/session-directory/unregister";
  transportRequest.body = SerializeUnregisterRequest(request);
  transportRequest.requestId = EncodeWireValue(GenerateRequestId());
  transportRequest.headers["X-ToolKit-Session-Directory-Version"] =
      BrokerWireProtocolVersion;

  const SessionDirectoryBrokerTransportResponse transportResponse =
      m_transport->Send(transportRequest);
  if (!transportResponse.success) {
    SessionDirectoryBrokerUnregisterResponse response;
    response.errorCode = MapTransportError(transportResponse.transportError);
    response.detailMessage = ResolveDetail(
        transportResponse.detailMessage,
        "Session directory broker transport request failed.", secrets);
    return response;
  }
  const SessionDirectoryBrokerUnregisterResponse correlationFailure =
      ValidateResponseCorrelation<SessionDirectoryBrokerUnregisterResponse>(
          transportResponse, transportRequest);
  if (!correlationFailure.detailMessage.empty()) {
    return correlationFailure;
  }

  if (transportResponse.statusCode < 200 || transportResponse.statusCode >= 300) {
    return ParseUnregisterErrorResponse(
        transportResponse, MapStatusCode(transportResponse.statusCode));
  }

  return ParseUnregisterResponse(transportResponse);
}

SessionDirectoryBrokerClientPtr CreateRemoteSessionDirectoryBrokerClient(
    SessionDirectoryBrokerTransportPtr transport) {
  return std::make_shared<SessionDirectoryRemoteBrokerClient>(
      std::move(transport));
}
} // namespace ToolKit::ToolKitNetworking
