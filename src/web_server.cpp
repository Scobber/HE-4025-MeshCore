#include "web_server.h"

#include "linux_hal.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>

namespace {

std::string errno_message(const std::string &prefix) {
    return prefix + ": " + strerror(errno);
}

std::string trim(const std::string &input) {
    const char *ws = " \t\r\n";
    const std::string::size_type first = input.find_first_not_of(ws);
    if (first == std::string::npos) {
        return "";
    }
    const std::string::size_type last = input.find_last_not_of(ws);
    return input.substr(first, last - first + 1);
}

std::string lower(const std::string &input) {
    std::string out = input;
    std::transform(out.begin(), out.end(), out.begin(), ::tolower);
    return out;
}

std::string json_escape(const std::string &input) {
    std::ostringstream out;
    for (size_t i = 0; i < input.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(input[i]);
        switch (c) {
        case '"':
            out << "\\\"";
            break;
        case '\\':
            out << "\\\\";
            break;
        case '\b':
            out << "\\b";
            break;
        case '\f':
            out << "\\f";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                out << buf;
            } else {
                out << input[i];
            }
        }
    }
    return out.str();
}

std::string html_escape(const std::string &input) {
    std::string out;
    for (size_t i = 0; i < input.size(); ++i) {
        switch (input[i]) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out += input[i];
        }
    }
    return out;
}

std::string read_first_line(const std::string &path) {
    std::ifstream file(path.c_str());
    std::string line;
    std::getline(file, line);
    return trim(line);
}

std::string read_key_from_openwrt_release(const std::string &key) {
    std::ifstream file("/etc/openwrt_release");
    std::string line;
    const std::string prefix = key + "=";
    while (std::getline(file, line)) {
        if (line.find(prefix) == 0) {
            std::string value = trim(line.substr(prefix.size()));
            if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'') {
                value = value.substr(1, value.size() - 2);
            }
            return value;
        }
    }
    return "";
}

uint64_t read_meminfo_kb(const std::string &key) {
    std::ifstream file("/proc/meminfo");
    std::string label;
    uint64_t value = 0;
    std::string units;
    while (file >> label >> value >> units) {
        if (label == key + ":") {
            return value;
        }
    }
    return 0;
}

std::string query_value(const std::string &query, const std::string &key) {
    size_t pos = 0;
    while (pos <= query.size()) {
        const size_t amp = query.find('&', pos);
        const std::string part = query.substr(pos, amp == std::string::npos ? amp : amp - pos);
        const size_t eq = part.find('=');
        const std::string part_key = eq == std::string::npos ? part : part.substr(0, eq);
        if (part_key == key) {
            return eq == std::string::npos ? "" : part.substr(eq + 1);
        }
        if (amp == std::string::npos) {
            break;
        }
        pos = amp + 1;
    }
    return "";
}

bool query_flag(const std::string &query, const std::string &key) {
    const std::string value = query_value(query, key);
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

bool write_all(int fd, const char *data, size_t len) {
    size_t offset = 0;
    while (offset < len) {
        const ssize_t n = send(fd, data + offset, len - offset, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (n == 0) {
            return false;
        }
        offset += static_cast<size_t>(n);
    }
    return true;
}

bool set_nonblocking(int fd, std::string *error) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        *error = errno_message("fcntl F_GETFL");
        return false;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        *error = errno_message("fcntl F_SETFL");
        return false;
    }
    return true;
}

std::string command_output_from_pipe(int fd) {
    std::string out;
    char buffer[512];
    while (true) {
        const ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (n == 0) {
            break;
        }
        out.append(buffer, static_cast<size_t>(n));
        if (out.size() > 4096) {
            out.erase(0, out.size() - 4096);
        }
    }
    return out;
}

}  // namespace

WebServer::WebServer(const Config *config, DaemonStats *stats)
    : config_(config), stats_(stats), listen_fd_(-1) {}

WebServer::~WebServer() {
    stop();
}

bool WebServer::start(std::string *error) {
    if (!config_->web_enabled) {
        return true;
    }

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        *error = errno_message("socket");
        return false;
    }

    int yes = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(config_->web_port));
    if (inet_aton(config_->web_bind.c_str(), &addr.sin_addr) == 0) {
        *error = "invalid web_bind address: " + config_->web_bind;
        stop();
        return false;
    }

    if (bind(listen_fd_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        *error = errno_message("bind " + config_->web_bind);
        stop();
        return false;
    }

    if (listen(listen_fd_, 4) < 0) {
        *error = errno_message("listen");
        stop();
        return false;
    }

    if (!set_nonblocking(listen_fd_, error)) {
        stop();
        return false;
    }

    return true;
}

