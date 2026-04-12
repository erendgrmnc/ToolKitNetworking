#include "SessionDirectoryWinHttpTransport.h"

#ifdef _WIN32
#include <Windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

#include <vector>

namespace ToolKit::ToolKitNetworking {
namespace {
#ifdef _WIN32
constexpr const wchar_t *BrokerUserAgent =
    L"ToolKitNetworkingSessionDirectory/1";
constexpr const wchar_t *RequestIdHeaderName =
    L"X-ToolKit-Session-Directory-Request-Id";

String Narrow(const std::wstring &value) {
  if (value.empty()) {
    return {};
  }

  const int required = WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
                                           static_cast<int>(value.size()),
                                           nullptr, 0, nullptr, nullptr);
  if (required <= 0) {
    return {};
  }

  String narrow(static_cast<size_t>(required), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
                      static_cast<int>(value.size()), narrow.data(), required,
                      nullptr, nullptr);
  return narrow;
}

std::wstring Widen(const String &value) {
  if (value.empty()) {
    return {};
  }

  const int required = MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
                                           static_cast<int>(value.size()),
                                           nullptr, 0);
  if (required <= 0) {
    return {};
  }

  std::wstring wide(static_cast<size_t>(required), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
                      static_cast<int>(value.size()), wide.data(), required);
  return wide;
}

String WinHttpErrorDetail(DWORD error) {
  switch (error) {
  case ERROR_WINHTTP_TIMEOUT:
    return "Session directory broker request timed out.";
  case ERROR_WINHTTP_NAME_NOT_RESOLVED:
  case ERROR_WINHTTP_CANNOT_CONNECT:
  case ERROR_WINHTTP_CONNECTION_ERROR:
    return "Session directory broker could not be reached.";
  case ERROR_WINHTTP_SECURE_FAILURE:
  case ERROR_WINHTTP_SECURE_CERT_CN_INVALID:
  case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID:
  case ERROR_WINHTTP_SECURE_CERT_REVOKED:
  case ERROR_WINHTTP_SECURE_INVALID_CA:
  case ERROR_WINHTTP_SECURE_CHANNEL_ERROR:
    return "Session directory broker TLS validation failed.";
  default:
    return "Session directory broker request failed.";
  }
}

SessionDirectoryBrokerTransportError MapWinHttpError(DWORD error) {
  switch (error) {
  case ERROR_WINHTTP_TIMEOUT:
    return SessionDirectoryBrokerTransportError::Timeout;
  case ERROR_WINHTTP_NAME_NOT_RESOLVED:
  case ERROR_WINHTTP_CANNOT_CONNECT:
  case ERROR_WINHTTP_CONNECTION_ERROR:
  case ERROR_WINHTTP_SECURE_FAILURE:
  case ERROR_WINHTTP_SECURE_CERT_CN_INVALID:
  case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID:
  case ERROR_WINHTTP_SECURE_CERT_REVOKED:
  case ERROR_WINHTTP_SECURE_INVALID_CA:
  case ERROR_WINHTTP_SECURE_CHANNEL_ERROR:
    return SessionDirectoryBrokerTransportError::Unreachable;
  default:
    return SessionDirectoryBrokerTransportError::InvalidResponse;
  }
}

std::wstring JoinPaths(const std::wstring &basePath,
                       const std::wstring &requestPath) {
  if (basePath.empty() || basePath == L"/") {
    return requestPath.empty() ? std::wstring{L"/"} : requestPath;
  }
  if (requestPath.empty() || requestPath == L"/") {
    return basePath;
  }

  const bool baseEndsWithSlash = basePath.back() == L'/';
  const bool requestStartsWithSlash = requestPath.front() == L'/';
  if (baseEndsWithSlash && requestStartsWithSlash) {
    return basePath + requestPath.substr(1);
  }
  if (!baseEndsWithSlash && !requestStartsWithSlash) {
    return basePath + L"/" + requestPath;
  }
  return basePath + requestPath;
}

class WinHttpSessionHandle {
public:
  explicit WinHttpSessionHandle(HINTERNET handle = nullptr) : m_handle(handle) {}
  ~WinHttpSessionHandle() {
    if (m_handle) {
      WinHttpCloseHandle(m_handle);
    }
  }

  WinHttpSessionHandle(const WinHttpSessionHandle &) = delete;
  WinHttpSessionHandle &operator=(const WinHttpSessionHandle &) = delete;

  WinHttpSessionHandle(WinHttpSessionHandle &&other) noexcept
      : m_handle(other.m_handle) {
    other.m_handle = nullptr;
  }

