extern "C" {
#include "php.h"
#include "ext/standard/info.h"
#include "Zend/zend_API.h"
#include "Zend/zend_interfaces.h"
#include "Zend/zend_exceptions.h"
}

#include "php_kislayphp_config.h"

#include <chrono>
#include <cstring>
#include <pthread.h>
#include <cstdlib>
#include <mutex>
#include <string>
#include <unordered_map>

#ifdef KISLAYPHP_RPC
#include <grpcpp/grpcpp.h>

#include "platform.grpc.pb.h"
#endif

#ifndef zend_call_method_with_0_params
static inline void kislayphp_call_method_with_0_params(
    zend_object *obj,
    zend_class_entry *obj_ce,
    zend_function **fn_proxy,
    const char *function_name,
    zval *retval) {
    zend_call_method(obj, obj_ce, fn_proxy, function_name, std::strlen(function_name), retval, 0, nullptr, nullptr);
}

#define zend_call_method_with_0_params(obj, obj_ce, fn_proxy, function_name, retval) \
    kislayphp_call_method_with_0_params(obj, obj_ce, fn_proxy, function_name, retval)
#endif

#ifndef zend_call_method_with_1_params
static inline void kislayphp_call_method_with_1_params(
    zend_object *obj,
    zend_class_entry *obj_ce,
    zend_function **fn_proxy,
    const char *function_name,
    zval *retval,
    zval *param1) {
    zend_call_method(obj, obj_ce, fn_proxy, function_name, std::strlen(function_name), retval, 1, param1, nullptr);
}

#define zend_call_method_with_1_params(obj, obj_ce, fn_proxy, function_name, retval, param1) \
    kislayphp_call_method_with_1_params(obj, obj_ce, fn_proxy, function_name, retval, param1)
#endif

#ifndef zend_call_method_with_2_params
static inline void kislayphp_call_method_with_2_params(
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
    kislayphp_call_method_with_2_params(obj, obj_ce, fn_proxy, function_name, retval, param1, param2)
#endif
static zend_class_entry *kislayphp_config_ce;
static zend_class_entry *kislayphp_config_client_ce;

static zend_long kislayphp_env_long(const char *name, zend_long fallback) {
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    return static_cast<zend_long>(std::strtoll(value, nullptr, 10));
}

static bool kislayphp_env_bool(const char *name, bool fallback) {
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    if (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 || std::strcmp(value, "TRUE") == 0) {
        return true;
    }
    if (std::strcmp(value, "0") == 0 || std::strcmp(value, "false") == 0 || std::strcmp(value, "FALSE") == 0) {
        return false;
    }
    return fallback;
}

static std::string kislayphp_env_string(const char *name, const std::string &fallback) {
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    return std::string(value);
}

#ifdef KISLAYPHP_RPC
static bool kislayphp_rpc_enabled() {
    return kislayphp_env_bool("KISLAY_RPC_ENABLED", false);
}

static zend_long kislayphp_rpc_timeout_ms() {
    zend_long timeout = kislayphp_env_long("KISLAY_RPC_TIMEOUT_MS", 200);
    return timeout > 0 ? timeout : 200;
}

static std::string kislayphp_rpc_platform_endpoint() {
    return kislayphp_env_string("KISLAY_RPC_PLATFORM_ENDPOINT", "127.0.0.1:9100");
}

static kislay::platform::v1::ConfigService::Stub *kislayphp_rpc_config_stub(const std::string &endpoint) {
    static std::mutex lock;
    static std::string cached_endpoint;
    static std::shared_ptr<grpc::Channel> channel;
    static std::unique_ptr<kislay::platform::v1::ConfigService::Stub> stub;
    std::lock_guard<std::mutex> guard(lock);
    if (!stub || cached_endpoint != endpoint) {
        channel = grpc::CreateChannel(endpoint, grpc::InsecureChannelCredentials());
        stub = kislay::platform::v1::ConfigService::NewStub(channel);
        cached_endpoint = endpoint;
    }
    return stub.get();
}

static bool kislayphp_rpc_config_set(const std::string &key, const std::string &value, std::string *error) {
    auto *stub = kislayphp_rpc_config_stub(kislayphp_rpc_platform_endpoint());
    if (!stub) {
        if (error) {
            *error = "RPC stub unavailable";
        }
        return false;
    }

    kislay::platform::v1::SetRequest request;
    request.set_key(key);
    request.set_value(value);

    kislay::platform::v1::SetResponse response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(kislayphp_rpc_timeout_ms()));

    grpc::Status status = stub->Set(&context, request, &response);
    if (!status.ok()) {
        if (error) {
            *error = status.error_message();
        }
        return false;
    }
    if (!response.ok()) {
        if (error) {
            *error = response.error();
        }
        return false;
    }
    return true;
}

