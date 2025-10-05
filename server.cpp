// server.cpp
// Simple single-threaded HTTP server that serves files from ./www
// and handles POST /contact to append messages.csv
// Usage: ./server [port]
// NOTE: For learning/testing only. Not for production use.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

string url_decode(const string &src) {
    string ret;
    char ch;
    int i, ii;
    for (i=0; i<src.length(); i++) {
        if (int(src[i]) == 37) {
            sscanf(src.substr(i+1,2).c_str(), "%x", &ii);
            ch = static_cast<char>(ii);
            ret += ch;
            i = i+2;
        } else if (src[i] == '+') {
            ret += ' ';
        } else {
            ret += src[i];
        }
    }
    return ret;
}

map<string,string> parse_form_urlencoded(const string &body) {
    map<string,string> out;
    stringstream ss(body);
    string pair;
    while (getline(ss, pair, '&')) {
        auto pos = pair.find('=');
        if (pos != string::npos) {
            string k = url_decode(pair.substr(0,pos));
            string v = url_decode(pair.substr(pos+1));
            out[k]=v;
        }
    }
    return out;
}

string now_iso() {
    time_t t = time(nullptr);
    char buf[100];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&t));
    return string(buf);
}

string guess_mime(const string &path) {
    if (path.find(".html")!=string::npos) return "text/html";
    if (path.find(".css")!=string::npos) return "text/css";
    if (path.find(".js")!=string::npos) return "application/javascript";
    if (path.find(".png")!=string::npos) return "image/png";
    if (path.find(".jpg")!=string::npos || path.find(".jpeg")!=string::npos) return "image/jpeg";
    if (path.find(".svg")!=string::npos) return "image/svg+xml";
    return "application/octet-stream";
}

string read_file_string(const string &path) {
    ifstream f(path, ios::binary);
    if (!f) return "";
    stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main(int argc, char** argv) {
    int port = 8080;
    if (argc > 1) port = stoi(argv[1]);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) { perror("socket failed"); return 1; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed"); return 2;
    }
    if (listen(server_fd, 10) < 0) {
        perror("listen"); return 3;
    }

    cout << "Server running on http://localhost:" << port << " — serving ./www\n";

    while (1) {
        int addrlen = sizeof(address);
        int new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
        if (new_socket < 0) { perror("accept"); continue; }

        // read request (simple)
        string req;
        char buf[4096];
        ssize_t len = recv(new_socket, buf, sizeof(buf)-1, 0);
        if (len <= 0) { close(new_socket); continue; }
        buf[len]='\0';
        req = string(buf);

        // parse request line
        string method, path, protocol;
        {
            stringstream sreq(req);
            sreq >> method >> path >> protocol;
        }

        // parse headers for Content-Length
        size_t header_end = req.find("\r\n\r\n");
        string headers = (header_end!=string::npos) ? req.substr(0, header_end) : req;
        int content_length = 0;
        {
            regex re("Content-Length:\\s*(\\d+)", regex_constants::icase);
            smatch m;
            if (regex_search(headers, m, re)) content_length = stoi(m[1].str());
        }

        string body;
        if (content_length > 0) {
            // body might not be fully received in first recv; read remaining
            size_t already = (header_end==string::npos) ? req.size() : (req.size() - (header_end+4));
            if (already >= (size_t)content_length) {
                body = req.substr(header_end+4, content_length);
            } else {
                body = req.substr(header_end+4);
                int remaining = content_length - already;
                while (remaining > 0) {
                    ssize_t r = recv(new_socket, buf, sizeof(buf)-1, 0);
                    if (r <= 0) break;
                    buf[r]='\0';
                    body += string(buf, r);
                    remaining -= r;
                }
            }
        }

        if (method == "GET") {
            string filePath = "./www";
            if (path == "/") path = "/index.html";
            // prevent path traversal
            if (path.find("..") != string::npos) {
                string resp = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
                send(new_socket, resp.c_str(), resp.size(), 0);
                close(new_socket);
                continue;
            }
            filePath += path;
            string content = read_file_string(filePath);
            if (content.empty()) {
                string notf = "<h1>404 Not Found</h1>";
                string resp = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length: "
                              + to_string(notf.size()) + "\r\n\r\n" + notf;
                send(new_socket, resp.c_str(), resp.size(), 0);
                close(new_socket); continue;
            }
            string mime = guess_mime(filePath);
            string resp = "HTTP/1.1 200 OK\r\nContent-Type: " + mime + "\r\nContent-Length: "
                          + to_string(content.size()) + "\r\n\r\n" + content;
            send(new_socket, resp.c_str(), resp.size(), 0);
            close(new_socket);
            continue;
        } else if (method == "POST" && path == "/contact") {
            // parse form x-www-form-urlencoded
            auto data = parse_form_urlencoded(body);
            string name = data.count("name") ? data["name"] : "";
            string email = data.count("email") ? data["email"] : "";
            string phone = data.count("phone") ? data["phone"] : "";
            string message = data.count("message") ? data["message"] : "";

            // append to messages.csv
            ofstream of("messages.csv", ios::app);
            if (of) {
                // CSV: timestamp, name, email, phone, message
                string t = now_iso();
                // escape double quotes by doubling them
                auto esc = [&](const string &s){
                    string res = s;
                    size_t pos = 0;
                    while ((pos = res.find("\"", pos)) != string::npos) {
                        res.insert(pos, "\"");
                        pos += 2;
                    }
                    return res;
                };
                of << "\"" << now_iso() << "\","
                   << "\"" << esc(name) << "\","
                   << "\"" << esc(email) << "\","
                   << "\"" << esc(phone) << "\","
                   << "\"" << esc(message) << "\"\n";
                of.close();
                string bodyjson = "{\"ok\":true}";
                string resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                              + to_string(bodyjson.size()) + "\r\n\r\n" + bodyjson;
                send(new_socket, resp.c_str(), resp.size(), 0);
            } else {
                string bodyjson = "{\"ok\":false,\"error\":\"could not open messages.csv\"}";
                string resp = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\nContent-Length: "
                              + to_string(bodyjson.size()) + "\r\n\r\n" + bodyjson;
                send(new_socket, resp.c_str(), resp.size(), 0);
            }
            close(new_socket);
            continue;
        } else {
            string notimpl = "{\"ok\":false,\"error\":\"not implemented\"}";
            string resp = "HTTP/1.1 501 Not Implemented\r\nContent-Type: application/json\r\nContent-Length: "
                          + to_string(notimpl.size()) + "\r\n\r\n" + notimpl;
            send(new_socket, resp.c_str(), resp.size(), 0);
            close(new_socket);
            continue;
        }

        close(new_socket);
    }

    close(server_fd);
    return 0;
}