void WebServer::stop() {
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
}

void WebServer::tick() {
    if (listen_fd_ < 0) {
        return;
    }

    for (int i = 0; i < 4; ++i) {
        if (!accept_one()) {
            break;
        }
    }
}

bool WebServer::accept_one() {
    struct sockaddr_in peer;
    socklen_t peer_len = sizeof(peer);
    const int client_fd = accept(listen_fd_, reinterpret_cast<struct sockaddr *>(&peer), &peer_len);
    if (client_fd < 0) {
        return false;
    }

    handle_client(client_fd);
    close(client_fd);
    return true;
}

void WebServer::handle_client(int client_fd) {
    struct timeval tv;
    tv.tv_sec = 15;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    HttpRequest request;
    std::string error;
    if (!read_request_headers(client_fd, &request, &error)) {
        send_json(client_fd, 400, "{\"ok\":false,\"error\":\"" + json_escape(error) + "\"}");
        return;
    }

    route_request(client_fd, request);
}

bool WebServer::read_request_headers(int client_fd, HttpRequest *request, std::string *error) {
    std::string data;
    char buffer[512];
    size_t header_end = std::string::npos;

    while (data.size() < 16384) {
        const ssize_t n = recv(client_fd, buffer, sizeof(buffer), 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            *error = errno_message("recv");
            return false;
        }
        if (n == 0) {
            *error = "connection closed before headers";
            return false;
        }
        data.append(buffer, static_cast<size_t>(n));
        header_end = data.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            break;
        }
    }

    if (header_end == std::string::npos) {
        *error = "HTTP headers too large or incomplete";
        return false;
    }

    request->header_bytes = data.substr(0, header_end + 4);
    request->body_prefix = data.substr(header_end + 4);

    std::istringstream stream(request->header_bytes);
    std::string line;
    if (!std::getline(stream, line)) {
        *error = "missing request line";
        return false;
    }
    line = trim(line);
    std::istringstream first(line);
    std::string target;
    if (!(first >> request->method >> target >> request->version)) {
        *error = "bad request line";
        return false;
    }

    const size_t question = target.find('?');
    if (question == std::string::npos) {
        request->path = target;
    } else {
        request->path = target.substr(0, question);
        request->query = target.substr(question + 1);
    }

    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        const size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        request->headers[lower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
    }

    request->content_length = 0;
    const std::map<std::string, std::string>::const_iterator it =
        request->headers.find("content-length");
    if (it != request->headers.end()) {
        char *end = NULL;
        const unsigned long parsed = strtoul(it->second.c_str(), &end, 10);
        if (end == it->second.c_str() || *end != '\0') {
            *error = "invalid Content-Length";
            return false;
        }
        request->content_length = static_cast<size_t>(parsed);
    }

    return true;
}

void WebServer::route_request(int client_fd, const HttpRequest &request) {
    if (request.method == "GET" && request.path == "/") {
        send_response(client_fd, 200, "OK", "text/html; charset=utf-8", render_index());
    } else if (request.method == "GET" && request.path == "/api/stats") {
        send_json(client_fd, 200, render_stats_json());
    } else if (request.method == "POST" && request.path == "/api/ota") {
        handle_ota_upload(client_fd, request);
    } else {
        send_json(client_fd, 404, "{\"ok\":false,\"error\":\"not found\"}");
    }
}

