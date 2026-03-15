extern "C" {
#include "php.h"
#include "ext/standard/info.h"
#include "Zend/zend_API.h"
#include "Zend/zend_interfaces.h"
#include "Zend/zend_exceptions.h"
}

#include "php_kislayphp_config.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef zend_call_method_with_0_params
static inline void kislay_call_method_with_0_params(
    zend_object *obj,
    zend_class_entry *obj_ce,
    zend_function **fn_proxy,
    const char *function_name,
    zval *retval) {
    zend_call_method(obj, obj_ce, fn_proxy, function_name, std::strlen(function_name), retval, 0, nullptr, nullptr);
}
#define zend_call_method_with_0_params(obj, obj_ce, fn_proxy, function_name, retval) \
    kislay_call_method_with_0_params(obj, obj_ce, fn_proxy, function_name, retval)
#endif

#ifndef zend_call_method_with_1_params
static inline void kislay_call_method_with_1_params(
    zend_object *obj,
    zend_class_entry *obj_ce,
    zend_function **fn_proxy,
    const char *function_name,
    zval *retval,
    zval *param1) {
    zend_call_method(obj, obj_ce, fn_proxy, function_name, std::strlen(function_name), retval, 1, param1, nullptr);
}
#define zend_call_method_with_1_params(obj, obj_ce, fn_proxy, function_name, retval, param1) \
    kislay_call_method_with_1_params(obj, obj_ce, fn_proxy, function_name, retval, param1)
#endif

#ifndef zend_call_method_with_2_params
static inline void kislay_call_method_with_2_params(
    zend_object *obj,
    zend_class_entry *obj_ce,
    zend_function **fn_proxy,
    const char *function_name,
    zval *retval,
    zval *param1,
    zval *param2) {
    zend_call_method(obj, obj_ce, fn_proxy, function_name, std::strlen(function_name), retval, 2, param1, param2);
}
#define zend_call_method_with_2_params(obj, obj_ce, fn_proxy, function_name, retval, param1, param2) \
    kislay_call_method_with_2_params(obj, obj_ce, fn_proxy, function_name, retval, param1, param2)
#endif

using flat_map_t = std::unordered_map<std::string, std::string>;
using scope_map_t = std::unordered_map<std::string, flat_map_t>;
using project_scope_map_t = std::unordered_map<std::string, scope_map_t>;
using node_scope_map_t = std::unordered_map<std::string, project_scope_map_t>;

static zend_class_entry *kislayphp_config_client_interface_ce;
static zend_class_entry *kislayphp_config_client_ce;
static zend_class_entry *kislayphp_config_runtime_ce;
static zend_class_entry *kislayphp_config_server_ce;

static zend_object_handlers kislayphp_config_client_handlers;
static zend_object_handlers kislayphp_config_server_handlers;

static pthread_mutex_t kislay_runtime_lock = PTHREAD_MUTEX_INITIALIZER;
static flat_map_t kislay_runtime_remote_snapshot;
static flat_map_t kislay_runtime_local_overrides;
static flat_map_t kislay_runtime_runtime_overrides;
static flat_map_t kislay_runtime_active_snapshot;
static std::string kislay_runtime_version("0");
static std::string kislay_runtime_checksum("0");
static std::string kislay_runtime_server_url;
static std::string kislay_runtime_environment;
static std::string kislay_runtime_project;
static std::string kislay_runtime_service;
static std::string kislay_runtime_node;
static std::string kislay_runtime_cache_file;
static std::string kislay_runtime_local_file;
static std::string kislay_runtime_env_prefix("KISLAY_CFG_");
static bool kislay_runtime_booted = false;

struct php_kislayphp_config_client_t {
    flat_map_t values;
    pthread_mutex_t lock;
    zval client;
    bool has_client;
    zend_object std;
};

struct php_kislayphp_config_server_t {
    flat_map_t global_scope;
    scope_map_t environment_scopes;
    scope_map_t project_scopes;
    project_scope_map_t service_scopes;
    node_scope_map_t node_scopes;
    std::string host;
    zend_long port;
    int listen_fd;
    bool running;
    std::uint64_t revision;
    std::string version;
    pthread_mutex_t lock;
    zend_object std;
};

struct kislay_http_request_t {
    std::string method;
    std::string uri;
    std::string path;
    std::string query;
    std::string body;
    std::map<std::string, std::string> headers;
};

struct kislay_scoped_pthread_lock_t {
    pthread_mutex_t *lock;

    explicit kislay_scoped_pthread_lock_t(pthread_mutex_t *target) : lock(target) {
        pthread_mutex_lock(lock);
    }

    ~kislay_scoped_pthread_lock_t() {
        pthread_mutex_unlock(lock);
    }
};

struct kislay_http_url_t {
    std::string host;
    int port;
    std::string path;
};

static inline php_kislayphp_config_client_t *php_kislayphp_config_client_from_obj(zend_object *obj) {
    return reinterpret_cast<php_kislayphp_config_client_t *>(
        reinterpret_cast<char *>(obj) - XtOffsetOf(php_kislayphp_config_client_t, std));
}

static inline php_kislayphp_config_server_t *php_kislayphp_config_server_from_obj(zend_object *obj) {
    return reinterpret_cast<php_kislayphp_config_server_t *>(
        reinterpret_cast<char *>(obj) - XtOffsetOf(php_kislayphp_config_server_t, std));
}

static bool kislay_call_function(const char *name, zval *retval, uint32_t param_count, zval params[]) {
    zval function_name;
    ZVAL_STRING(&function_name, name);
    int result = call_user_function(EG(function_table), nullptr, &function_name, retval, param_count, params);
    zval_ptr_dtor(&function_name);
    return result == SUCCESS;
}

static bool kislay_json_encode_zval(zval *value, std::string *out) {
    zval retval;
    zval params[1];
    ZVAL_COPY(&params[0], value);
    ZVAL_UNDEF(&retval);
    bool ok = kislay_call_function("json_encode", &retval, 1, params);
    zval_ptr_dtor(&params[0]);
    if (!ok || Z_TYPE(retval) != IS_STRING) {
        if (!Z_ISUNDEF(retval)) {
            zval_ptr_dtor(&retval);
        }
        return false;
    }
    *out = std::string(Z_STRVAL(retval), Z_STRLEN(retval));
    zval_ptr_dtor(&retval);
    return true;
}

static bool kislay_json_decode_assoc(const std::string &json, zval *return_value) {
    zval retval;
    zval params[2];
    ZVAL_STRINGL(&params[0], json.c_str(), json.size());
    ZVAL_TRUE(&params[1]);
    ZVAL_UNDEF(&retval);
    bool ok = kislay_call_function("json_decode", &retval, 2, params);
    zval_ptr_dtor(&params[0]);
    zval_ptr_dtor(&params[1]);
    if (!ok || Z_ISUNDEF(retval)) {
        if (!Z_ISUNDEF(retval)) {
            zval_ptr_dtor(&retval);
        }
        return false;
    }
    ZVAL_COPY_VALUE(return_value, &retval);
    return true;
}

static std::string kislay_trim(const std::string &value) {
    std::size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t' || value[start] == '\r' || value[start] == '\n')) {
        start++;
    }
    std::size_t end = value.size();
    while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r' || value[end - 1] == '\n')) {
        end--;
    }
    return value.substr(start, end - start);
}

static std::string kislay_to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

static std::string kislay_url_decode(const std::string &value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            char hex[3] = {value[i + 1], value[i + 2], '\0'};
            char *endptr = nullptr;
            long decoded = std::strtol(hex, &endptr, 16);
            if (endptr != nullptr && *endptr == '\0') {
                result.push_back(static_cast<char>(decoded));
                i += 2;
                continue;
            }
        }
        if (value[i] == '+') {
            result.push_back(' ');
        } else {
            result.push_back(value[i]);
        }
    }
    return result;
}

static std::vector<std::string> kislay_split(const std::string &value, char delimiter) {
    std::vector<std::string> parts;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, delimiter)) {
        parts.push_back(item);
    }
    return parts;
}

static std::map<std::string, std::string> kislay_parse_query(const std::string &query) {
    std::map<std::string, std::string> out;
    std::vector<std::string> pairs = kislay_split(query, '&');
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        if (pairs[i].empty()) {
            continue;
        }
        std::size_t eq = pairs[i].find('=');
        if (eq == std::string::npos) {
            out[kislay_url_decode(pairs[i])] = "";
        } else {
            out[kislay_url_decode(pairs[i].substr(0, eq))] = kislay_url_decode(pairs[i].substr(eq + 1));
        }
    }
    return out;
}

static std::string kislay_env_key_to_config_key(const std::string &env_key, const std::string &prefix) {
    if (env_key.size() < prefix.size() || env_key.compare(0, prefix.size(), prefix) != 0) {
        return std::string();
    }
    std::string suffix = env_key.substr(prefix.size());
    std::string result;
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        char ch = suffix[i];
        if (ch == '_' && i + 1 < suffix.size() && suffix[i + 1] == '_') {
            result.push_back('.');
            i++;
            continue;
        }
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return result;
}

static std::string kislay_string_from_zval(zval *value) {
    if (Z_TYPE_P(value) == IS_TRUE) {
        return "true";
    }
    if (Z_TYPE_P(value) == IS_FALSE) {
        return "false";
    }
    if (Z_TYPE_P(value) == IS_NULL) {
        return "null";
    }
    zend_string *string_value = zval_get_string(value);
    std::string out(ZSTR_VAL(string_value), ZSTR_LEN(string_value));
    zend_string_release(string_value);
    return out;
}

static bool kislay_hash_is_list(HashTable *ht) {
    zend_ulong expected = 0;
    zend_string *key = nullptr;
    zend_ulong index = 0;
    zval *value = nullptr;
    ZEND_HASH_FOREACH_KEY_VAL(ht, index, key, value) {
        if (key != nullptr || index != expected) {
            return false;
        }
        expected++;
    } ZEND_HASH_FOREACH_END();
    return true;
}

static void kislay_flatten_zval(zval *value, const std::string &prefix, flat_map_t *out) {
    if (Z_TYPE_P(value) == IS_ARRAY) {
        HashTable *ht = Z_ARRVAL_P(value);
        if (kislay_hash_is_list(ht)) {
            if (!prefix.empty()) {
                std::string encoded;
                if (kislay_json_encode_zval(value, &encoded)) {
                    (*out)[prefix] = encoded;
                }
            }
            return;
        }
        zend_string *key = nullptr;
        zend_ulong index = 0;
        zval *entry = nullptr;
        ZEND_HASH_FOREACH_KEY_VAL(ht, index, key, entry) {
            std::string next = prefix;
            if (key != nullptr) {
                if (!next.empty()) {
                    next.push_back('.');
                }
                next.append(ZSTR_VAL(key), ZSTR_LEN(key));
            } else {
                if (!next.empty()) {
                    next.push_back('.');
                }
                next += std::to_string(index);
            }
            kislay_flatten_zval(entry, next, out);
        } ZEND_HASH_FOREACH_END();
        return;
    }

    if (!prefix.empty()) {
        (*out)[prefix] = kislay_string_from_zval(value);
    }
}

