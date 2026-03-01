# KislayPHP Config

[![PHP Version](https://img.shields.io/badge/PHP-8.2+-blue.svg)](https://php.net)
[![License](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)
[![Build Status](https://img.shields.io/github/actions/workflow/status/KislayPHP/config/ci.yml)](https://github.com/KislayPHP/config/actions)
[![codecov](https://codecov.io/gh/KislayPHP/config/branch/main/graph/badge.svg)](https://codecov.io/gh/KislayPHP/config)

A high-performance C++ PHP extension providing dynamic configuration management for microservices with support for multiple backends and hot-reloading. Perfect for PHP ecosystem integration and modern microservices architecture.

Primary runtime namespace is `Kislay\Config` (legacy `KislayPHP\Config` aliases are kept for compatibility).
For service-to-service config policy, see `SERVICE_COMMUNICATION.md` and `service_communication.php`.

## ⚡ Key Features

- 🚀 **High Performance**: Fast configuration retrieval with caching
- 🔄 **Hot Reloading**: Dynamic configuration updates without restart
- 🔌 **Pluggable Backends**: Support for external registry, distributed KV store, KV store, and custom clients
- 🌐 **Environment Integration**: Automatic environment variable loading
- 📁 **File Support**: JSON, YAML, and INI configuration files
- 🔧 **Hierarchical Config**: Namespace-based configuration organization
- 📊 **Monitoring**: Configuration change tracking and metrics
- 🔄 **PHP Ecosystem**: Seamless integration with PHP ecosystem and frameworks
- 🌐 **Microservices Architecture**: Designed for distributed PHP applications

## 📦 Installation

### Via PIE (Recommended)

```bash
pie install kislayphp/config:0.0.2
```

Add to your `php.ini`:

```ini
extension=kislayphp_config.so
```

### Manual Build

```bash
git clone https://github.com/KislayPHP/config.git
cd config
phpize
./configure
make
sudo make install
```

### container

```containerfile
FROM php:8.2-cli
```

## 🚀 Quick Start

### Basic Usage

```php
<?php

// Create config instance
$config = new KislayConfig();

// Set configuration values
$config->set('database.host', 'localhost');
$config->set('database.port', 3306);
$config->set('cache.enabled', true);

// Get configuration values
$host = $config->get('database.host');        // 'localhost'
$port = $config->get('database.port', 3306);  // 3306 (with default)
$cache = $config->get('cache.enabled');       // true

// Get all config as array
$allConfig = $config->all();

// Check if key exists
if ($config->has('database.host')) {
    // Key exists
}
```

### Backend Integration

```php
<?php

// Use external registry backend
$config = new KislayConfig([
    'backend' => 'registry',
    'registry' => [
        'host' => 'registry-server:8500',
        'token' => 'your-registry-token'
    ]
]);

// Configuration automatically loaded from external registry
$appName = $config->get('app.name');
$databaseUrl = $config->get('database.url');
```

### Environment Variables

```php
<?php

// Load from environment variables
$config = new KislayConfig();
$config->loadFromEnv();

// Access environment variables
$env = $config->get('APP_ENV');           // production
$debug = $config->get('DEBUG', false);    // false (default)
```

### File-based Configuration

```php
<?php

// Load from JSON file
$config = new KislayConfig();
$config->loadFromFile('/path/to/config.json');

// Load from YAML file
$config->loadFromFile('/path/to/config.yaml');

// Load from INI file
$config->loadFromFile('/path/to/config.ini');
```

## 📚 Documentation

📖 **[Complete Documentation](docs.md)** - API reference, backend integrations, examples, and best practices
- 🌐 **Full Detailed Docs Site:** [https://skelves.com/docs](https://skelves.com/docs)
- 🧪 **Local Docs Route:** `http://localhost:5180/docs`

## 🏗️ Architecture

KislayPHP Config implements a flexible configuration system:

```
┌─────────────────┐    ┌─────────────────┐
│   Application   │    │   Application   │
│                 │    │                 │
│ ┌─────────────┐ │    │ ┌─────────────┐ │
│ │ Config API  │ │    │ │ Config API  │ │
│ │ (PHP)       │ │    │ │ (PHP)       │ │
│ └─────────────┘ │    │ └─────────────┘ │
└─────────────────┘    └─────────────────┘
         │                       │
         └───────────────────────┘
            ┌─────────────────┐
            │ Configuration   │
            │ Backends        │
            │ (external registry/distributed KV store/   │
            │  KV store/Custom)  │
            └─────────────────┘
```

## 🎯 Use Cases

- **Microservices**: Centralized configuration management
- **Multi-environment**: Different configs for dev/staging/prod
- **Dynamic Updates**: Runtime configuration changes
- **Service Discovery**: Integration with service registries
- **Feature Flags**: Dynamic feature toggling
- **Secret Management**: Secure credential storage

## 📊 Performance

```
Configuration Benchmark:
==================
Read Operations:     100,000/sec
Write Operations:     50,000/sec
Memory Usage:         8 MB
Cache Hit Rate:       95%
Average Latency:      0.05 ms
```

## 🔧 Configuration

### php.ini Settings

```ini
; Config extension settings
kislayphp.config.cache_size = 1000
kislayphp.config.cache_ttl = 300
kislayphp.config.enable_hot_reload = 1
kislayphp.config.reload_interval = 60

; Backend settings
kislayphp.config.backend = "memory"
kislayphp.config.registry_host = "localhost:8500"
kislayphp.config.distributed KV store_endpoints = "localhost:2379"
```

### Environment Variables

```bash
export KISLAYPHP_CONFIG_BACKEND=registry
export KISLAYPHP_CONFIG_REGISTRY_HOST=registry-server:8500
export KISLAYPHP_CONFIG_CACHE_SIZE=1000
export KISLAYPHP_CONFIG_HOT_RELOAD=1
```

## 🧪 Testing

```bash
# Run unit tests
php run-tests.php

# Test with different backends
cd tests/
php test_registry_backend.php
php test_distributed KV store_backend.php
```

## 🤝 Contributing

We welcome contributions! Please see our [Contributing Guide](.github/CONTRIBUTING.md) for details.

## 📄 License

Licensed under the [Apache License 2.0](LICENSE).

## 🆘 Support

- 📖 [Documentation](docs.md)
- 🐛 [Issue Tracker](https://github.com/KislayPHP/config/issues)
- 💬 [Discussions](https://github.com/KislayPHP/config/discussions)
- 📧 [Security Issues](.github/SECURITY.md)

## 📈 Roadmap

- [ ] orchestrator config map integration
- [ ] AWS Parameter Store support
- [ ] Azure Key Vault integration
- [ ] Configuration validation schemas
- [ ] Configuration history and rollback

## 🙏 Acknowledgments

- **external registry**: Service discovery and configuration
- **distributed KV store**: Distributed KV store
- **PHP**: Zend API for extension development

---

**Built with ❤️ for configurable PHP applications**

## Installation

### Via PIE

```bash
pie install kislayphp/config
```

Then add to your php.ini:

```ini
extension=kislayphp_config.so
```

### Manual Build

```sh
phpize
./configure --enable-kislayphp_config
make
```

## Run Locally

```sh
cd /path/to/config
php -d extension=modules/kislayphp_config.so example.php
```

## Custom Client Interface

Default is in-memory. To plug in KV store, MySQL, Mongo, or any other backend, provide
your own PHP client that implements `KislayPHP\Config\ClientInterface` and call
`setClient()`.

Example:

```php
$cfg = new KislayPHP\Config\ConfigClient();
$cfg->setClient(new MyConfigClient());
```

## Example

```php
<?php
extension_loaded('kislayphp_config') or die('kislayphp_config not loaded');

$cfg = new KislayPHP\Config\ConfigClient();
$cfg->set('db.host', '127.0.0.1');
$cfg->set('db.port', '5432');

var_dump($cfg->get('db.host'));
print_r($cfg->all());
?>
```

## SEO Keywords

PHP, microservices, PHP ecosystem, PHP extension, C++ PHP extension, PHP configuration management, dynamic PHP config, PHP hot reloading, PHP external registry, PHP distributed KV store, PHP KV store config, PHP microservices config, distributed PHP configuration

---