void WebServer::handle_ota_upload(int client_fd, const HttpRequest &request) {
    if (!config_->ota_enabled) {
        send_json(client_fd, 403, "{\"ok\":false,\"error\":\"OTA is disabled\"}");
        return;
    }
    if (!check_ota_token(request)) {
        send_json(client_fd, 403, "{\"ok\":false,\"error\":\"bad OTA token\"}");
        return;
    }
    if (stats_->ota_in_progress) {
        send_json(client_fd, 409, "{\"ok\":false,\"error\":\"OTA already in progress\"}");
        return;
    }
    if (request.content_length == 0) {
        send_json(client_fd, 400, "{\"ok\":false,\"error\":\"empty firmware upload\"}");
        return;
    }
    if (request.content_length > config_->ota_max_upload_bytes) {
        send_json(client_fd, 413, "{\"ok\":false,\"error\":\"firmware upload too large\"}");
        return;
    }

    stats_->ota_in_progress = true;

    std::string error;
    if (!write_ota_body(client_fd, request, &error)) {
        stats_->ota_in_progress = false;
        send_json(client_fd, 500, "{\"ok\":false,\"error\":\"" + json_escape(error) + "\"}");
        return;
    }

    std::string output;
    if (!run_sysupgrade_test(config_->ota_image_path, &output, &error)) {
        stats_->ota_in_progress = false;
        unlink(config_->ota_image_path.c_str());
        send_json(client_fd, 400,
                  "{\"ok\":false,\"error\":\"" + json_escape(error) + "\",\"output\":\"" +
                      json_escape(output) + "\"}");
        return;
    }

    if (query_flag(request.query, "validate")) {
        stats_->ota_in_progress = false;
        unlink(config_->ota_image_path.c_str());
        send_json(client_fd, 200,
                  "{\"ok\":true,\"validated\":true,\"output\":\"" + json_escape(output) + "\"}");
        return;
    }

    const bool keep_config = query_flag(request.query, "keep") || config_->ota_keep_config;
    if (!start_sysupgrade_flash(config_->ota_image_path, keep_config, &error)) {
        stats_->ota_in_progress = false;
        send_json(client_fd, 500, "{\"ok\":false,\"error\":\"" + json_escape(error) + "\"}");
        return;
    }

    send_json(client_fd, 202,
              "{\"ok\":true,\"accepted\":true,\"message\":\"sysupgrade will start now\"}");
}

bool WebServer::write_ota_body(int client_fd, const HttpRequest &request, std::string *error) {
    const int fd = open(config_->ota_image_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        *error = errno_message("open " + config_->ota_image_path);
        return false;
    }

    size_t written_total = 0;
    if (!request.body_prefix.empty()) {
        const size_t to_write = std::min(request.body_prefix.size(), request.content_length);
        const ssize_t n = write(fd, request.body_prefix.data(), to_write);
        if (n < 0 || static_cast<size_t>(n) != to_write) {
            *error = errno_message("write firmware prefix");
            close(fd);
            return false;
        }
        written_total += to_write;
    }

    char buffer[4096];
    while (written_total < request.content_length) {
        const size_t remaining = request.content_length - written_total;
        const ssize_t n =
            recv(client_fd, buffer, std::min(remaining, sizeof(buffer)), 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            *error = errno_message("recv firmware body");
            close(fd);
            return false;
        }
        if (n == 0) {
            *error = "connection closed during firmware upload";
            close(fd);
            return false;
        }
        const ssize_t written = write(fd, buffer, static_cast<size_t>(n));
        if (written < 0 || written != n) {
            *error = errno_message("write firmware body");
            close(fd);
            return false;
        }
        written_total += static_cast<size_t>(n);
    }

    if (fsync(fd) < 0) {
        *error = errno_message("fsync firmware image");
        close(fd);
        return false;
    }
    close(fd);
    return true;
}

bool WebServer::check_ota_token(const HttpRequest &request) const {
    if (config_->ota_token.empty() || config_->ota_token == "change-me") {
        return false;
    }

    const std::map<std::string, std::string>::const_iterator header =
        request.headers.find("x-ota-token");
    if (header != request.headers.end() && header->second == config_->ota_token) {
        return true;
    }

    return query_value(request.query, "token") == config_->ota_token;
}