static bool kislay_zval_to_flat_map(zval *value, flat_map_t *out, std::string *error) {
    if (Z_TYPE_P(value) != IS_ARRAY) {
        if (error != nullptr) {
            *error = "Expected associative array";
        }
        return false;
    }
    out->clear();
    kislay_flatten_zval(value, std::string(), out);
    return true;
}

static void kislay_flat_map_to_array(const flat_map_t &values, zval *return_value) {
    array_init(return_value);
    for (flat_map_t::const_iterator it = values.begin(); it != values.end(); ++it) {
        add_assoc_string(return_value, it->first.c_str(), const_cast<char *>(it->second.c_str()));
    }
}

static void kislay_merge_flat_map(flat_map_t *target, const flat_map_t &source) {
    for (flat_map_t::const_iterator it = source.begin(); it != source.end(); ++it) {
        (*target)[it->first] = it->second;
    }
}

static std::string kislay_checksum_for_map(const flat_map_t &values) {
    std::vector<std::string> keys;
    keys.reserve(values.size());
    for (flat_map_t::const_iterator it = values.begin(); it != values.end(); ++it) {
        keys.push_back(it->first);
    }
    std::sort(keys.begin(), keys.end());
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const std::string &key = keys[i];
        const std::string &value = values.find(key)->second;
        const std::string combined = key + "=" + value + "\n";
        for (std::size_t j = 0; j < combined.size(); ++j) {
            hash ^= static_cast<unsigned char>(combined[j]);
            hash *= 1099511628211ULL;
        }
    }
    std::ostringstream oss;
    oss << std::hex << hash;
    return oss.str();
}

static bool kislay_write_text_file(const std::string &path, const std::string &body) {
    std::ofstream out(path.c_str(), std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out << body;
    return out.good();
}

static bool kislay_read_text_file(const std::string &path, std::string *body) {
    std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    std::ostringstream contents;
    contents << in.rdbuf();
    *body = contents.str();
    return true;
}

static bool kislay_parse_http_url(const std::string &url, kislay_http_url_t *parsed) {
    std::string work = url;
    const std::string http_prefix("http://");
    if (work.compare(0, http_prefix.size(), http_prefix) == 0) {
        work = work.substr(http_prefix.size());
    }
    std::size_t slash = work.find('/');
    std::string authority = slash == std::string::npos ? work : work.substr(0, slash);
    parsed->path = slash == std::string::npos ? "/" : work.substr(slash);
    std::size_t colon = authority.rfind(':');
    if (colon == std::string::npos) {
        parsed->host = authority;
        parsed->port = 80;
    } else {
        parsed->host = authority.substr(0, colon);
        parsed->port = std::atoi(authority.substr(colon + 1).c_str());
        if (parsed->port <= 0) {
            parsed->port = 80;
        }
    }
    return !parsed->host.empty();
}

static bool kislay_http_request(const std::string &method, const std::string &url, const std::string &body, int *status_code, std::string *response_body, std::string *error) {
    kislay_http_url_t parsed;
    if (!kislay_parse_http_url(url, &parsed)) {
        if (error != nullptr) {
            *error = "Invalid URL";
        }
        return false;
    }

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result = nullptr;
    std::string port = std::to_string(parsed.port);
    if (getaddrinfo(parsed.host.c_str(), port.c_str(), &hints, &result) != 0) {
        if (error != nullptr) {
            *error = "getaddrinfo failed";
        }
        return false;
    }

    int fd = -1;
    for (struct addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == -1) {
            continue;
        }
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(result);

    if (fd == -1) {
        if (error != nullptr) {
            *error = "connect failed";
        }
        return false;
    }

    std::ostringstream request;
    request << method << " " << parsed.path << " HTTP/1.1\r\n";
    request << "Host: " << parsed.host << "\r\n";
    request << "Connection: close\r\n";
    if (!body.empty()) {
        request << "Content-Type: application/json\r\n";
        request << "Content-Length: " << body.size() << "\r\n";
    }
    request << "\r\n";
    request << body;

    const std::string wire = request.str();
    std::size_t sent = 0;
    while (sent < wire.size()) {
        ssize_t wrote = send(fd, wire.data() + sent, wire.size() - sent, 0);
        if (wrote <= 0) {
            if (error != nullptr) {
                *error = "send failed";
            }
            close(fd);
            return false;
        }
        sent += static_cast<std::size_t>(wrote);
    }

    std::string response;
    char buffer[4096];
    for (;;) {
        ssize_t received = recv(fd, buffer, sizeof(buffer), 0);
        if (received == 0) {
            break;
        }
        if (received < 0) {
            if (error != nullptr) {
                *error = "recv failed";
            }
            close(fd);
            return false;
        }
        response.append(buffer, static_cast<std::size_t>(received));
    }
    close(fd);

    std::size_t header_end = response.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        if (error != nullptr) {
            *error = "Invalid HTTP response";
        }
        return false;
    }

    std::string headers = response.substr(0, header_end);
    if (response_body != nullptr) {
        *response_body = response.substr(header_end + 4);
    }

    std::size_t first_space = headers.find(' ');
    if (first_space == std::string::npos) {
        if (error != nullptr) {
            *error = "Invalid status line";
        }
        return false;
    }
    std::size_t second_space = headers.find(' ', first_space + 1);
    std::string code = headers.substr(first_space + 1, second_space == std::string::npos ? std::string::npos : (second_space - first_space - 1));
    if (status_code != nullptr) {
        *status_code = std::atoi(code.c_str());
    }
    return true;
}

static bool kislay_http_read_request(int fd, kislay_http_request_t *request) {
    std::string incoming;
    char buffer[4096];
    std::size_t header_end = std::string::npos;
    while ((header_end = incoming.find("\r\n\r\n")) == std::string::npos) {
        ssize_t received = recv(fd, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            return false;
        }
        incoming.append(buffer, static_cast<std::size_t>(received));
        if (incoming.size() > 1024 * 1024) {
            return false;
        }
    }

    std::string header_block = incoming.substr(0, header_end);
    std::string body = incoming.substr(header_end + 4);

    std::istringstream stream(header_block);
    std::string request_line;
    if (!std::getline(stream, request_line)) {
        return false;
    }
    if (!request_line.empty() && request_line[request_line.size() - 1] == '\r') {
        request_line.erase(request_line.size() - 1);
    }
    std::istringstream line_stream(request_line);
    line_stream >> request->method >> request->uri;

    std::string header_line;
    std::size_t content_length = 0;
    while (std::getline(stream, header_line)) {
        if (!header_line.empty() && header_line[header_line.size() - 1] == '\r') {
            header_line.erase(header_line.size() - 1);
        }
        std::size_t colon = header_line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        std::string key = kislay_to_lower(kislay_trim(header_line.substr(0, colon)));
        std::string value = kislay_trim(header_line.substr(colon + 1));
        request->headers[key] = value;
        if (key == "content-length") {
            content_length = static_cast<std::size_t>(std::strtoull(value.c_str(), nullptr, 10));
        }
    }

    while (body.size() < content_length) {
        ssize_t received = recv(fd, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            return false;
        }
        body.append(buffer, static_cast<std::size_t>(received));
    }
    request->body = body.substr(0, content_length);

    std::size_t q = request->uri.find('?');
    request->path = q == std::string::npos ? request->uri : request->uri.substr(0, q);
    request->query = q == std::string::npos ? std::string() : request->uri.substr(q + 1);
    return true;
}

static void kislay_http_send_response(int fd, int status_code, const std::string &content_type, const std::string &body) {
    std::ostringstream response;
    const char *status_text = "OK";
    if (status_code == 400) status_text = "Bad Request";
    if (status_code == 404) status_text = "Not Found";
    if (status_code == 405) status_text = "Method Not Allowed";
    if (status_code == 500) status_text = "Internal Server Error";
    response << "HTTP/1.1 " << status_code << ' ' << status_text << "\r\n";
    response << "Content-Type: " << content_type << "\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "Connection: close\r\n\r\n";
    response << body;
    const std::string wire = response.str();
    send(fd, wire.data(), wire.size(), 0);
}

static void kislay_server_bump_version(php_kislayphp_config_server_t *server) {
    server->revision++;
    server->version = std::to_string(static_cast<unsigned long long>(server->revision));
}

static flat_map_t kislay_server_resolve_locked(php_kislayphp_config_server_t *server, const std::string &environment, const std::string &project, const std::string &service, const std::string &node) {
    flat_map_t result;
    kislay_merge_flat_map(&result, server->global_scope);

    scope_map_t::const_iterator env_it = server->environment_scopes.find(environment);
    if (env_it != server->environment_scopes.end()) {
        kislay_merge_flat_map(&result, env_it->second);
    }

    scope_map_t::const_iterator project_it = server->project_scopes.find(project);
    if (project_it != server->project_scopes.end()) {
        kislay_merge_flat_map(&result, project_it->second);
    }

    project_scope_map_t::const_iterator service_project_it = server->service_scopes.find(project);
    if (service_project_it != server->service_scopes.end()) {
        scope_map_t::const_iterator service_it = service_project_it->second.find(service);
        if (service_it != service_project_it->second.end()) {
            kislay_merge_flat_map(&result, service_it->second);
        }
    }

    node_scope_map_t::const_iterator node_project_it = server->node_scopes.find(project);
    if (node_project_it != server->node_scopes.end()) {
        project_scope_map_t::const_iterator node_service_map_it = node_project_it->second.find(service);
        if (node_service_map_it != node_project_it->second.end()) {
            scope_map_t::const_iterator node_it = node_service_map_it->second.find(node);
            if (node_it != node_service_map_it->second.end()) {
                kislay_merge_flat_map(&result, node_it->second);
            }
        }
    }

    return result;
}

