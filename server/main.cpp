#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/config.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <memory>
#include <map>
#include <mutex>
#include <random>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;
using json = nlohmann::json;


struct Session {
    std::string id;
    websocket::stream<beast::tcp_stream>* receiver = nullptr; 
    websocket::stream<beast::tcp_stream>* sender = nullptr; 
    std::mutex mtx;

    void send_to_peer(bool from_sender, const std::string& message) {
        std::lock_guard<std::mutex> lock(mtx);
        auto* target = from_sender ? receiver : sender;
        if (target) {
            try {
                target->text(true);
                target->write(net::buffer(message));
            } catch (const std::exception& e) {
                std::cerr << "Send error: " << e.what() << std::endl;
            }
        }
    }
};

std::map<std::string, std::shared_ptr<Session>> sessions;
std::mutex session_manager_mtx;

std::string generate_session_id() {
    static const char alphanum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string s;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(alphanum) - 2);
    for (int i = 0; i < 6; ++i) s += alphanum[dis(gen)];
    return s;
}


void do_websocket_session(tcp::socket socket, std::string session_id, bool is_sender, std::shared_ptr<http::request<http::string_body>> req) {
    try {
        auto ws = std::make_unique<websocket::stream<beast::tcp_stream>>(std::move(socket));
        ws->accept(*req);

        std::shared_ptr<Session> current_session;
        {
            std::lock_guard<std::mutex> lock(session_manager_mtx);
            if (sessions.find(session_id) == sessions.end()) {
                std::cerr << "Session not found: " << session_id << std::endl;
                return;
            }
            current_session = sessions[session_id];
            
            std::lock_guard<std::mutex> s_lock(current_session->mtx);
            if (is_sender) current_session->sender = ws.get();
            else current_session->receiver = ws.get();
        }

        std::cout << (is_sender ? "Sender" : "Receiver") << " connected to session " << session_id << std::endl;

        beast::flat_buffer buffer;
        for (;;) {
            ws->read(buffer);
            auto message = beast::buffers_to_string(buffer.data());
            
            std::cout << "Forwarding " << message.size() << " bytes from " << (is_sender ? "Sender" : "Receiver") << std::endl;

            current_session->send_to_peer(is_sender, message);
            
            buffer.consume(buffer.size());
        }

    } catch (beast::system_error const& se) {
        if (se.code() != websocket::error::closed) {
            std::cerr << "WebSocket Error: " << se.code().message() << std::endl;
        }
    } catch (std::exception const& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    
    if (sessions.count(session_id)) {
        std::lock_guard<std::mutex> lock(session_manager_mtx);
        auto s = sessions[session_id];
        std::lock_guard<std::mutex> s_lock(s->mtx);
        if (is_sender && s->sender) s->sender = nullptr;
        if (!is_sender && s->receiver) s->receiver = nullptr;
    }
}

std::string get_mime_type(const std::string& path) {
    if (path.length() >= 5 && path.substr(path.length() - 5) == ".html") return "text/html";
    if (path.length() >= 3 && path.substr(path.length() - 3) == ".js") return "application/javascript";
    if (path.length() >= 4 && path.substr(path.length() - 4) == ".css") return "text/css";
    return "text/plain";
}

void do_http_session(tcp::socket socket) {
    try {
        auto stream = std::make_shared<beast::tcp_stream>(std::move(socket));
        beast::flat_buffer buffer;
        auto req = std::make_shared<http::request<http::string_body>>();

        http::read(*stream, buffer, *req);

        if (req->method() == http::verb::get) {
            std::string target(req->target());
            std::cout << "Request: " << target << " Upgrade: " << websocket::is_upgrade(*req) << std::endl;

            if (target == "/create") {
                std::string id = generate_session_id();
                {
                    std::lock_guard<std::mutex> lock(session_manager_mtx);
                    auto s = std::make_shared<Session>();
                    s->id = id;
                    sessions[id] = s;
                }
                
                json resp;
                resp["session_id"] = id;
                
                http::response<http::string_body> res{http::status::ok, req->version()};
                res.set(http::field::server, "MirrorServer");
                res.set(http::field::content_type, "application/json");
                res.set(http::field::access_control_allow_origin, "*");
                res.body() = resp.dump();
                res.prepare_payload();
                http::write(*stream, res);
                return;
            }
            
            if (websocket::is_upgrade(*req)) {
                auto q_pos = target.find('?');
                std::string id, role;
                if (q_pos != std::string::npos) {
                    std::string query = target.substr(q_pos + 1);
                    auto id_pos = query.find("id=");
                    if (id_pos != std::string::npos) {
                        auto amp = query.find('&', id_pos);
                        id = query.substr(id_pos + 3, amp == std::string::npos ? std::string::npos : amp - (id_pos + 3));
                    }
                    auto role_pos = query.find("role=");
                    if (role_pos != std::string::npos) {
                        auto amp = query.find('&', role_pos);
                        role = query.substr(role_pos + 5, amp == std::string::npos ? std::string::npos : amp - (role_pos + 5));
                    }
                }
                
                if (id.empty()) {
                    http::response<http::string_body> res{http::status::bad_request, req->version()};
                    res.body() = "Missing session ID";
                    res.prepare_payload();
                    http::write(*stream, res);
                    return;
                }

                do_websocket_session(stream->release_socket(), id, (role == "sender"), req);
                return;
            }

            if (target == "/" || target.empty()) target = "/index.html";
            if (target.find("..") != std::string::npos) {
                 http::response<http::string_body> res{http::status::not_found, req->version()};
                 res.body() = "Not found";
                 http::write(*stream, res);
                 return;
            }
            
            auto q = target.find('?');
            if (q != std::string::npos) target = target.substr(0, q);

            std::string path = "public" + target; 
            std::ifstream file(path.c_str());
            if (file) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                http::response<http::string_body> res{http::status::ok, req->version()};
                res.set(http::field::server, "MirrorServer");
                res.set(http::field::content_type, get_mime_type(path));
                res.body() = buffer.str();
                res.prepare_payload();
                http::write(*stream, res);
            } else {
                http::response<http::string_body> res{http::status::not_found, req->version()};
                res.body() = "File not found: " + path;
                res.prepare_payload();
                http::write(*stream, res);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "HTTP Error: " << e.what() << std::endl;
    }
}

int main(int argc, char* argv[]) {
    try {
        net::io_context ioc{1};
        tcp::acceptor acceptor{ioc, {tcp::v4(), 8080}};
        
        std::cout << "Server listening on 0.0.0.0:8080 (HTTP)" << std::endl;
        
        for (;;) {
            tcp::socket socket{ioc};
            acceptor.accept(socket);
            std::thread([socket = std::move(socket)]() mutable {
                do_http_session(std::move(socket));
            }).detach();
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << std::endl;
        return 1;
    }
}