static bool kislayphp_rpc_config_get(const std::string &key, std::string *value, bool *found, std::string *error) {
    auto *stub = kislayphp_rpc_config_stub(kislayphp_rpc_platform_endpoint());
    if (!stub) {
        if (error) {
            *error = "RPC stub unavailable";
        }
        return false;
    }

    kislay::platform::v1::GetRequest request;
    request.set_key(key);

    kislay::platform::v1::GetResponse response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(kislayphp_rpc_timeout_ms()));

    grpc::Status status = stub->Get(&context, request, &response);
    if (!status.ok()) {
        if (error) {
            *error = status.error_message();
        }
        return false;
    }

    if (found) {
        *found = response.found();
    }
    if (value) {
        *value = response.value();
    }
    return true;
}

static bool kislayphp_rpc_config_all(zval *return_value, std::string *error) {
    auto *stub = kislayphp_rpc_config_stub(kislayphp_rpc_platform_endpoint());
    if (!stub) {
        if (error) {
            *error = "RPC stub unavailable";
        }
        return false;
    }

    kislay::platform::v1::AllRequest request;
    kislay::platform::v1::AllResponse response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(kislayphp_rpc_timeout_ms()));

    grpc::Status status = stub->All(&context, request, &response);
    if (!status.ok()) {
        if (error) {
            *error = status.error_message();
        }
        return false;
    }

    array_init(return_value);
    for (const auto &item : response.items()) {
        add_assoc_string(return_value, item.key().c_str(), item.value().c_str());
    }
    return true;
}

static bool kislayphp_rpc_config_remove(const std::string &key, bool *ok, std::string *error) {
    auto *stub = kislayphp_rpc_config_stub(kislayphp_rpc_platform_endpoint());
    if (!stub) {
        if (error) {
            *error = "RPC stub unavailable";
        }
        return false;
    }

    kislay::platform::v1::RemoveRequest request;
    request.set_key(key);
    kislay::platform::v1::RemoveResponse response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(kislayphp_rpc_timeout_ms()));

    grpc::Status status = stub->Remove(&context, request, &response);
    if (!status.ok()) {
        if (error) {
            *error = status.error_message();
        }
        return false;
    }
    if (ok) {
        *ok = response.ok();
    }
    return true;
}
#endif

typedef struct _php_kislayphp_config_t {
    std::unordered_map<std::string, std::string> values;
    pthread_mutex_t lock;
    zval client;
    bool has_client;
    zend_object std;
} php_kislayphp_config_t;

static zend_object_handlers kislayphp_config_handlers;

static inline php_kislayphp_config_t *php_kislayphp_config_from_obj(zend_object *obj) {
    return reinterpret_cast<php_kislayphp_config_t *>(
        reinterpret_cast<char *>(obj) - XtOffsetOf(php_kislayphp_config_t, std));
}

static zend_object *kislayphp_config_create_object(zend_class_entry *ce) {
    php_kislayphp_config_t *obj = static_cast<php_kislayphp_config_t *>(
        ecalloc(1, sizeof(php_kislayphp_config_t) + zend_object_properties_size(ce)));
    zend_object_std_init(&obj->std, ce);
    object_properties_init(&obj->std, ce);
    new (&obj->values) std::unordered_map<std::string, std::string>();
    pthread_mutex_init(&obj->lock, nullptr);
    ZVAL_UNDEF(&obj->client);
    obj->has_client = false;
    obj->std.handlers = &kislayphp_config_handlers;
    return &obj->std;
}