static void kislay_runtime_rebuild_locked() {
    kislay_runtime_active_snapshot.clear();
    kislay_merge_flat_map(&kislay_runtime_active_snapshot, kislay_runtime_remote_snapshot);
    kislay_merge_flat_map(&kislay_runtime_active_snapshot, kislay_runtime_local_overrides);
    kislay_merge_flat_map(&kislay_runtime_active_snapshot, kislay_runtime_runtime_overrides);

    extern char **environ;
    if (!kislay_runtime_env_prefix.empty()) {
        for (char **env = environ; env != nullptr && *env != nullptr; ++env) {
            std::string item(*env);
            std::size_t eq = item.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            std::string key = item.substr(0, eq);
            std::string value = item.substr(eq + 1);
            std::string config_key = kislay_env_key_to_config_key(key, kislay_runtime_env_prefix);
            if (!config_key.empty()) {
                kislay_runtime_active_snapshot[config_key] = value;
            }
        }
    }

    kislay_runtime_checksum = kislay_checksum_for_map(kislay_runtime_active_snapshot);
}

static bool kislay_runtime_save_cache_locked() {
    if (kislay_runtime_cache_file.empty()) {
        return true;
    }
    zval root;
    zval config;
    array_init(&root);
    add_assoc_string(&root, "version", const_cast<char *>(kislay_runtime_version.c_str()));
    kislay_flat_map_to_array(kislay_runtime_remote_snapshot, &config);
    add_assoc_zval(&root, "config", &config);
    std::string json;
    bool ok = kislay_json_encode_zval(&root, &json);
    zval_ptr_dtor(&root);
    if (!ok) {
        return false;
    }
    return kislay_write_text_file(kislay_runtime_cache_file, json);
}

static bool kislay_runtime_load_cache_locked(std::string *error) {
    if (kislay_runtime_cache_file.empty()) {
        if (error) {
            *error = "No cache file configured";
        }
        return false;
    }
    std::string body;
    if (!kislay_read_text_file(kislay_runtime_cache_file, &body)) {
        if (error) {
            *error = "Unable to read cache file";
        }
        return false;
    }
    zval decoded;
    ZVAL_UNDEF(&decoded);
    if (!kislay_json_decode_assoc(body, &decoded) || Z_TYPE(decoded) != IS_ARRAY) {
        if (!Z_ISUNDEF(decoded)) {
            zval_ptr_dtor(&decoded);
        }
        if (error) {
            *error = "Invalid cache payload";
        }
        return false;
    }

    zval *version = zend_hash_str_find(Z_ARRVAL(decoded), "version", sizeof("version") - 1);
    zval *config = zend_hash_str_find(Z_ARRVAL(decoded), "config", sizeof("config") - 1);
    if (version != nullptr) {
        kislay_runtime_version = kislay_string_from_zval(version);
    }
    if (config == nullptr || Z_TYPE_P(config) != IS_ARRAY) {
        zval_ptr_dtor(&decoded);
        if (error) {
            *error = "Cache missing config";
        }
        return false;
    }

    kislay_runtime_remote_snapshot.clear();
    HashTable *ht = Z_ARRVAL_P(config);
    zend_string *key = nullptr;
    zend_ulong index = 0;
    zval *entry = nullptr;
    ZEND_HASH_FOREACH_KEY_VAL(ht, index, key, entry) {
        if (key != nullptr) {
            kislay_runtime_remote_snapshot[std::string(ZSTR_VAL(key), ZSTR_LEN(key))] = kislay_string_from_zval(entry);
        }
    } ZEND_HASH_FOREACH_END();

    zval_ptr_dtor(&decoded);
    return true;
}

static bool kislay_runtime_load_local_file_locked(const std::string &path, std::string *error) {
    std::string body;
    if (!kislay_read_text_file(path, &body)) {
        if (error) {
            *error = "Unable to read local config file";
        }
        return false;
    }

    zval decoded;
    ZVAL_UNDEF(&decoded);
    if (!kislay_json_decode_assoc(body, &decoded) || Z_TYPE(decoded) != IS_ARRAY) {
        if (!Z_ISUNDEF(decoded)) {
            zval_ptr_dtor(&decoded);
        }
        if (error) {
            *error = "Invalid local config JSON";
        }
        return false;
    }

    flat_map_t flattened;
    if (!kislay_zval_to_flat_map(&decoded, &flattened, error)) {
        zval_ptr_dtor(&decoded);
        return false;
    }
    kislay_runtime_local_overrides = flattened;
    zval_ptr_dtor(&decoded);
    return true;
}

static bool kislay_runtime_fetch_remote_locked(std::string *error) {
    if (kislay_runtime_server_url.empty()) {
        kislay_runtime_remote_snapshot.clear();
        kislay_runtime_version = "local";
        return true;
    }

    std::ostringstream url;
    url << kislay_runtime_server_url << "/v1/config/resolve?environment=" << kislay_runtime_environment
        << "&project=" << kislay_runtime_project
        << "&service=" << kislay_runtime_service
        << "&node=" << kislay_runtime_node;

    int status = 0;
    std::string body;
    if (!kislay_http_request("GET", url.str(), std::string(), &status, &body, error)) {
        return false;
    }
    if (status != 200) {
        if (error != nullptr) {
            *error = "Remote config request failed with HTTP " + std::to_string(status);
        }
        return false;
    }

    zval decoded;
    ZVAL_UNDEF(&decoded);
    if (!kislay_json_decode_assoc(body, &decoded) || Z_TYPE(decoded) != IS_ARRAY) {
        if (!Z_ISUNDEF(decoded)) {
            zval_ptr_dtor(&decoded);
        }
        if (error != nullptr) {
            *error = "Invalid remote config JSON";
        }
        return false;
    }

    zval *version = zend_hash_str_find(Z_ARRVAL(decoded), "version", sizeof("version") - 1);
    zval *config = zend_hash_str_find(Z_ARRVAL(decoded), "config", sizeof("config") - 1);
    if (version != nullptr) {
        kislay_runtime_version = kislay_string_from_zval(version);
    }
    if (config == nullptr || Z_TYPE_P(config) != IS_ARRAY) {
        zval_ptr_dtor(&decoded);
        if (error != nullptr) {
            *error = "Remote payload missing config";
        }
        return false;
    }

    kislay_runtime_remote_snapshot.clear();
    HashTable *ht = Z_ARRVAL_P(config);
    zend_string *key = nullptr;
    zend_ulong index = 0;
    zval *entry = nullptr;
    ZEND_HASH_FOREACH_KEY_VAL(ht, index, key, entry) {
        if (key != nullptr) {
            kislay_runtime_remote_snapshot[std::string(ZSTR_VAL(key), ZSTR_LEN(key))] = kislay_string_from_zval(entry);
        }
    } ZEND_HASH_FOREACH_END();

    zval_ptr_dtor(&decoded);
    return true;
}

static bool kislay_hash_find_string(HashTable *ht, const char *key, std::string *out) {
    zval *value = zend_hash_str_find(ht, key, std::strlen(key));
    if (value == nullptr || Z_TYPE_P(value) == IS_NULL) {
        return false;
    }
    *out = kislay_string_from_zval(value);
    return true;
}

static zend_object *kislayphp_config_client_create_object(zend_class_entry *ce) {
    php_kislayphp_config_client_t *obj = static_cast<php_kislayphp_config_client_t *>(
        ecalloc(1, sizeof(php_kislayphp_config_client_t) + zend_object_properties_size(ce)));
    zend_object_std_init(&obj->std, ce);
    object_properties_init(&obj->std, ce);
    new (&obj->values) flat_map_t();
    pthread_mutex_init(&obj->lock, nullptr);
    ZVAL_UNDEF(&obj->client);
    obj->has_client = false;
    obj->std.handlers = &kislayphp_config_client_handlers;
    return &obj->std;
}

static void kislayphp_config_client_free_obj(zend_object *object) {
    php_kislayphp_config_client_t *obj = php_kislayphp_config_client_from_obj(object);
    if (obj->has_client) {
        zval_ptr_dtor(&obj->client);
    }
    obj->values.~unordered_map();
    pthread_mutex_destroy(&obj->lock);
    zend_object_std_dtor(&obj->std);
}

static zend_object *kislayphp_config_server_create_object(zend_class_entry *ce) {
    php_kislayphp_config_server_t *obj = static_cast<php_kislayphp_config_server_t *>(
        ecalloc(1, sizeof(php_kislayphp_config_server_t) + zend_object_properties_size(ce)));
    zend_object_std_init(&obj->std, ce);
    object_properties_init(&obj->std, ce);
    new (&obj->global_scope) flat_map_t();
    new (&obj->environment_scopes) scope_map_t();
    new (&obj->project_scopes) scope_map_t();
    new (&obj->service_scopes) project_scope_map_t();
    new (&obj->node_scopes) node_scope_map_t();
    obj->host = "127.0.0.1";
    obj->port = 9011;
    obj->listen_fd = -1;
    obj->running = false;
    obj->revision = 0;
    obj->version = "0";
    pthread_mutex_init(&obj->lock, nullptr);
    obj->std.handlers = &kislayphp_config_server_handlers;
    return &obj->std;
}

static void kislayphp_config_server_free_obj(zend_object *object) {
    php_kislayphp_config_server_t *obj = php_kislayphp_config_server_from_obj(object);
    if (obj->listen_fd >= 0) {
        close(obj->listen_fd);
        obj->listen_fd = -1;
    }
    obj->global_scope.~unordered_map();
    obj->environment_scopes.~unordered_map();
    obj->project_scopes.~unordered_map();
    obj->service_scopes.~unordered_map();
    obj->node_scopes.~unordered_map();
    pthread_mutex_destroy(&obj->lock);
    zend_object_std_dtor(&obj->std);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_config_void, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_config_set, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, key, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, value, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_config_get, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, key, IS_STRING, 0)
    ZEND_ARG_INFO(0, default)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_config_set_client, 0, 0, 1)
    ZEND_ARG_OBJ_INFO(0, client, Kislay\Config\ClientInterface, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_config_has, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, key, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_config_remove, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, key, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_config_get_string, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, key, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, default, IS_STRING, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_config_get_int, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, key, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, default, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_config_get_bool, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, key, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, default, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_config_get_array, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, key, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, default, IS_ARRAY, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_config_boot, 0, 0, 1)
    ZEND_ARG_ARRAY_INFO(0, options, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_config_load_local, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, path, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_config_set_override, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, key, IS_STRING, 0)
    ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_server_construct, 0, 0, 0)
    ZEND_ARG_ARRAY_INFO(0, options, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_server_listen, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, host, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, port, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_server_scope, 0, 0, 1)
    ZEND_ARG_ARRAY_INFO(0, config, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_server_environment_scope, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, environment, IS_STRING, 0)
    ZEND_ARG_ARRAY_INFO(0, config, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_server_project_scope, 0, 0, 2)
    ZEND_ARG_TYPE_INFO(0, project, IS_STRING, 0)
    ZEND_ARG_ARRAY_INFO(0, config, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_server_service_scope, 0, 0, 3)
    ZEND_ARG_TYPE_INFO(0, project, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, service, IS_STRING, 0)
    ZEND_ARG_ARRAY_INFO(0, config, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_server_node_scope, 0, 0, 4)
    ZEND_ARG_TYPE_INFO(0, project, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, service, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, node, IS_STRING, 0)
    ZEND_ARG_ARRAY_INFO(0, config, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_server_resolve, 0, 0, 0)
    ZEND_ARG_TYPE_INFO(0, environment, IS_STRING, 1)
    ZEND_ARG_TYPE_INFO(0, project, IS_STRING, 1)
    ZEND_ARG_TYPE_INFO(0, service, IS_STRING, 1)
    ZEND_ARG_TYPE_INFO(0, node, IS_STRING, 1)
