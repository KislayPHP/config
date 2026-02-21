#ifndef PHP_KISLAYPHP_CONFIG_H
#define PHP_KISLAYPHP_CONFIG_H

extern "C" {
#include "php.h"
}

#define PHP_KISLAYPHP_CONFIG_VERSION "0.0.1"
#define PHP_KISLAYPHP_CONFIG_EXTNAME "kislayphp_config"

extern zend_module_entry kislayphp_config_module_entry;
#define phpext_kislayphp_config_ptr &kislayphp_config_module_entry

#endif