bool WebServer::run_sysupgrade_test(const std::string &image_path, std::string *output,
                                    std::string *error) {
    int pipe_fd[2];
    if (pipe(pipe_fd) < 0) {
        *error = errno_message("pipe");
        return false;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        *error = errno_message("fork");
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return false;
    }

    if (pid == 0) {
        close(pipe_fd[0]);
        dup2(pipe_fd[1], STDOUT_FILENO);
        dup2(pipe_fd[1], STDERR_FILENO);
        close(pipe_fd[1]);
        execl("/sbin/sysupgrade", "sysupgrade", "-T", image_path.c_str(), static_cast<char *>(NULL));
        _exit(127);
    }

    close(pipe_fd[1]);
    *output = command_output_from_pipe(pipe_fd[0]);
    close(pipe_fd[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        *error = errno_message("waitpid sysupgrade -T");
        return false;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::ostringstream msg;
        msg << "sysupgrade validation failed";
        if (WIFEXITED(status)) {
            msg << " with exit " << WEXITSTATUS(status);
        }
        *error = msg.str();
        return false;
    }

    return true;
}

bool WebServer::start_sysupgrade_flash(const std::string &image_path, bool keep_config,
                                       std::string *error) {
    const pid_t pid = fork();
    if (pid < 0) {
        *error = errno_message("fork sysupgrade");
        return false;
    }

    if (pid == 0) {
        signal(SIGTERM, SIG_DFL);
        signal(SIGINT, SIG_DFL);
        sleep(2);
        if (keep_config) {
            execl("/sbin/sysupgrade", "sysupgrade", image_path.c_str(), static_cast<char *>(NULL));
        } else {
            execl("/sbin/sysupgrade", "sysupgrade", "-n", image_path.c_str(),
                  static_cast<char *>(NULL));
        }
        _exit(127);
    }

    return true;
}

void WebServer::send_response(int client_fd, int status, const std::string &status_text,
                              const std::string &content_type, const std::string &body) {
    std::ostringstream header;
    header << "HTTP/1.1 " << status << " " << status_text << "\r\n"
           << "Connection: close\r\n"
           << "Cache-Control: no-store\r\n"
           << "Content-Type: " << content_type << "\r\n"
           << "Content-Length: " << body.size() << "\r\n\r\n";
    const std::string header_text = header.str();
    write_all(client_fd, header_text.data(), header_text.size());
    write_all(client_fd, body.data(), body.size());
}

void WebServer::send_json(int client_fd, int status, const std::string &body) {
    const char *status_text = "OK";
    if (status == 202) {
        status_text = "Accepted";
    } else if (status == 400) {
        status_text = "Bad Request";
    } else if (status == 403) {
        status_text = "Forbidden";
    } else if (status == 404) {
        status_text = "Not Found";
    } else if (status == 409) {
        status_text = "Conflict";
    } else if (status == 413) {
        status_text = "Payload Too Large";
    } else if (status >= 500) {
        status_text = "Server Error";
    }
    send_response(client_fd, status, status_text, "application/json", body);
}

std::string WebServer::render_index() const {
    std::ostringstream html;
    html << "<!doctype html><html><head><meta charset=\"utf-8\">"
         << "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
         << "<title>MeshCore HE-4025</title>"
         << "<style>"
         << "body{font-family:system-ui,-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;margin:0;background:#f4f6f8;color:#172026}"
         << "main{max-width:980px;margin:0 auto;padding:20px}"
         << "header{display:flex;justify-content:space-between;gap:16px;align-items:center;margin-bottom:18px}"
         << "h1{font-size:24px;margin:0}section{background:white;border:1px solid #d8dee4;border-radius:8px;padding:16px;margin:12px 0}"
         << ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px}"
         << ".tile{border:1px solid #e3e8ee;border-radius:6px;padding:10px}.label{font-size:12px;color:#65727f}.value{font-size:20px;font-weight:650}"
         << "code,pre{background:#eef2f6;border-radius:6px;padding:2px 5px}button{border:0;border-radius:6px;background:#165dff;color:white;padding:9px 12px;font-weight:650}"
         << "button.secondary{background:#415466}input{max-width:100%;box-sizing:border-box}#log{white-space:pre-wrap;font-size:13px;max-height:220px;overflow:auto}"
         << "</style></head><body><main><header><div><h1>"
         << html_escape(config_->device_name)
         << "</h1><div>HE-4025 SX1276 MeshCore firmware</div></div><div id=\"state\">loading</div></header>"
         << "<section><div class=\"grid\" id=\"tiles\"></div></section>"
         << "<section><h2>Radio</h2><pre id=\"radio\"></pre></section>"
         << "<section><h2>OTA Upgrade</h2>"
         << "<p>Upload a sysupgrade image built for this Dragino HE target. OTA is token-gated and validates with <code>sysupgrade -T</code> before flashing.</p>"
         << "<input id=\"token\" type=\"password\" placeholder=\"OTA token\"> "
         << "<input id=\"file\" type=\"file\"> "
         << "<button class=\"secondary\" onclick=\"ota(true)\">Validate</button> "
         << "<button onclick=\"ota(false)\">Flash</button>"
         << "<pre id=\"log\"></pre></section>"
         << "</main><script>"
         << "async function stats(){let r=await fetch('/api/stats');let s=await r.json();"
         << "state.textContent=s.radio.ready?'radio ready':'radio not ready';"
         << "let t=[['Uptime',s.uptime_s+'s'],['RX packets',s.radio.rx_packets],['TX packets',s.radio.tx_packets],['RSSI',s.radio.last_rssi_dbm+' dBm'],['SNR',s.radio.last_snr_db+' dB'],['Load',s.system.loadavg],['Memory free',s.system.mem_free_kb+' kB'],['Root free',s.system.root_free_kb+' kB']];"
         << "tiles.innerHTML=t.map(x=>'<div class=\"tile\"><div class=\"label\">'+x[0]+'</div><div class=\"value\">'+x[1]+'</div></div>').join('');"
         << "radio.textContent=JSON.stringify(s.radio,null,2);}setInterval(stats,3000);stats();"
         << "async function ota(validate){let f=file.files[0];if(!f){log.textContent='Choose a sysupgrade image first.';return}"
         << "let q=validate?'?validate=1':'';log.textContent='Uploading '+f.name+'...';"
         << "let r=await fetch('/api/ota'+q,{method:'POST',headers:{'X-OTA-Token':token.value},body:f});"
         << "let text=await r.text();try{text=JSON.stringify(JSON.parse(text),null,2)}catch(e){}log.textContent=text;}"
         << "</script></body></html>";
    return html.str();
}

