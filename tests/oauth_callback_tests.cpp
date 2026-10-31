// OAuth callback server tests: the local server that receives the browser
// redirect during WarcraftLogs sign-in. Everything here talks to 127.0.0.1
// only; no request leaves the machine.

#include <catch2/catch_test_macros.hpp>

#include "LocalHttpServer.h"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <chrono>
#include <string>
#include <thread>

namespace beast = boost::beast;
namespace bhttp = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

using warcraftlogs::http::LocalHttpServer;

namespace {

// First free port in a range unlikely to clash with the real sign-in ports
int findFreePort(LocalHttpServer& server) {
    for (int port = 47810; port < 47860; ++port) {
        if (server.tryBind(port)) {
            return port;
        }
    }
    FAIL("No free port for the callback server");
    return 0;
}

tcp::socket connectTo(net::io_context& ioc, int port) {
    tcp::socket socket(ioc);
    socket.connect(tcp::endpoint(net::ip::make_address("127.0.0.1"),
                                 static_cast<unsigned short>(port)));
    return socket;
}

void sendGet(tcp::socket& socket, const std::string& target) {
    std::string request = "GET " + target + " HTTP/1.1\r\nHost: localhost\r\n\r\n";
    net::write(socket, net::buffer(request));
}

std::string readBody(tcp::socket& socket) {
    beast::flat_buffer buffer;
    bhttp::response<bhttp::string_body> res;
    beast::error_code ec;
    bhttp::read(socket, buffer, res, ec);
    return res.body();
}

bool waitForComplete(const LocalHttpServer& server, std::chrono::seconds limit) {
    auto deadline = std::chrono::steady_clock::now() + limit;
    while (!server.isComplete() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return server.isComplete();
}

} // namespace

TEST_CASE("A client that sends nothing does not block the callback", "[oauth][server]") {
    LocalHttpServer server;
    int port = findFreePort(server);
    REQUIRE(server.startAsync(port, 30));

    net::io_context ioc;
    tcp::socket silent = connectTo(ioc, port);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    tcp::socket browser = connectTo(ioc, port);
    sendGet(browser, "/callback?code=abc&state=xyz");

    bool complete = waitForComplete(server, std::chrono::seconds(5));
    beast::error_code ec;
    silent.close(ec);
    CHECK(complete);

    server.stop();
    auto result = server.getResult();
    REQUIRE(result.has_value());
    CHECK(result->code == "abc");
    CHECK(result->state == "xyz");
}

TEST_CASE("A silent client cannot keep the server past its deadline", "[oauth][server]") {
    LocalHttpServer server;
    int port = findFreePort(server);
    REQUIRE(server.startAsync(port, 2));

    net::io_context ioc;
    tcp::socket silent = connectTo(ioc, port);

    bool complete = waitForComplete(server, std::chrono::seconds(6));
    beast::error_code ec;
    silent.close(ec);
    CHECK(complete);

    server.stop();
    auto result = server.getResult();
    REQUIRE(result.has_value());
    CHECK_FALSE(result->isSuccess());
}

TEST_CASE("Requests for other paths do not end the wait", "[oauth][server]") {
    LocalHttpServer server;
    int port = findFreePort(server);
    REQUIRE(server.startAsync(port, 30));

    net::io_context ioc;
    bool callbackDelivered = false;
    try {
        tcp::socket favicon = connectTo(ioc, port);
        sendGet(favicon, "/favicon.ico");
        readBody(favicon);

        tcp::socket browser = connectTo(ioc, port);
        sendGet(browser, "/callback?code=abc&state=xyz");
        readBody(browser);
        callbackDelivered = true;
    } catch (const std::exception&) {
        // Connection refused: the server stopped after the first request
    }

    CHECK(waitForComplete(server, std::chrono::seconds(5)));
    server.stop();

    CHECK(callbackDelivered);
    auto result = server.getResult();
    REQUIRE(result.has_value());
    CHECK(result->code == "abc");
}

TEST_CASE("The error page does not echo markup from the request", "[oauth][server]") {
    LocalHttpServer server;
    int port = findFreePort(server);
    REQUIRE(server.startAsync(port, 30));

    net::io_context ioc;
    tcp::socket browser = connectTo(ioc, port);
    sendGet(browser, "/callback?error=access_denied&error_description=%3Cscript%3Ealert(1)%3C%2Fscript%3E");
    std::string body = readBody(browser);

    CHECK(waitForComplete(server, std::chrono::seconds(5)));
    server.stop();

    CHECK_FALSE(body.empty());
    CHECK(body.find("<script>") == std::string::npos);

    auto result = server.getResult();
    REQUIRE(result.has_value());
    CHECK(result->error == "access_denied");
}