  WinHttpSessionHandle &operator=(WinHttpSessionHandle &&other) noexcept {
    if (this != &other) {
      if (m_handle) {
        WinHttpCloseHandle(m_handle);
      }
      m_handle = other.m_handle;
      other.m_handle = nullptr;
    }
    return *this;
  }

  HINTERNET get() const { return m_handle; }

private:
  HINTERNET m_handle = nullptr;
};

class WinHttpSessionDirectoryBrokerTransport
    : public ISessionDirectoryBrokerTransport {
public:
  WinHttpSessionDirectoryBrokerTransport(std::wstring host, INTERNET_PORT port,
                                         std::wstring basePath, bool secure,
                                         int timeoutMs, String authToken,
                                         bool allowInsecureLocalDevelopment)
      : m_host(std::move(host)), m_port(port), m_basePath(std::move(basePath)),
        m_secure(secure), m_timeoutMs(timeoutMs), m_authToken(std::move(authToken)),
        m_allowInsecureLocalDevelopment(allowInsecureLocalDevelopment) {}

  SessionDirectoryBrokerTransportSecurity GetSecurityState() const override {
    SessionDirectoryBrokerTransportSecurity state;
    state.secureChannel = m_secure;
    state.hostnameValidated = m_secure;
    state.authenticatedBroker = !m_authToken.empty();
    state.allowInsecureLocalDevelopment = m_allowInsecureLocalDevelopment;
    return state;
  }

  SessionDirectoryBrokerTransportResponse Send(
      const SessionDirectoryBrokerTransportRequest &request) const override {
    SessionDirectoryBrokerTransportResponse response;

    WinHttpSessionHandle session(
        WinHttpOpen(BrokerUserAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.get()) {
      const DWORD error = GetLastError();
      response.transportError = MapWinHttpError(error);
      response.detailMessage = WinHttpErrorDetail(error);
      return response;
    }

    WinHttpSetTimeouts(session.get(), m_timeoutMs, m_timeoutMs, m_timeoutMs,
                       m_timeoutMs);

    WinHttpSessionHandle connection(
        WinHttpConnect(session.get(), m_host.c_str(), m_port, 0));
    if (!connection.get()) {
      const DWORD error = GetLastError();
      response.transportError = MapWinHttpError(error);
      response.detailMessage = WinHttpErrorDetail(error);
      return response;
    }

    const std::wstring method =
        Widen(request.method.empty() ? String{"POST"} : request.method);
    const std::wstring path = JoinPaths(m_basePath, Widen(request.path));
    const DWORD flags = m_secure ? WINHTTP_FLAG_SECURE : 0;
    WinHttpSessionHandle requestHandle(WinHttpOpenRequest(
        connection.get(), method.c_str(), path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!requestHandle.get()) {
      const DWORD error = GetLastError();
      response.transportError = MapWinHttpError(error);
      response.detailMessage = WinHttpErrorDetail(error);
      return response;
    }

    std::wstring headers;
    headers += L"Content-Type: ";
    headers += Widen(request.contentType);
    headers += L"\r\n";
    headers += RequestIdHeaderName;
    headers += L": ";
    headers += Widen(request.requestId);
    headers += L"\r\n";
    for (const auto &entry : request.headers) {
      headers += Widen(entry.first);
      headers += L": ";
      headers += Widen(entry.second);
      headers += L"\r\n";
    }
    if (!m_authToken.empty()) {
      headers += L"Authorization: Bearer ";
      headers += Widen(m_authToken);
      headers += L"\r\n";
    }

    const String requestBody = request.body;
    if (!WinHttpSendRequest(requestHandle.get(), headers.c_str(),
                            static_cast<DWORD>(headers.size()),
                            requestBody.empty()
                                ? WINHTTP_NO_REQUEST_DATA
                                : const_cast<char *>(requestBody.data()),
                            static_cast<DWORD>(requestBody.size()),
                            static_cast<DWORD>(requestBody.size()), 0)) {
      const DWORD error = GetLastError();
      response.transportError = MapWinHttpError(error);
      response.detailMessage = WinHttpErrorDetail(error);
      return response;
    }

    if (!WinHttpReceiveResponse(requestHandle.get(), nullptr)) {
      const DWORD error = GetLastError();
      response.transportError = MapWinHttpError(error);
      response.detailMessage = WinHttpErrorDetail(error);
      return response;
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(requestHandle.get(),
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &statusCode,
                             &statusCodeSize, WINHTTP_NO_HEADER_INDEX)) {
      response.transportError = SessionDirectoryBrokerTransportError::InvalidResponse;
      response.detailMessage =
          "Session directory broker response did not include an HTTP status code.";
      return response;
    }

    DWORD correlationSize = 0;
    WinHttpQueryHeaders(requestHandle.get(),
                        WINHTTP_QUERY_CUSTOM,
                        RequestIdHeaderName,
                        WINHTTP_NO_OUTPUT_BUFFER,
                        &correlationSize,
                        WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && correlationSize > 0) {
      std::wstring correlation(correlationSize / sizeof(wchar_t), L'\0');
      if (WinHttpQueryHeaders(requestHandle.get(), WINHTTP_QUERY_CUSTOM,
                              RequestIdHeaderName, correlation.data(),
                              &correlationSize, WINHTTP_NO_HEADER_INDEX)) {
        if (!correlation.empty() && correlation.back() == L'\0') {
          correlation.pop_back();
        }
        response.correlationId = Narrow(correlation);
      }
    }

    String responseBody;
    for (;;) {
      DWORD availableSize = 0;
      if (!WinHttpQueryDataAvailable(requestHandle.get(), &availableSize)) {
        response.transportError = SessionDirectoryBrokerTransportError::InvalidResponse;
        response.detailMessage =
            "Failed while reading the session directory broker response body.";
        return response;
      }
      if (availableSize == 0) {
        break;
      }

      std::vector<char> buffer(availableSize);
      DWORD bytesRead = 0;
      if (!WinHttpReadData(requestHandle.get(), buffer.data(), availableSize,
                           &bytesRead)) {
        response.transportError = SessionDirectoryBrokerTransportError::InvalidResponse;
        response.detailMessage =
            "Failed while reading the session directory broker response body.";
        return response;
      }
      responseBody.append(buffer.data(), buffer.data() + bytesRead);
    }

    response.success = true;
    response.statusCode = static_cast<int>(statusCode);
    response.body = std::move(responseBody);
    return response;
  }

private:
  std::wstring m_host;
  INTERNET_PORT m_port = 0;
  std::wstring m_basePath;
  bool m_secure = false;
  int m_timeoutMs = 5000;
  String m_authToken;
  bool m_allowInsecureLocalDevelopment = false;
};
#endif
} // namespace

SessionDirectoryBrokerTransportPtr CreateWinHttpSessionDirectoryBrokerTransport(
    const SessionDirectoryBrokerRuntimeConfig &config, String &detailMessage) {
  detailMessage.clear();
#ifdef _WIN32
  std::wstring url = Widen(config.baseUrl);
  if (url.empty()) {
    detailMessage = "Session directory broker base URL could not be encoded.";
    return nullptr;
  }

  URL_COMPONENTS components{};
  components.dwStructSize = sizeof(components);
  components.dwSchemeLength = static_cast<DWORD>(-1);
  components.dwHostNameLength = static_cast<DWORD>(-1);
  components.dwUrlPathLength = static_cast<DWORD>(-1);
  components.dwExtraInfoLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0,
                       &components)) {
    detailMessage = "Session directory broker base URL is invalid.";
    return nullptr;
  }

  if (components.lpszHostName == nullptr || components.dwHostNameLength == 0) {
    detailMessage = "Session directory broker base URL is missing a host name.";
    return nullptr;
  }

  const bool secure = components.nScheme == INTERNET_SCHEME_HTTPS;
  const bool insecure = components.nScheme == INTERNET_SCHEME_HTTP;
  if (!secure && !insecure) {
    detailMessage =
        "Session directory broker base URL must use HTTP or HTTPS.";
    return nullptr;
  }

  std::wstring host(components.lpszHostName, components.dwHostNameLength);
  std::wstring basePath = L"/";
  if (components.lpszUrlPath != nullptr && components.dwUrlPathLength > 0) {
    basePath.assign(components.lpszUrlPath, components.dwUrlPathLength);
  }
  if (components.lpszExtraInfo != nullptr && components.dwExtraInfoLength > 0) {
    basePath.append(components.lpszExtraInfo, components.dwExtraInfoLength);
  }

  return std::make_shared<WinHttpSessionDirectoryBrokerTransport>(
      std::move(host), components.nPort, std::move(basePath), secure,
      static_cast<int>(config.requestTimeoutMs), config.authToken,
      config.allowInsecureHttpForLocalDev);
#else
  (void)config;
  detailMessage =
      "WinHTTP session directory broker transport is only available on Windows.";
  return nullptr;
#endif
}
} // namespace ToolKit::ToolKitNetworking