ZEND_END_ARG_INFO()

static zval *kislayphp_config_client_get_value(php_kislayphp_config_client_t *obj, const char *key, size_t key_len, zval *default_val, zval *return_value) {
    if (obj->has_client) {
        zval key_zv;
        ZVAL_STRINGL(&key_zv, key, key_len);
        zval retval;
        ZVAL_UNDEF(&retval);
        if (default_val != nullptr) {
            zend_call_method_with_2_params(Z_OBJ(obj->client), Z_OBJCE(obj->client), nullptr, "get", &retval, &key_zv, default_val);
        } else {
            zend_call_method_with_1_params(Z_OBJ(obj->client), Z_OBJCE(obj->client), nullptr, "get", &retval, &key_zv);
        }
        zval_ptr_dtor(&key_zv);
        if (Z_ISUNDEF(retval)) {
            if (default_val != nullptr) {
                ZVAL_COPY(return_value, default_val);
            } else {
                ZVAL_NULL(return_value);
            }
        } else {
            ZVAL_COPY_VALUE(return_value, &retval);
        }
        return return_value;
    }

    std::string value;
    bool found = false;
    pthread_mutex_lock(&obj->lock);
    flat_map_t::iterator it = obj->values.find(std::string(key, key_len));
    if (it != obj->values.end()) {
        value = it->second;
        found = true;
    }
    pthread_mutex_unlock(&obj->lock);

    if (!found) {
        if (default_val != nullptr) {
            ZVAL_COPY(return_value, default_val);
        } else {
            ZVAL_NULL(return_value);
        }
        return return_value;
    }
    ZVAL_STRING(return_value, value.c_str());
    return return_value;
}

PHP_METHOD(KislayPHPConfigClient, __construct) {
    ZEND_PARSE_PARAMETERS_NONE();
}

