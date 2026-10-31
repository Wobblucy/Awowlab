#include "HttpClient.h"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>

#include <sstream>
#include <iomanip>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#include <openssl/x509.h>
#endif

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

namespace {

// Long encounters return much more data per page of events than short
// ones, and thirty seconds was not enough for a ten-minute fight.
constexpr int kDefaultTimeoutMs = 120000;

// Beast caps a response body at 8 MB by default. A page of fight events
// is capped at ten thousand events rather than by size, so a long
// encounter can exceed that; raising the cap keeps one page as one query.
constexpr std::uint64_t kMaxResponseBodyBytes = 256ull * 1024 * 1024;

std::optional<std::string> bearerHeader(const std::optional<std::string>& token) {
    if (!token) {
        return std::nullopt;
    }
    return "Bearer " + *token;
}

#ifdef _WIN32
// OpenSSL can't see the Windows certificate stores on its own
// (set_default_verify_paths() only knows Unix-style CA file paths),
// so copy the system-trusted roots into an OpenSSL store once and
// share it across requests.
X509_STORE* systemTrustStore() {
    static X509_STORE* store = [] {
        X509_STORE* s = X509_STORE_new();
        for (const wchar_t* storeName : { L"ROOT", L"CA" }) {
            HCERTSTORE winStore = CertOpenSystemStoreW(0, storeName);
            if (!winStore) {
                continue;
            }
            PCCERT_CONTEXT cert = nullptr;
            while ((cert = CertEnumCertificatesInStore(winStore, cert)) != nullptr) {
                const unsigned char* encoded = cert->pbCertEncoded;
                X509* x509 = d2i_X509(nullptr, &encoded, static_cast<long>(cert->cbCertEncoded));
                if (x509) {
                    X509_STORE_add_cert(s, x509);
                    X509_free(x509);
                }
            }
            CertCloseStore(winStore, 0);
        }
        return s;
    }();
    return store;
}
#endif

} // namespace

namespace warcraftlogs::http {

HttpClient::HttpClient()
    : m_timeoutMs(kDefaultTimeoutMs)
{
}

HttpClient::~HttpClient() = default;

HttpClient::HttpClient(HttpClient&&) noexcept = default;
HttpClient& HttpClient::operator=(HttpClient&&) noexcept = default;

void HttpClient::setTimeout(int timeoutMs) {
    m_timeoutMs = timeoutMs;
}

int HttpClient::getTimeout() const noexcept {
    return m_timeoutMs;
}

HttpResponse HttpClient::post(
    const std::string& host,
    const std::string& path,
    const std::string& body,
    const std::string& contentType,
    const std::optional<std::string>& authToken
) {
    return performRequest("POST", host, path, body, contentType, bearerHeader(authToken));
}

HttpResponse HttpClient::get(
    const std::string& host,
    const std::string& path,
    const std::optional<std::string>& authToken
) {
    return performRequest("GET", host, path, "", "", bearerHeader(authToken));
}

HttpResponse HttpClient::postForm(
    const std::string& host,
    const std::string& path,
    const std::map<std::string, std::string>& formData
) {
    std::ostringstream body;
    bool first = true;
    for (const auto& [key, value] : formData) {
        if (!first) body << "&";
        first = false;
        body << urlEncode(key) << "=" << urlEncode(value);
    }

    return performRequest(
        "POST",
        host,
        path,
        body.str(),
        "application/x-www-form-urlencoded",
        std::nullopt
    );
}

HttpResponse HttpClient::performRequest(
    const std::string& method,
    const std::string& host,
    const std::string& path,
    const std::string& body,
    const std::string& contentType,
    const std::optional<std::string>& authHeader
) {
    try {
        net::io_context ioc;
        ssl::context ctx(ssl::context::tlsv12_client);

        // Trust whatever certificate authorities Windows trusts, then
        // require a valid peer certificate. SSL_CTX_set1_cert_store bumps
        // the store's refcount, so the shared store stays valid.
#ifdef _WIN32
        SSL_CTX_set1_cert_store(ctx.native_handle(), systemTrustStore());
#else
        ctx.set_default_verify_paths();
#endif
        ctx.set_verify_mode(ssl::verify_peer);

        tcp::resolver resolver(ioc);
        beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);

        if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
            beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
            throw beast::system_error{ec};
        }

        // Verify the certificate was actually issued for this host;
        // verify_peer alone only checks the chain of trust.
        if (!SSL_set1_host(stream.native_handle(), host.c_str())) {
            beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
            throw beast::system_error{ec};
        }

        beast::get_lowest_layer(stream).expires_after(std::chrono::milliseconds(m_timeoutMs));

        auto const results = resolver.resolve(host, "443");
        beast::get_lowest_layer(stream).connect(results);

        stream.handshake(ssl::stream_base::client);

        ::http::verb verb = ::http::verb::get;
        if (method == "POST") {
            verb = ::http::verb::post;
        } else if (method == "PUT") {
            verb = ::http::verb::put;
        } else if (method == "DELETE") {
            verb = ::http::verb::delete_;
        }

        ::http::request<::http::string_body> req{verb, path, 11};
        req.set(::http::field::host, host);
        req.set(::http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        req.set(::http::field::accept, "application/json");

        if (!contentType.empty()) {
            req.set(::http::field::content_type, contentType);
        }

        if (authHeader) {
            req.set(::http::field::authorization, *authHeader);
        }

        if (!body.empty()) {
            req.body() = body;
        }

        req.prepare_payload();

        ::http::write(stream, req);

        beast::flat_buffer buffer;
        ::http::response_parser<::http::string_body> parser;
        parser.body_limit(kMaxResponseBodyBytes);
        ::http::read(stream, buffer, parser);
        auto& res = parser.get();

        HttpResponse response;
        response.statusCode = static_cast<int>(res.result());
        response.body = std::move(res.body());

        for (const auto& field : res) {
            response.headers[std::string(field.name_string())] = std::string(field.value());
        }

        beast::error_code ec;
        stream.shutdown(ec);
        // ssl::error::stream_truncated is normal here and ignored.

        return response;

    } catch (const std::exception& e) {
        std::string errorMsg = "Network error during " + method + " " + host + path + ": " + e.what();
        throw HttpException(errorMsg);
    } catch (...) {
        // Non-std exceptions, e.g. from OpenSSL.
        std::string errorMsg = "Unknown network error during " + method + " " + host + path;
        throw HttpException(errorMsg);
    }
}

std::string HttpClient::urlEncode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (char c : value) {
        // RFC 3986 unreserved characters pass through; the rest are percent-encoded.
        if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
            escaped << std::nouppercase;
        }
    }

    return escaped.str();
}

} // namespace warcraftlogs::http
