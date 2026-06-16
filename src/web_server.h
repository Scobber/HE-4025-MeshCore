#ifndef MESHCORE_HE4025_WEB_SERVER_H
#define MESHCORE_HE4025_WEB_SERVER_H

#include <stdint.h>

#include <map>
#include <string>

#include "config.h"
#include "stats.h"

class WebServer {
public:
    WebServer(const Config *config, DaemonStats *stats);
    ~WebServer();

    bool start(std::string *error);
    void stop();
    void tick();

private:
    struct HttpRequest {
        std::string method;
        std::string path;
        std::string query;
        std::string version;
        std::map<std::string, std::string> headers;
        std::string header_bytes;
        std::string body_prefix;
        size_t content_length;
    };

    bool accept_one();
    void handle_client(int client_fd);
    bool read_request_headers(int client_fd, HttpRequest *request, std::string *error);
    void route_request(int client_fd, const HttpRequest &request);
    void handle_ota_upload(int client_fd, const HttpRequest &request);
    bool write_ota_body(int client_fd, const HttpRequest &request, std::string *error);
    bool check_ota_token(const HttpRequest &request) const;
    bool run_sysupgrade_test(const std::string &image_path, std::string *output,
                             std::string *error);
    bool start_sysupgrade_flash(const std::string &image_path, bool keep_config,
                                std::string *error);
    void send_response(int client_fd, int status, const std::string &status_text,
                       const std::string &content_type, const std::string &body);
    void send_json(int client_fd, int status, const std::string &body);
    std::string render_index() const;
    std::string render_stats_json() const;

    const Config *config_;
    DaemonStats *stats_;
    int listen_fd_;
};

#endif