PHP_METHOD(KislayPHPConfigClient, setClient) {
    zval *client = nullptr;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ZVAL(client)
    ZEND_PARSE_PARAMETERS_END();

    if (client == nullptr || Z_TYPE_P(client) != IS_OBJECT || !instanceof_function(Z_OBJCE_P(client), kislayphp_config_client_interface_ce)) {
        zend_throw_exception(zend_ce_exception, "Client must implement Kislay\\Config\\ClientInterface", 0);
        RETURN_FALSE;
    }

    php_kislayphp_config_client_t *obj = php_kislayphp_config_client_from_obj(Z_OBJ_P(getThis()));
    if (obj->has_client) {
        zval_ptr_dtor(&obj->client);
    }
    ZVAL_COPY(&obj->client, client);
    obj->has_client = true;
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPConfigClient, set) {
    char *key = nullptr;
    size_t key_len = 0;
    char *value = nullptr;
    size_t value_len = 0;
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(key, key_len)
        Z_PARAM_STRING(value, value_len)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_config_client_t *obj = php_kislayphp_config_client_from_obj(Z_OBJ_P(getThis()));
    if (obj->has_client) {
        zval key_zv;
        zval value_zv;
        zval retval;
        ZVAL_STRINGL(&key_zv, key, key_len);
        ZVAL_STRINGL(&value_zv, value, value_len);
        ZVAL_UNDEF(&retval);
        zend_call_method_with_2_params(Z_OBJ(obj->client), Z_OBJCE(obj->client), nullptr, "set", &retval, &key_zv, &value_zv);
        zval_ptr_dtor(&key_zv);
        zval_ptr_dtor(&value_zv);
        if (Z_ISUNDEF(retval)) {
            RETURN_TRUE;
        }
        RETVAL_ZVAL(&retval, 1, 1);
        return;
    }

    pthread_mutex_lock(&obj->lock);
    obj->values[std::string(key, key_len)] = std::string(value, value_len);
    pthread_mutex_unlock(&obj->lock);
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPConfigClient, get) {
    char *key = nullptr;
    size_t key_len = 0;
    zval *default_val = nullptr;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(key, key_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_ZVAL(default_val)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_config_client_t *obj = php_kislayphp_config_client_from_obj(Z_OBJ_P(getThis()));
    zval out;
    ZVAL_UNDEF(&out);
    kislayphp_config_client_get_value(obj, key, key_len, default_val, &out);
    RETVAL_ZVAL(&out, 1, 1);
}

PHP_METHOD(KislayPHPConfigClient, getString) {
    char *key = nullptr;
    size_t key_len = 0;
    zend_string *default_val = nullptr;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(key, key_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_STR_OR_NULL(default_val)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_config_client_t *obj = php_kislayphp_config_client_from_obj(Z_OBJ_P(getThis()));
    zval fallback;
    zval out;
    ZVAL_UNDEF(&fallback);
    ZVAL_UNDEF(&out);
    zval *fallback_ptr = nullptr;
    if (default_val != nullptr) {
        ZVAL_STR_COPY(&fallback, default_val);
        fallback_ptr = &fallback;
    }
    kislayphp_config_client_get_value(obj, key, key_len, fallback_ptr, &out);
    if (Z_TYPE(out) == IS_NULL) {
        RETVAL_NULL();
    } else {
        zend_string *string_value = zval_get_string(&out);
        RETVAL_STR(string_value);
    }
    if (!Z_ISUNDEF(out)) {
        zval_ptr_dtor(&out);
    }
    if (!Z_ISUNDEF(fallback)) {
        zval_ptr_dtor(&fallback);
    }
}

PHP_METHOD(KislayPHPConfigClient, getInt) {
    char *key = nullptr;
    size_t key_len = 0;
    zend_long default_val = 0;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(key, key_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(default_val)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_config_client_t *obj = php_kislayphp_config_client_from_obj(Z_OBJ_P(getThis()));
    zval fallback;
    zval out;
    ZVAL_LONG(&fallback, default_val);
    ZVAL_UNDEF(&out);
    kislayphp_config_client_get_value(obj, key, key_len, &fallback, &out);
    convert_to_long(&out);
    RETVAL_LONG(Z_LVAL(out));
    zval_ptr_dtor(&out);
}

PHP_METHOD(KislayPHPConfigClient, getBool) {
    char *key = nullptr;
    size_t key_len = 0;
    zend_bool default_val = 0;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(key, key_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_BOOL(default_val)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_config_client_t *obj = php_kislayphp_config_client_from_obj(Z_OBJ_P(getThis()));
    zval fallback;
    zval out;
    ZVAL_BOOL(&fallback, default_val);
    ZVAL_UNDEF(&out);
    kislayphp_config_client_get_value(obj, key, key_len, &fallback, &out);
    if (Z_TYPE(out) == IS_STRING) {
        std::string lower = kislay_to_lower(std::string(Z_STRVAL(out), Z_STRLEN(out)));
        RETVAL_BOOL(lower == "1" || lower == "true" || lower == "yes" || lower == "on");
    } else {
        convert_to_boolean(&out);
        RETVAL_BOOL(Z_TYPE(out) == IS_TRUE);
    }
    zval_ptr_dtor(&out);
}

PHP_METHOD(KislayPHPConfigClient, getArray) {
    char *key = nullptr;
    size_t key_len = 0;
    zval *default_val = nullptr;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(key, key_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_ARRAY_EX(default_val, 1, 0)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_config_client_t *obj = php_kislayphp_config_client_from_obj(Z_OBJ_P(getThis()));
    zval out;
    ZVAL_UNDEF(&out);
    kislayphp_config_client_get_value(obj, key, key_len, default_val, &out);
    if (Z_TYPE(out) == IS_ARRAY) {
        RETVAL_ZVAL(&out, 1, 1);
        zval_ptr_dtor(&out);
        return;
    }
    if (Z_TYPE(out) == IS_STRING) {
        zval decoded;
        ZVAL_UNDEF(&decoded);
        if (kislay_json_decode_assoc(std::string(Z_STRVAL(out), Z_STRLEN(out)), &decoded) && Z_TYPE(decoded) == IS_ARRAY) {
            RETVAL_ZVAL(&decoded, 1, 1);
            zval_ptr_dtor(&out);
            return;
        }
        if (!Z_ISUNDEF(decoded)) {
            zval_ptr_dtor(&decoded);
        }
    }
    if (default_val != nullptr) {
        RETVAL_ZVAL(default_val, 1, 0);
    } else {
        array_init(return_value);
    }
    zval_ptr_dtor(&out);
}

PHP_METHOD(KislayPHPConfigClient, all) {
    php_kislayphp_config_client_t *obj = php_kislayphp_config_client_from_obj(Z_OBJ_P(getThis()));
    if (obj->has_client) {
        zval retval;
        ZVAL_UNDEF(&retval);
        zend_call_method_with_0_params(Z_OBJ(obj->client), Z_OBJCE(obj->client), nullptr, "all", &retval);
        if (Z_ISUNDEF(retval)) {
            array_init(return_value);
            return;
        }
        RETVAL_ZVAL(&retval, 1, 1);
        return;
    }
    array_init(return_value);
    pthread_mutex_lock(&obj->lock);
    for (flat_map_t::const_iterator it = obj->values.begin(); it != obj->values.end(); ++it) {
        add_assoc_string(return_value, it->first.c_str(), const_cast<char *>(it->second.c_str()));
    }
    pthread_mutex_unlock(&obj->lock);
}

PHP_METHOD(KislayPHPConfigClient, has) {
    char *key = nullptr;
    size_t key_len = 0;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(key, key_len)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_config_client_t *obj = php_kislayphp_config_client_from_obj(Z_OBJ_P(getThis()));
    if (obj->has_client) {
        zval key_zv;
        zval retval;
        ZVAL_STRINGL(&key_zv, key, key_len);
        ZVAL_UNDEF(&retval);
        zend_call_method_with_1_params(Z_OBJ(obj->client), Z_OBJCE(obj->client), nullptr, "get", &retval, &key_zv);
        zval_ptr_dtor(&key_zv);
        if (Z_ISUNDEF(retval) || Z_TYPE(retval) == IS_NULL) {
            if (!Z_ISUNDEF(retval)) {
                zval_ptr_dtor(&retval);
            }
            RETURN_FALSE;
        }
        zval_ptr_dtor(&retval);
        RETURN_TRUE;
    }

    pthread_mutex_lock(&obj->lock);
    bool found = obj->values.find(std::string(key, key_len)) != obj->values.end();
    pthread_mutex_unlock(&obj->lock);
    RETURN_BOOL(found);
}

PHP_METHOD(KislayPHPConfigClient, remove) {
    char *key = nullptr;
    size_t key_len = 0;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(key, key_len)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_config_client_t *obj = php_kislayphp_config_client_from_obj(Z_OBJ_P(getThis()));
    if (obj->has_client) {
        RETURN_FALSE;
    }
    pthread_mutex_lock(&obj->lock);
    flat_map_t::iterator it = obj->values.find(std::string(key, key_len));
    bool removed = it != obj->values.end();
    if (removed) {
        obj->values.erase(it);
    }
    pthread_mutex_unlock(&obj->lock);
    RETURN_BOOL(removed);
}

PHP_METHOD(KislayPHPConfigClient, delete) {
    ZEND_MN(KislayPHPConfigClient_remove)(INTERNAL_FUNCTION_PARAM_PASSTHRU);
}

PHP_METHOD(KislayPHPConfigClient, refresh) {
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPConfigRuntime, boot) {
    zval *options = nullptr;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ARRAY(options)
    ZEND_PARSE_PARAMETERS_END();

    kislay_scoped_pthread_lock_t guard(&kislay_runtime_lock);
    HashTable *ht = Z_ARRVAL_P(options);
    kislay_hash_find_string(ht, "server", &kislay_runtime_server_url);
    kislay_hash_find_string(ht, "environment", &kislay_runtime_environment);
    kislay_hash_find_string(ht, "project", &kislay_runtime_project);
    kislay_hash_find_string(ht, "service", &kislay_runtime_service);
    kislay_hash_find_string(ht, "node", &kislay_runtime_node);
    kislay_hash_find_string(ht, "cache_file", &kislay_runtime_cache_file);
    kislay_hash_find_string(ht, "local_file", &kislay_runtime_local_file);
    kislay_hash_find_string(ht, "env_prefix", &kislay_runtime_env_prefix);

    std::string error;
    std::string remote_error;
    bool fetched = kislay_runtime_fetch_remote_locked(&remote_error);
    if (!fetched && !kislay_runtime_cache_file.empty()) {
        std::string cache_error;
        fetched = kislay_runtime_load_cache_locked(&cache_error);
        if (!fetched) {
            if (!remote_error.empty()) {
                error = remote_error + "; cache fallback failed: " + cache_error;
            } else {
                error = cache_error;
            }
        }
    } else if (!fetched) {
        error = remote_error;
    }
    if (!fetched && !kislay_runtime_server_url.empty()) {
        if (error.empty()) {
            error = "Unable to bootstrap config runtime";
        }
        zend_throw_exception(zend_ce_exception, error.c_str(), 0);
        RETURN_FALSE;
    }
    if (!kislay_runtime_local_file.empty() && !kislay_runtime_load_local_file_locked(kislay_runtime_local_file, &error)) {
        zend_throw_exception(zend_ce_exception, error.c_str(), 0);
        RETURN_FALSE;
    }
    kislay_runtime_rebuild_locked();
    kislay_runtime_save_cache_locked();
    kislay_runtime_booted = true;
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPConfigRuntime, loadLocal) {
    char *path = nullptr;
    size_t path_len = 0;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(path, path_len)
    ZEND_PARSE_PARAMETERS_END();

    kislay_scoped_pthread_lock_t guard(&kislay_runtime_lock);
    std::string error;
    if (!kislay_runtime_load_local_file_locked(std::string(path, path_len), &error)) {
        zend_throw_exception(zend_ce_exception, error.c_str(), 0);
        RETURN_FALSE;
    }
    kislay_runtime_rebuild_locked();
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPConfigRuntime, refresh) {
    kislay_scoped_pthread_lock_t guard(&kislay_runtime_lock);
    std::string error;
    if (!kislay_runtime_fetch_remote_locked(&error)) {
        RETURN_FALSE;
    }
    if (!kislay_runtime_local_file.empty()) {
        kislay_runtime_load_local_file_locked(kislay_runtime_local_file, &error);
    }
    kislay_runtime_rebuild_locked();
    kislay_runtime_save_cache_locked();
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPConfigRuntime, setOverride) {
    char *key = nullptr;
    size_t key_len = 0;
    zval *value = nullptr;
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(key, key_len)
        Z_PARAM_ZVAL(value)
    ZEND_PARSE_PARAMETERS_END();

    kislay_scoped_pthread_lock_t guard(&kislay_runtime_lock);
    kislay_runtime_runtime_overrides[std::string(key, key_len)] = kislay_string_from_zval(value);
    kislay_runtime_rebuild_locked();
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPConfigRuntime, has) {
    char *key = nullptr;
    size_t key_len = 0;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(key, key_len)
    ZEND_PARSE_PARAMETERS_END();

    kislay_scoped_pthread_lock_t guard(&kislay_runtime_lock);
    RETURN_BOOL(kislay_runtime_active_snapshot.find(std::string(key, key_len)) != kislay_runtime_active_snapshot.end());
}

PHP_METHOD(KislayPHPConfigRuntime, get) {
    char *key = nullptr;
    size_t key_len = 0;
    zval *default_val = nullptr;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(key, key_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_ZVAL(default_val)
    ZEND_PARSE_PARAMETERS_END();

    kislay_scoped_pthread_lock_t guard(&kislay_runtime_lock);
    flat_map_t::iterator it = kislay_runtime_active_snapshot.find(std::string(key, key_len));
    if (it == kislay_runtime_active_snapshot.end()) {
        if (default_val != nullptr) {
            RETURN_ZVAL(default_val, 1, 0);
        }
        RETURN_NULL();
    }
    RETURN_STRING(it->second.c_str());
}

PHP_METHOD(KislayPHPConfigRuntime, getString) {
    char *key = nullptr;
    size_t key_len = 0;
    zend_string *default_val = nullptr;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(key, key_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_STR_OR_NULL(default_val)
    ZEND_PARSE_PARAMETERS_END();

    kislay_scoped_pthread_lock_t guard(&kislay_runtime_lock);
    flat_map_t::iterator it = kislay_runtime_active_snapshot.find(std::string(key, key_len));
    if (it == kislay_runtime_active_snapshot.end()) {
        if (default_val != nullptr) {
            RETURN_STR_COPY(default_val);
        }
        RETURN_NULL();
    }
    RETURN_STRING(it->second.c_str());
}

PHP_METHOD(KislayPHPConfigRuntime, getInt) {
    char *key = nullptr;
    size_t key_len = 0;
    zend_long default_val = 0;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(key, key_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_LONG(default_val)
    ZEND_PARSE_PARAMETERS_END();

    kislay_scoped_pthread_lock_t guard(&kislay_runtime_lock);
    flat_map_t::iterator it = kislay_runtime_active_snapshot.find(std::string(key, key_len));
    if (it == kislay_runtime_active_snapshot.end()) {
        RETURN_LONG(default_val);
    }
    RETURN_LONG(static_cast<zend_long>(std::strtoll(it->second.c_str(), nullptr, 10)));
}

PHP_METHOD(KislayPHPConfigRuntime, getBool) {
    char *key = nullptr;
    size_t key_len = 0;
    zend_bool default_val = 0;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(key, key_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_BOOL(default_val)
    ZEND_PARSE_PARAMETERS_END();

    kislay_scoped_pthread_lock_t guard(&kislay_runtime_lock);
    flat_map_t::iterator it = kislay_runtime_active_snapshot.find(std::string(key, key_len));
    if (it == kislay_runtime_active_snapshot.end()) {
        RETURN_BOOL(default_val);
    }
    std::string lower = kislay_to_lower(it->second);
    RETURN_BOOL(lower == "1" || lower == "true" || lower == "yes" || lower == "on");
}

PHP_METHOD(KislayPHPConfigRuntime, getArray) {
    char *key = nullptr;
    size_t key_len = 0;
    zval *default_val = nullptr;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(key, key_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_ARRAY_EX(default_val, 1, 0)
    ZEND_PARSE_PARAMETERS_END();

    kislay_scoped_pthread_lock_t guard(&kislay_runtime_lock);
    flat_map_t::iterator it = kislay_runtime_active_snapshot.find(std::string(key, key_len));
    if (it == kislay_runtime_active_snapshot.end()) {
        if (default_val != nullptr) {
            RETURN_ZVAL(default_val, 1, 0);
        }
        array_init(return_value);
        return;
    }
    zval decoded;
    ZVAL_UNDEF(&decoded);
    if (kislay_json_decode_assoc(it->second, &decoded) && Z_TYPE(decoded) == IS_ARRAY) {
        RETVAL_ZVAL(&decoded, 1, 1);
        return;
    }
    if (!Z_ISUNDEF(decoded)) {
        zval_ptr_dtor(&decoded);
    }
    if (default_val != nullptr) {
        RETURN_ZVAL(default_val, 1, 0);
    }
    array_init(return_value);
}

PHP_METHOD(KislayPHPConfigRuntime, all) {
    kislay_scoped_pthread_lock_t guard(&kislay_runtime_lock);
    kislay_flat_map_to_array(kislay_runtime_active_snapshot, return_value);
}

PHP_METHOD(KislayPHPConfigRuntime, version) {
    kislay_scoped_pthread_lock_t guard(&kislay_runtime_lock);
    RETURN_STRING(kislay_runtime_version.c_str());
}

PHP_METHOD(KislayPHPConfigRuntime, checksum) {
    kislay_scoped_pthread_lock_t guard(&kislay_runtime_lock);
    RETURN_STRING(kislay_runtime_checksum.c_str());
}

PHP_METHOD(KislayPHPConfigServer, __construct) {
    zval *options = nullptr;
    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_ARRAY_EX(options, 1, 0)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_config_server_t *obj = php_kislayphp_config_server_from_obj(Z_OBJ_P(getThis()));
    if (options != nullptr) {
        std::string host;
        std::string port;
        if (kislay_hash_find_string(Z_ARRVAL_P(options), "host", &host)) {
            obj->host = host;
        }
        if (kislay_hash_find_string(Z_ARRVAL_P(options), "port", &port)) {
            obj->port = std::strtol(port.c_str(), nullptr, 10);
        }
    }
}

PHP_METHOD(KislayPHPConfigServer, listen) {
    char *host = nullptr;
    size_t host_len = 0;
    zend_long port = 0;
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(host, host_len)
        Z_PARAM_LONG(port)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_config_server_t *obj = php_kislayphp_config_server_from_obj(Z_OBJ_P(getThis()));
    obj->host.assign(host, host_len);
    obj->port = port;
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPConfigServer, setGlobal) {
    zval *config = nullptr;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ARRAY(config)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_config_server_t *obj = php_kislayphp_config_server_from_obj(Z_OBJ_P(getThis()));
    flat_map_t flattened;
    std::string error;
    if (!kislay_zval_to_flat_map(config, &flattened, &error)) {
        zend_throw_exception(zend_ce_exception, error.c_str(), 0);
        RETURN_FALSE;
    }
    pthread_mutex_lock(&obj->lock);
    obj->global_scope = flattened;
    kislay_server_bump_version(obj);
    pthread_mutex_unlock(&obj->lock);
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPConfigServer, setEnvironment) {
    char *environment = nullptr;
    size_t environment_len = 0;
    zval *config = nullptr;
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(environment, environment_len)
        Z_PARAM_ARRAY(config)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_config_server_t *obj = php_kislayphp_config_server_from_obj(Z_OBJ_P(getThis()));
    flat_map_t flattened;
    std::string error;
    if (!kislay_zval_to_flat_map(config, &flattened, &error)) {
        zend_throw_exception(zend_ce_exception, error.c_str(), 0);
        RETURN_FALSE;
    }
    pthread_mutex_lock(&obj->lock);
    obj->environment_scopes[std::string(environment, environment_len)] = flattened;
    kislay_server_bump_version(obj);
    pthread_mutex_unlock(&obj->lock);
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPConfigServer, setProject) {
    char *project = nullptr;
    size_t project_len = 0;
    zval *config = nullptr;
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(project, project_len)
        Z_PARAM_ARRAY(config)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_config_server_t *obj = php_kislayphp_config_server_from_obj(Z_OBJ_P(getThis()));
    flat_map_t flattened;
    std::string error;
    if (!kislay_zval_to_flat_map(config, &flattened, &error)) {
        zend_throw_exception(zend_ce_exception, error.c_str(), 0);
        RETURN_FALSE;
    }
    pthread_mutex_lock(&obj->lock);
    obj->project_scopes[std::string(project, project_len)] = flattened;
    kislay_server_bump_version(obj);
    pthread_mutex_unlock(&obj->lock);
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPConfigServer, setService) {
    char *project = nullptr;
    size_t project_len = 0;
    char *service = nullptr;
    size_t service_len = 0;
    zval *config = nullptr;
    ZEND_PARSE_PARAMETERS_START(3, 3)
        Z_PARAM_STRING(project, project_len)
        Z_PARAM_STRING(service, service_len)
        Z_PARAM_ARRAY(config)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_config_server_t *obj = php_kislayphp_config_server_from_obj(Z_OBJ_P(getThis()));
    flat_map_t flattened;
    std::string error;
    if (!kislay_zval_to_flat_map(config, &flattened, &error)) {
        zend_throw_exception(zend_ce_exception, error.c_str(), 0);
        RETURN_FALSE;
    }
    pthread_mutex_lock(&obj->lock);
    obj->service_scopes[std::string(project, project_len)][std::string(service, service_len)] = flattened;
    kislay_server_bump_version(obj);
    pthread_mutex_unlock(&obj->lock);
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPConfigServer, setNode) {
    char *project = nullptr;
    size_t project_len = 0;
    char *service = nullptr;
    size_t service_len = 0;
    char *node = nullptr;
    size_t node_len = 0;
    zval *config = nullptr;
    ZEND_PARSE_PARAMETERS_START(4, 4)
        Z_PARAM_STRING(project, project_len)
        Z_PARAM_STRING(service, service_len)
        Z_PARAM_STRING(node, node_len)
        Z_PARAM_ARRAY(config)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_config_server_t *obj = php_kislayphp_config_server_from_obj(Z_OBJ_P(getThis()));
    flat_map_t flattened;
    std::string error;
    if (!kislay_zval_to_flat_map(config, &flattened, &error)) {
        zend_throw_exception(zend_ce_exception, error.c_str(), 0);
        RETURN_FALSE;
    }
    pthread_mutex_lock(&obj->lock);
    obj->node_scopes[std::string(project, project_len)][std::string(service, service_len)][std::string(node, node_len)] = flattened;
    kislay_server_bump_version(obj);
    pthread_mutex_unlock(&obj->lock);
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPConfigServer, resolve) {
    zend_string *environment = nullptr;
    zend_string *project = nullptr;
    zend_string *service = nullptr;
    zend_string *node = nullptr;
    ZEND_PARSE_PARAMETERS_START(0, 4)
        Z_PARAM_OPTIONAL
        Z_PARAM_STR_OR_NULL(environment)
        Z_PARAM_STR_OR_NULL(project)
        Z_PARAM_STR_OR_NULL(service)
        Z_PARAM_STR_OR_NULL(node)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_config_server_t *obj = php_kislayphp_config_server_from_obj(Z_OBJ_P(getThis()));
    pthread_mutex_lock(&obj->lock);
    flat_map_t resolved = kislay_server_resolve_locked(
        obj,
        environment != nullptr ? std::string(ZSTR_VAL(environment), ZSTR_LEN(environment)) : std::string(),
        project != nullptr ? std::string(ZSTR_VAL(project), ZSTR_LEN(project)) : std::string(),
        service != nullptr ? std::string(ZSTR_VAL(service), ZSTR_LEN(service)) : std::string(),
        node != nullptr ? std::string(ZSTR_VAL(node), ZSTR_LEN(node)) : std::string());
    pthread_mutex_unlock(&obj->lock);
    kislay_flat_map_to_array(resolved, return_value);
}

PHP_METHOD(KislayPHPConfigServer, version) {
    php_kislayphp_config_server_t *obj = php_kislayphp_config_server_from_obj(Z_OBJ_P(getThis()));
    pthread_mutex_lock(&obj->lock);
    std::string version = obj->version;
    pthread_mutex_unlock(&obj->lock);
    RETURN_STRING(version.c_str());
}

PHP_METHOD(KislayPHPConfigServer, save) {
    char *path = nullptr;
    size_t path_len = 0;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(path, path_len)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_config_server_t *obj = php_kislayphp_config_server_from_obj(Z_OBJ_P(getThis()));
    zval root;
    array_init(&root);

    pthread_mutex_lock(&obj->lock);
    add_assoc_string(&root, "version", const_cast<char *>(obj->version.c_str()));
    add_assoc_long(&root, "revision", static_cast<zend_long>(obj->revision));

    zval global;
    kislay_flat_map_to_array(obj->global_scope, &global);
    add_assoc_zval(&root, "global", &global);

    zval environments;
    array_init(&environments);
    for (scope_map_t::const_iterator it = obj->environment_scopes.begin(); it != obj->environment_scopes.end(); ++it) {
        zval scope;
        kislay_flat_map_to_array(it->second, &scope);
        add_assoc_zval(&environments, it->first.c_str(), &scope);
    }
    add_assoc_zval(&root, "environments", &environments);

    zval projects;
    array_init(&projects);
    for (scope_map_t::const_iterator it = obj->project_scopes.begin(); it != obj->project_scopes.end(); ++it) {
        zval scope;
        kislay_flat_map_to_array(it->second, &scope);
        add_assoc_zval(&projects, it->first.c_str(), &scope);
    }
    add_assoc_zval(&root, "projects", &projects);

    zval services;
    array_init(&services);
    for (project_scope_map_t::const_iterator pit = obj->service_scopes.begin(); pit != obj->service_scopes.end(); ++pit) {
        zval service_map;
        array_init(&service_map);
        for (scope_map_t::const_iterator sit = pit->second.begin(); sit != pit->second.end(); ++sit) {
            zval scope;
            kislay_flat_map_to_array(sit->second, &scope);
            add_assoc_zval(&service_map, sit->first.c_str(), &scope);
        }
        add_assoc_zval(&services, pit->first.c_str(), &service_map);
    }
    add_assoc_zval(&root, "services", &services);

    zval nodes;
    array_init(&nodes);
    for (node_scope_map_t::const_iterator pit = obj->node_scopes.begin(); pit != obj->node_scopes.end(); ++pit) {
        zval project_map;
        array_init(&project_map);
        for (project_scope_map_t::const_iterator sit = pit->second.begin(); sit != pit->second.end(); ++sit) {
            zval service_map;
            array_init(&service_map);
            for (scope_map_t::const_iterator nit = sit->second.begin(); nit != sit->second.end(); ++nit) {
                zval scope;
                kislay_flat_map_to_array(nit->second, &scope);
                add_assoc_zval(&service_map, nit->first.c_str(), &scope);
            }
            add_assoc_zval(&project_map, sit->first.c_str(), &service_map);
        }
        add_assoc_zval(&nodes, pit->first.c_str(), &project_map);
    }
    add_assoc_zval(&root, "nodes", &nodes);
    pthread_mutex_unlock(&obj->lock);

    std::string json;
    bool ok = kislay_json_encode_zval(&root, &json);
    zval_ptr_dtor(&root);
    if (!ok) {
        RETURN_FALSE;
    }
    RETURN_BOOL(kislay_write_text_file(std::string(path, path_len), json));
}

PHP_METHOD(KislayPHPConfigServer, load) {
    char *path = nullptr;
    size_t path_len = 0;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(path, path_len)
    ZEND_PARSE_PARAMETERS_END();

    std::string body;
    if (!kislay_read_text_file(std::string(path, path_len), &body)) {
        RETURN_FALSE;
    }

    zval decoded;
    ZVAL_UNDEF(&decoded);
    if (!kislay_json_decode_assoc(body, &decoded) || Z_TYPE(decoded) != IS_ARRAY) {
        if (!Z_ISUNDEF(decoded)) {
            zval_ptr_dtor(&decoded);
        }
        RETURN_FALSE;
    }

    php_kislayphp_config_server_t *obj = php_kislayphp_config_server_from_obj(Z_OBJ_P(getThis()));
    pthread_mutex_lock(&obj->lock);
    obj->global_scope.clear();
    obj->environment_scopes.clear();
    obj->project_scopes.clear();
    obj->service_scopes.clear();
    obj->node_scopes.clear();

    zval *version = zend_hash_str_find(Z_ARRVAL(decoded), "version", sizeof("version") - 1);
    zval *revision = zend_hash_str_find(Z_ARRVAL(decoded), "revision", sizeof("revision") - 1);
    if (version != nullptr) {
        obj->version = kislay_string_from_zval(version);
    }
    if (revision != nullptr) {
        obj->revision = static_cast<std::uint64_t>(zval_get_long(revision));
    }

    zval *global = zend_hash_str_find(Z_ARRVAL(decoded), "global", sizeof("global") - 1);
    if (global != nullptr && Z_TYPE_P(global) == IS_ARRAY) {
        HashTable *ht = Z_ARRVAL_P(global);
        zend_string *key = nullptr;
        zend_ulong index = 0;
        zval *entry = nullptr;
        ZEND_HASH_FOREACH_KEY_VAL(ht, index, key, entry) {
            if (key != nullptr) {
                obj->global_scope[std::string(ZSTR_VAL(key), ZSTR_LEN(key))] = kislay_string_from_zval(entry);
            }
        } ZEND_HASH_FOREACH_END();
    }

    zval *environments = zend_hash_str_find(Z_ARRVAL(decoded), "environments", sizeof("environments") - 1);
    if (environments != nullptr && Z_TYPE_P(environments) == IS_ARRAY) {
        zend_string *scope_name = nullptr;
        zend_ulong idx = 0;
        zval *scope_value = nullptr;
        ZEND_HASH_FOREACH_KEY_VAL(Z_ARRVAL_P(environments), idx, scope_name, scope_value) {
            if (scope_name == nullptr || Z_TYPE_P(scope_value) != IS_ARRAY) {
                continue;
            }
            HashTable *scope_ht = Z_ARRVAL_P(scope_value);
            zend_string *key = nullptr;
            zend_ulong inner_idx = 0;
            zval *entry = nullptr;
            flat_map_t map;
            ZEND_HASH_FOREACH_KEY_VAL(scope_ht, inner_idx, key, entry) {
                if (key != nullptr) {
                    map[std::string(ZSTR_VAL(key), ZSTR_LEN(key))] = kislay_string_from_zval(entry);
                }
            } ZEND_HASH_FOREACH_END();
            obj->environment_scopes[std::string(ZSTR_VAL(scope_name), ZSTR_LEN(scope_name))] = map;
        } ZEND_HASH_FOREACH_END();
    }

    zval *projects = zend_hash_str_find(Z_ARRVAL(decoded), "projects", sizeof("projects") - 1);
    if (projects != nullptr && Z_TYPE_P(projects) == IS_ARRAY) {
        zend_string *scope_name = nullptr;
        zend_ulong idx = 0;
        zval *scope_value = nullptr;
        ZEND_HASH_FOREACH_KEY_VAL(Z_ARRVAL_P(projects), idx, scope_name, scope_value) {
            if (scope_name == nullptr || Z_TYPE_P(scope_value) != IS_ARRAY) {
                continue;
            }
            HashTable *scope_ht = Z_ARRVAL_P(scope_value);
            zend_string *key = nullptr;
            zend_ulong inner_idx = 0;
            zval *entry = nullptr;
            flat_map_t map;
            ZEND_HASH_FOREACH_KEY_VAL(scope_ht, inner_idx, key, entry) {
                if (key != nullptr) {
                    map[std::string(ZSTR_VAL(key), ZSTR_LEN(key))] = kislay_string_from_zval(entry);
                }
            } ZEND_HASH_FOREACH_END();
            obj->project_scopes[std::string(ZSTR_VAL(scope_name), ZSTR_LEN(scope_name))] = map;
        } ZEND_HASH_FOREACH_END();
    }

    zval *services = zend_hash_str_find(Z_ARRVAL(decoded), "services", sizeof("services") - 1);
    if (services != nullptr && Z_TYPE_P(services) == IS_ARRAY) {
        zend_string *project_name = nullptr;
        zend_ulong idx = 0;
        zval *service_map = nullptr;
        ZEND_HASH_FOREACH_KEY_VAL(Z_ARRVAL_P(services), idx, project_name, service_map) {
            if (project_name == nullptr || Z_TYPE_P(service_map) != IS_ARRAY) {
                continue;
            }
            zend_string *service_name = nullptr;
            zend_ulong inner_idx = 0;
            zval *scope_value = nullptr;
            ZEND_HASH_FOREACH_KEY_VAL(Z_ARRVAL_P(service_map), inner_idx, service_name, scope_value) {
                if (service_name == nullptr || Z_TYPE_P(scope_value) != IS_ARRAY) {
                    continue;
                }
                flat_map_t map;
                zend_string *key = nullptr;
                zend_ulong key_idx = 0;
                zval *entry = nullptr;
                ZEND_HASH_FOREACH_KEY_VAL(Z_ARRVAL_P(scope_value), key_idx, key, entry) {
                    if (key != nullptr) {
                        map[std::string(ZSTR_VAL(key), ZSTR_LEN(key))] = kislay_string_from_zval(entry);
                    }
                } ZEND_HASH_FOREACH_END();
                obj->service_scopes[std::string(ZSTR_VAL(project_name), ZSTR_LEN(project_name))][std::string(ZSTR_VAL(service_name), ZSTR_LEN(service_name))] = map;
            } ZEND_HASH_FOREACH_END();
        } ZEND_HASH_FOREACH_END();
    }

    zval *nodes = zend_hash_str_find(Z_ARRVAL(decoded), "nodes", sizeof("nodes") - 1);
    if (nodes != nullptr && Z_TYPE_P(nodes) == IS_ARRAY) {
        zend_string *project_name = nullptr;
        zend_ulong idx = 0;
        zval *service_map = nullptr;
        ZEND_HASH_FOREACH_KEY_VAL(Z_ARRVAL_P(nodes), idx, project_name, service_map) {
            if (project_name == nullptr || Z_TYPE_P(service_map) != IS_ARRAY) {
                continue;
            }
            zend_string *service_name = nullptr;
            zend_ulong inner_idx = 0;
            zval *node_map = nullptr;
            ZEND_HASH_FOREACH_KEY_VAL(Z_ARRVAL_P(service_map), inner_idx, service_name, node_map) {
                if (service_name == nullptr || Z_TYPE_P(node_map) != IS_ARRAY) {
                    continue;
                }
                zend_string *node_name = nullptr;
                zend_ulong node_idx = 0;
                zval *scope_value = nullptr;
                ZEND_HASH_FOREACH_KEY_VAL(Z_ARRVAL_P(node_map), node_idx, node_name, scope_value) {
                    if (node_name == nullptr || Z_TYPE_P(scope_value) != IS_ARRAY) {
                        continue;
                    }
                    flat_map_t map;
                    zend_string *key = nullptr;
                    zend_ulong key_idx = 0;
                    zval *entry = nullptr;
                    ZEND_HASH_FOREACH_KEY_VAL(Z_ARRVAL_P(scope_value), key_idx, key, entry) {
                        if (key != nullptr) {
                            map[std::string(ZSTR_VAL(key), ZSTR_LEN(key))] = kislay_string_from_zval(entry);
                        }
                    } ZEND_HASH_FOREACH_END();
                    obj->node_scopes[std::string(ZSTR_VAL(project_name), ZSTR_LEN(project_name))][std::string(ZSTR_VAL(service_name), ZSTR_LEN(service_name))][std::string(ZSTR_VAL(node_name), ZSTR_LEN(node_name))] = map;
                } ZEND_HASH_FOREACH_END();
            } ZEND_HASH_FOREACH_END();
        } ZEND_HASH_FOREACH_END();
    }
    pthread_mutex_unlock(&obj->lock);

    zval_ptr_dtor(&decoded);
    RETURN_TRUE;
}

static std::string kislay_server_response_json(const std::string &version, const flat_map_t &config, const std::string &checksum) {
    zval root;
    zval cfg;
    array_init(&root);
    add_assoc_string(&root, "version", const_cast<char *>(version.c_str()));
    add_assoc_string(&root, "checksum", const_cast<char *>(checksum.c_str()));
    kislay_flat_map_to_array(config, &cfg);
    add_assoc_zval(&root, "config", &cfg);
    std::string json;
    kislay_json_encode_zval(&root, &json);
    zval_ptr_dtor(&root);
    return json;
}

static std::string kislay_server_simple_json(const std::string &key, const std::string &value) {
    zval root;
    array_init(&root);
    add_assoc_string(&root, key.c_str(), const_cast<char *>(value.c_str()));
    std::string json;
    kislay_json_encode_zval(&root, &json);
    zval_ptr_dtor(&root);
    return json;
}

static void kislay_server_apply_remote_write(php_kislayphp_config_server_t *server, const std::string &path, const std::string &body, int client_fd) {
    zval decoded;
    ZVAL_UNDEF(&decoded);
    if (!kislay_json_decode_assoc(body, &decoded) || Z_TYPE(decoded) != IS_ARRAY) {
        if (!Z_ISUNDEF(decoded)) {
            zval_ptr_dtor(&decoded);
        }
        kislay_http_send_response(client_fd, 400, "application/json", "{\"error\":\"invalid json\"}");
        return;
    }

    flat_map_t flattened;
    std::string error;
    if (!kislay_zval_to_flat_map(&decoded, &flattened, &error)) {
        zval_ptr_dtor(&decoded);
        kislay_http_send_response(client_fd, 400, "application/json", "{\"error\":\"invalid config payload\"}");
        return;
    }
    zval_ptr_dtor(&decoded);

    std::vector<std::string> parts;
    std::stringstream stream(path);
    std::string item;
    while (std::getline(stream, item, '/')) {
        if (!item.empty()) {
            parts.push_back(kislay_url_decode(item));
        }
    }

    pthread_mutex_lock(&server->lock);
    bool ok = false;
    if (parts.size() == 3 && parts[0] == "v1" && parts[1] == "config" && parts[2] == "global") {
        server->global_scope = flattened;
        ok = true;
    } else if (parts.size() == 4 && parts[0] == "v1" && parts[1] == "config" && parts[2] == "environments") {
        server->environment_scopes[parts[3]] = flattened;
        ok = true;
    } else if (parts.size() == 4 && parts[0] == "v1" && parts[1] == "config" && parts[2] == "projects") {
        server->project_scopes[parts[3]] = flattened;
        ok = true;
    } else if (parts.size() == 6 && parts[0] == "v1" && parts[1] == "config" && parts[2] == "projects" && parts[4] == "services") {
        server->service_scopes[parts[3]][parts[5]] = flattened;
        ok = true;
    } else if (parts.size() == 8 && parts[0] == "v1" && parts[1] == "config" && parts[2] == "projects" && parts[4] == "services" && parts[6] == "nodes") {
        server->node_scopes[parts[3]][parts[5]][parts[7]] = flattened;
        ok = true;
    }
    if (ok) {
        kislay_server_bump_version(server);
    }
    std::string version = server->version;
    pthread_mutex_unlock(&server->lock);

    if (!ok) {
        kislay_http_send_response(client_fd, 404, "application/json", "{\"error\":\"unknown config scope\"}");
        return;
    }
    kislay_http_send_response(client_fd, 200, "application/json", kislay_server_simple_json("version", version));
}

PHP_METHOD(KislayPHPConfigServer, run) {
    php_kislayphp_config_server_t *obj = php_kislayphp_config_server_from_obj(Z_OBJ_P(getThis()));

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        zend_throw_exception(zend_ce_exception, "Unable to create server socket", 0);
        RETURN_FALSE;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(obj->port));
    if (obj->host == "0.0.0.0") {
        address.sin_addr.s_addr = INADDR_ANY;
    } else if (inet_pton(AF_INET, obj->host.c_str(), &address.sin_addr) != 1) {
        close(server_fd);
        zend_throw_exception(zend_ce_exception, "Invalid listen host", 0);
        RETURN_FALSE;
    }

    if (bind(server_fd, reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) != 0) {
        close(server_fd);
        zend_throw_exception(zend_ce_exception, "Unable to bind config server", 0);
        RETURN_FALSE;
    }
    if (listen(server_fd, 64) != 0) {
        close(server_fd);
        zend_throw_exception(zend_ce_exception, "Unable to listen on config server", 0);
        RETURN_FALSE;
    }

    obj->listen_fd = server_fd;
    obj->running = true;

    while (obj->running) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        kislay_http_request_t request;
        if (!kislay_http_read_request(client_fd, &request)) {
            close(client_fd);
            continue;
        }

        if (request.method == "GET" && request.path == "/health") {
            pthread_mutex_lock(&obj->lock);
            std::string payload = kislay_server_simple_json("version", obj->version);
            pthread_mutex_unlock(&obj->lock);
            kislay_http_send_response(client_fd, 200, "application/json", payload);
            close(client_fd);
            continue;
        }

        if (request.method == "GET" && request.path == "/v1/config/version") {
            pthread_mutex_lock(&obj->lock);
            std::string payload = kislay_server_simple_json("version", obj->version);
            pthread_mutex_unlock(&obj->lock);
            kislay_http_send_response(client_fd, 200, "application/json", payload);
            close(client_fd);
            continue;
        }

        if (request.method == "GET" && request.path == "/v1/config/resolve") {
            std::map<std::string, std::string> query = kislay_parse_query(request.query);
            std::string environment = query["environment"];
            std::string project = query["project"];
            std::string service = query["service"];
            std::string node = query["node"];
            pthread_mutex_lock(&obj->lock);
            flat_map_t resolved = kislay_server_resolve_locked(obj, environment, project, service, node);
            std::string version = obj->version;
            pthread_mutex_unlock(&obj->lock);
            std::string payload = kislay_server_response_json(version, resolved, kislay_checksum_for_map(resolved));
            kislay_http_send_response(client_fd, 200, "application/json", payload);
            close(client_fd);
            continue;
        }

        if (request.method == "PUT") {
            kislay_server_apply_remote_write(obj, request.path, request.body, client_fd);
            close(client_fd);
            continue;
        }

        kislay_http_send_response(client_fd, 404, "application/json", "{\"error\":\"not found\"}");
        close(client_fd);
    }

    if (obj->listen_fd >= 0) {
        close(obj->listen_fd);
        obj->listen_fd = -1;
    }
    obj->running = false;
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPConfigServer, stop) {
    php_kislayphp_config_server_t *obj = php_kislayphp_config_server_from_obj(Z_OBJ_P(getThis()));
    obj->running = false;
    if (obj->listen_fd >= 0) {
        shutdown(obj->listen_fd, SHUT_RDWR);
        close(obj->listen_fd);
        obj->listen_fd = -1;
    }
    RETURN_TRUE;
}

static const zend_function_entry kislayphp_config_client_methods[] = {
    PHP_ME(KislayPHPConfigClient, __construct, arginfo_kislayphp_config_void, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfigClient, setClient, arginfo_kislayphp_config_set_client, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfigClient, set, arginfo_kislayphp_config_set, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfigClient, get, arginfo_kislayphp_config_get, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfigClient, getString, arginfo_kislayphp_config_get_string, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfigClient, getInt, arginfo_kislayphp_config_get_int, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfigClient, getBool, arginfo_kislayphp_config_get_bool, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfigClient, getArray, arginfo_kislayphp_config_get_array, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfigClient, all, arginfo_kislayphp_config_void, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfigClient, has, arginfo_kislayphp_config_has, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfigClient, remove, arginfo_kislayphp_config_remove, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfigClient, delete, arginfo_kislayphp_config_remove, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfigClient, refresh, arginfo_kislayphp_config_void, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

static const zend_function_entry kislayphp_config_runtime_methods[] = {
    PHP_ME(KislayPHPConfigRuntime, boot, arginfo_kislayphp_config_boot, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(KislayPHPConfigRuntime, loadLocal, arginfo_kislayphp_config_load_local, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(KislayPHPConfigRuntime, refresh, arginfo_kislayphp_config_void, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(KislayPHPConfigRuntime, setOverride, arginfo_kislayphp_config_set_override, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(KislayPHPConfigRuntime, has, arginfo_kislayphp_config_has, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(KislayPHPConfigRuntime, get, arginfo_kislayphp_config_get, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(KislayPHPConfigRuntime, getString, arginfo_kislayphp_config_get_string, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(KislayPHPConfigRuntime, getInt, arginfo_kislayphp_config_get_int, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(KislayPHPConfigRuntime, getBool, arginfo_kislayphp_config_get_bool, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(KislayPHPConfigRuntime, getArray, arginfo_kislayphp_config_get_array, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(KislayPHPConfigRuntime, all, arginfo_kislayphp_config_void, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(KislayPHPConfigRuntime, version, arginfo_kislayphp_config_void, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(KislayPHPConfigRuntime, checksum, arginfo_kislayphp_config_void, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_FE_END
};

static const zend_function_entry kislayphp_config_server_methods[] = {
    PHP_ME(KislayPHPConfigServer, __construct, arginfo_kislayphp_server_construct, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfigServer, listen, arginfo_kislayphp_server_listen, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfigServer, run, arginfo_kislayphp_config_void, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfigServer, stop, arginfo_kislayphp_config_void, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfigServer, setGlobal, arginfo_kislayphp_server_scope, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfigServer, setEnvironment, arginfo_kislayphp_server_environment_scope, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfigServer, setProject, arginfo_kislayphp_server_project_scope, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfigServer, setService, arginfo_kislayphp_server_service_scope, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfigServer, setNode, arginfo_kislayphp_server_node_scope, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfigServer, resolve, arginfo_kislayphp_server_resolve, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfigServer, version, arginfo_kislayphp_config_void, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfigServer, save, arginfo_kislayphp_config_load_local, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfigServer, load, arginfo_kislayphp_config_load_local, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

static const zend_function_entry kislayphp_config_client_interface_methods[] = {
    ZEND_ABSTRACT_ME(KislayPHPConfigClientInterface, set, arginfo_kislayphp_config_set)
    ZEND_ABSTRACT_ME(KislayPHPConfigClientInterface, get, arginfo_kislayphp_config_get)
    ZEND_ABSTRACT_ME(KislayPHPConfigClientInterface, all, arginfo_kislayphp_config_void)
    PHP_FE_END
};

PHP_MINIT_FUNCTION(kislayphp_config) {
    zend_class_entry ce;

    INIT_NS_CLASS_ENTRY(ce, "Kislay\\Config", "ClientInterface", kislayphp_config_client_interface_methods);
    kislayphp_config_client_interface_ce = zend_register_internal_interface(&ce);
    zend_register_class_alias("KislayPHP\\Config\\ClientInterface", kislayphp_config_client_interface_ce);

    INIT_NS_CLASS_ENTRY(ce, "Kislay\\Config", "ConfigClient", kislayphp_config_client_methods);
    kislayphp_config_client_ce = zend_register_internal_class(&ce);
    zend_register_class_alias("KislayPHP\\Config\\ConfigClient", kislayphp_config_client_ce);
    kislayphp_config_client_ce->create_object = kislayphp_config_client_create_object;
    std::memcpy(&kislayphp_config_client_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    kislayphp_config_client_handlers.offset = XtOffsetOf(php_kislayphp_config_client_t, std);
    kislayphp_config_client_handlers.free_obj = kislayphp_config_client_free_obj;

    INIT_NS_CLASS_ENTRY(ce, "Kislay\\Config", "Config", kislayphp_config_runtime_methods);
    kislayphp_config_runtime_ce = zend_register_internal_class(&ce);
    zend_register_class_alias("KislayPHP\\Config\\Config", kislayphp_config_runtime_ce);

    INIT_NS_CLASS_ENTRY(ce, "Kislay\\Config", "Server", kislayphp_config_server_methods);
    kislayphp_config_server_ce = zend_register_internal_class(&ce);
    zend_register_class_alias("KislayPHP\\Config\\Server", kislayphp_config_server_ce);
    kislayphp_config_server_ce->create_object = kislayphp_config_server_create_object;
    std::memcpy(&kislayphp_config_server_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    kislayphp_config_server_handlers.offset = XtOffsetOf(php_kislayphp_config_server_t, std);
    kislayphp_config_server_handlers.free_obj = kislayphp_config_server_free_obj;

    return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(kislayphp_config) {
    return SUCCESS;
}

PHP_MINFO_FUNCTION(kislayphp_config) {
    php_info_print_table_start();
    php_info_print_table_header(2, "kislayphp_config support", "enabled");
    php_info_print_table_row(2, "Version", PHP_KISLAYPHP_CONFIG_VERSION);
    php_info_print_table_row(2, "Primary API", "Kislay\\Config\\Config + Kislay\\Config\\Server");
    php_info_print_table_end();
}

zend_module_entry kislayphp_config_module_entry = {
    STANDARD_MODULE_HEADER,
    PHP_KISLAYPHP_CONFIG_EXTNAME,
    nullptr,
    PHP_MINIT(kislayphp_config),
    PHP_MSHUTDOWN(kislayphp_config),
    nullptr,
    nullptr,
    PHP_MINFO(kislayphp_config),
    PHP_KISLAYPHP_CONFIG_VERSION,
    STANDARD_MODULE_PROPERTIES
};

#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE();
#endif

extern "C" {
ZEND_DLEXPORT zend_module_entry *get_module(void) {
    return &kislayphp_config_module_entry;
}
}