static void kislayphp_config_free_obj(zend_object *object) {
    php_kislayphp_config_t *obj = php_kislayphp_config_from_obj(object);
    if (obj->has_client) {
        zval_ptr_dtor(&obj->client);
    }
    obj->values.~unordered_map();
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
    ZEND_ARG_OBJ_INFO(0, client, KislayPHP\\Config\\ClientInterface, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_config_has, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, key, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_kislayphp_config_remove, 0, 0, 1)
    ZEND_ARG_TYPE_INFO(0, key, IS_STRING, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(KislayPHPConfig, __construct) {
    ZEND_PARSE_PARAMETERS_NONE();
}

PHP_METHOD(KislayPHPConfig, setClient) {
    zval *client = nullptr;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ZVAL(client)
    ZEND_PARSE_PARAMETERS_END();

    if (client == nullptr || Z_TYPE_P(client) != IS_OBJECT) {
        zend_throw_exception(zend_ce_exception, "Client must be an object", 0);
        RETURN_FALSE;
    }

    if (!instanceof_function(Z_OBJCE_P(client), kislayphp_config_client_ce)) {
        zend_throw_exception(zend_ce_exception, "Client must implement KislayPHP\\Config\\ClientInterface", 0);
        RETURN_FALSE;
    }

    php_kislayphp_config_t *obj = php_kislayphp_config_from_obj(Z_OBJ_P(getThis()));
    if (obj->has_client) {
        zval_ptr_dtor(&obj->client);
        obj->has_client = false;
    }
    ZVAL_COPY(&obj->client, client);
    obj->has_client = true;
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPConfig, set) {
    char *key = nullptr;
    size_t key_len = 0;
    char *value = nullptr;
    size_t value_len = 0;
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STRING(key, key_len)
        Z_PARAM_STRING(value, value_len)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_config_t *obj = php_kislayphp_config_from_obj(Z_OBJ_P(getThis()));
    if (obj->has_client) {
        zval key_zv;
        zval value_zv;
        ZVAL_STRINGL(&key_zv, key, key_len);
        ZVAL_STRINGL(&value_zv, value, value_len);

        zval retval;
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

#ifdef KISLAYPHP_RPC
    if (kislayphp_rpc_enabled()) {
        std::string error;
        if (kislayphp_rpc_config_set(std::string(key, key_len), std::string(value, value_len), &error)) {
            RETURN_TRUE;
        }
    }
#endif

    pthread_mutex_lock(&obj->lock);
    obj->values[std::string(key, key_len)] = std::string(value, value_len);
    pthread_mutex_unlock(&obj->lock);
    RETURN_TRUE;
}

PHP_METHOD(KislayPHPConfig, get) {
    char *key = nullptr;
    size_t key_len = 0;
    zval *default_val = nullptr;
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STRING(key, key_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_ZVAL(default_val)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_config_t *obj = php_kislayphp_config_from_obj(Z_OBJ_P(getThis()));
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
            RETURN_NULL();
        }
        RETVAL_ZVAL(&retval, 1, 1);
        return;
    }

#ifdef KISLAYPHP_RPC
    if (kislayphp_rpc_enabled()) {
        std::string value;
        bool found = false;
        std::string error;
        if (kislayphp_rpc_config_get(std::string(key, key_len), &value, &found, &error)) {
            if (!found) {
                if (default_val != nullptr) {
                    RETURN_ZVAL(default_val, 1, 0);
                }
                RETURN_NULL();
            }
            RETURN_STRING(value.c_str());
        }
    }
#endif

    std::string value;
    bool found = false;
    pthread_mutex_lock(&obj->lock);
    auto it = obj->values.find(std::string(key, key_len));
    if (it != obj->values.end()) {
        value = it->second;
        found = true;
    }
    pthread_mutex_unlock(&obj->lock);
    if (!found) {
        if (default_val != nullptr) {
            RETURN_ZVAL(default_val, 1, 0);
        }
        RETURN_NULL();
    }
    RETURN_STRING(value.c_str());
}

PHP_METHOD(KislayPHPConfig, all) {
    php_kislayphp_config_t *obj = php_kislayphp_config_from_obj(Z_OBJ_P(getThis()));
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

#ifdef KISLAYPHP_RPC
    if (kislayphp_rpc_enabled()) {
        std::string error;
        if (kislayphp_rpc_config_all(return_value, &error)) {
            return;
        }
    }
#endif

    array_init(return_value);
    pthread_mutex_lock(&obj->lock);
    for (const auto &entry : obj->values) {
        add_assoc_string(return_value, entry.first.c_str(), entry.second.c_str());
    }
    pthread_mutex_unlock(&obj->lock);
}

PHP_METHOD(KislayPHPConfig, has) {
    char *key = nullptr;
    size_t key_len = 0;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(key, key_len)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_config_t *obj = php_kislayphp_config_from_obj(Z_OBJ_P(getThis()));
    if (obj->has_client) {
        zval key_zv;
        ZVAL_STRINGL(&key_zv, key, key_len);

        zval retval;
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

#ifdef KISLAYPHP_RPC
    if (kislayphp_rpc_enabled()) {
        std::string value;
        bool found = false;
        std::string error;
        if (kislayphp_rpc_config_get(std::string(key, key_len), &value, &found, &error)) {
            RETURN_BOOL(found);
        }
    }
#endif

    bool found = false;
    pthread_mutex_lock(&obj->lock);
    auto it = obj->values.find(std::string(key, key_len));
    found = (it != obj->values.end());
    pthread_mutex_unlock(&obj->lock);
    RETURN_BOOL(found);
}

PHP_METHOD(KislayPHPConfig, remove) {
    char *key = nullptr;
    size_t key_len = 0;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STRING(key, key_len)
    ZEND_PARSE_PARAMETERS_END();

    php_kislayphp_config_t *obj = php_kislayphp_config_from_obj(Z_OBJ_P(getThis()));
    if (obj->has_client) {
        zval key_zv;
        ZVAL_STRINGL(&key_zv, key, key_len);
        zval null_zv;
        ZVAL_NULL(&null_zv);

        zval retval;
        ZVAL_UNDEF(&retval);
        zend_call_method_with_2_params(Z_OBJ(obj->client), Z_OBJCE(obj->client), nullptr, "set", &retval, &key_zv, &null_zv);
        zval_ptr_dtor(&key_zv);

        if (Z_ISUNDEF(retval)) {
            RETURN_TRUE;
        }
        RETVAL_ZVAL(&retval, 1, 1);
        return;
    }

#ifdef KISLAYPHP_RPC
    if (kislayphp_rpc_enabled()) {
        std::string error;
        bool ok = false;
        if (kislayphp_rpc_config_remove(std::string(key, key_len), &ok, &error)) {
            RETURN_BOOL(ok);
        }
    }
#endif

    bool removed = false;
    pthread_mutex_lock(&obj->lock);
    auto it = obj->values.find(std::string(key, key_len));
    if (it != obj->values.end()) {
        obj->values.erase(it);
        removed = true;
    }
    pthread_mutex_unlock(&obj->lock);
    RETURN_BOOL(removed);
}

static const zend_function_entry kislayphp_config_methods[] = {
    PHP_ME(KislayPHPConfig, __construct, arginfo_kislayphp_config_void, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfig, setClient, arginfo_kislayphp_config_set_client, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfig, set, arginfo_kislayphp_config_set, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfig, get, arginfo_kislayphp_config_get, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfig, all, arginfo_kislayphp_config_void, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfig, has, arginfo_kislayphp_config_has, ZEND_ACC_PUBLIC)
    PHP_ME(KislayPHPConfig, remove, arginfo_kislayphp_config_remove, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

static const zend_function_entry kislayphp_config_client_methods[] = {
    ZEND_ABSTRACT_ME(KislayPHPConfigClientInterface, set, arginfo_kislayphp_config_set)
    ZEND_ABSTRACT_ME(KislayPHPConfigClientInterface, get, arginfo_kislayphp_config_get)
    ZEND_ABSTRACT_ME(KislayPHPConfigClientInterface, all, arginfo_kislayphp_config_void)
    PHP_FE_END
};

PHP_MINIT_FUNCTION(kislayphp_config) {
    zend_class_entry ce;
    INIT_NS_CLASS_ENTRY(ce, "KislayPHP\\Config", "ClientInterface", kislayphp_config_client_methods);
    kislayphp_config_client_ce = zend_register_internal_interface(&ce);
    INIT_NS_CLASS_ENTRY(ce, "KislayPHP\\Config", "ConfigClient", kislayphp_config_methods);
    kislayphp_config_ce = zend_register_internal_class(&ce);
    kislayphp_config_ce->create_object = kislayphp_config_create_object;
    std::memcpy(&kislayphp_config_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    kislayphp_config_handlers.offset = XtOffsetOf(php_kislayphp_config_t, std);
    kislayphp_config_handlers.free_obj = kislayphp_config_free_obj;
    return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(kislayphp_config) {
    return SUCCESS;
}

PHP_MINFO_FUNCTION(kislayphp_config) {
    php_info_print_table_start();
    php_info_print_table_header(2, "kislayphp_config support", "enabled");
    php_info_print_table_row(2, "Version", PHP_KISLAYPHP_CONFIG_VERSION);
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

#if defined(COMPILE_DL_KISLAYPHP_CONFIG) || defined(ZEND_COMPILE_DL_EXT)
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE();
#endif
extern "C" {
ZEND_GET_MODULE(kislayphp_config)
}
#endif