std::string WebServer::render_stats_json() const {
    const uint64_t now = hal_millis();
    const uint64_t uptime_s =
        stats_->started_ms == 0 || now < stats_->started_ms ? 0 : (now - stats_->started_ms) / 1000;

    struct statvfs rootfs;
    uint64_t root_free_kb = 0;
    if (statvfs("/", &rootfs) == 0) {
        root_free_kb =
            static_cast<uint64_t>(rootfs.f_bavail) * static_cast<uint64_t>(rootfs.f_frsize) / 1024;
    }

    std::ostringstream json;
    json << "{"
         << "\"ok\":true,"
         << "\"device\":\"" << json_escape(config_->device_name) << "\","
         << "\"uptime_s\":" << uptime_s << ","
         << "\"config\":{"
         << "\"role\":\"" << json_escape(config_->role) << "\","
         << "\"region\":\"" << json_escape(config_->region) << "\","
         << "\"frequency\":" << static_cast<unsigned long long>(config_->frequency) << ","
         << "\"spreading_factor\":" << config_->spreading_factor << ","
         << "\"bandwidth\":" << config_->bandwidth << ","
         << "\"coding_rate\":" << config_->coding_rate << ","
         << "\"tx_power\":" << config_->tx_power << ","
         << "\"spi_device\":\"" << json_escape(config_->spi_device) << "\""
         << "},"
         << "\"radio\":{"
         << "\"ready\":" << (stats_->radio_ready ? "true" : "false") << ","
         << "\"rx_continuous\":" << (stats_->rx_continuous ? "true" : "false") << ","
         << "\"sx1276_version\":\"0x";
    char version[3];
    snprintf(version, sizeof(version), "%02X", stats_->sx1276_version);
    json << version << "\","
         << "\"rx_packets\":" << static_cast<unsigned long long>(stats_->rx_packets) << ","
         << "\"tx_packets\":" << static_cast<unsigned long long>(stats_->tx_packets) << ","
         << "\"rx_errors\":" << static_cast<unsigned long long>(stats_->rx_errors) << ","
         << "\"tx_errors\":" << static_cast<unsigned long long>(stats_->tx_errors) << ","
         << "\"last_rx_bytes\":" << stats_->last_rx_bytes << ","
         << "\"last_tx_bytes\":" << stats_->last_tx_bytes << ","
         << "\"last_rssi_dbm\":" << stats_->last_rssi_dbm << ","
         << "\"last_snr_db\":" << stats_->last_snr_db << ","
         << "\"last_rx_hex\":\"" << json_escape(stats_->last_rx_hex) << "\","
         << "\"last_tx_hex\":\"" << json_escape(stats_->last_tx_hex) << "\","
         << "\"last_error\":\"" << json_escape(stats_->last_error) << "\","
         << "\"ota_in_progress\":" << (stats_->ota_in_progress ? "true" : "false")
         << "},"
         << "\"system\":{"
         << "\"hostname\":\"" << json_escape(read_first_line("/proc/sys/kernel/hostname")) << "\","
         << "\"openwrt_release\":\"" << json_escape(read_key_from_openwrt_release("DISTRIB_DESCRIPTION")) << "\","
         << "\"kernel\":\"" << json_escape(read_first_line("/proc/version")) << "\","
         << "\"loadavg\":\"" << json_escape(read_first_line("/proc/loadavg")) << "\","
         << "\"mem_total_kb\":" << static_cast<unsigned long long>(read_meminfo_kb("MemTotal")) << ","
         << "\"mem_free_kb\":" << static_cast<unsigned long long>(read_meminfo_kb("MemAvailable")) << ","
         << "\"root_free_kb\":" << static_cast<unsigned long long>(root_free_kb)
         << "}"
         << "}";
    return json.str();
}
