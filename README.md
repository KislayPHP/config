# KislayPHP Config

[![PHP Version](https://img.shields.io/badge/PHP-8.2%2B-blue.svg)](https://php.net)
[![License](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)
[![Build Status](https://img.shields.io/github/actions/workflow/status/KislayPHP/config/ci.yml?branch=main&label=CI)](https://github.com/KislayPHP/config/actions)
[![PIE](https://img.shields.io/badge/install-pie-blueviolet)](https://github.com/php/pie)

> **Centralized configuration for PHP microservices.** Environment-aware, namespace-scoped, hot-reloadable configuration — shared across services without a config server.

Part of the [KislayPHP ecosystem](https://skelves.com/kislayphp/docs).

---

## ✨ What It Does

`kislayphp/config` provides structured, namespace-scoped configuration management for distributed PHP services. Load from environment variables, INI files, or remote sources. Services share config without tight coupling.

```php
<?php
$config = new Kislay\Config\Config();
$config->set('db.host', getenv('DB_HOST') ?: 'localhost');

$host = $config->get('db.host');  // 'localhost'
```

---

## 📦 Installation

```bash
pie install kislayphp/config
```

Enable in `php.ini`:
```ini
extension=kislayphp_config.so
```

---

## 🚀 Quick Start

```php
<?php
$config = new Kislay\Config\Config();

// Load from environment
$config->set('app.env',      getenv('APP_ENV') ?: 'production');
$config->set('db.host',      getenv('DB_HOST') ?: 'localhost');
$config->set('db.port',      (int) (getenv('DB_PORT') ?: 5432));
$config->set('cache.ttl',    (int) (getenv('CACHE_TTL') ?: 3600));

// Read anywhere in the application
$dbHost  = $config->get('db.host');
$appEnv  = $config->get('app.env');

// Check existence before reading
if ($config->has('feature.new_ui')) {
    enable_new_ui();
}

// Delete a key
$config->delete('temp.key');

// Get all config as array
$all = $config->all();
```

### Shared Config Across Services

```php
<?php
// bootstrap.php — shared configuration singleton
$config = new Kislay\Config\Config();

$config->set('gateway.host',    getenv('GATEWAY_HOST') ?: '127.0.0.1');
$config->set('gateway.port',    (int)(getenv('GATEWAY_PORT') ?: 9008));
$config->set('metrics.enabled', (bool)(getenv('METRICS_ENABLED') ?: true));

// Pass to all services that need it
$app->set('config', $config);
```

---

## 📖 Public API

```php
namespace Kislay\Config;

class Config {
    public function __construct();
    public function set(string $key, mixed $value): bool;
    public function get(string $key, mixed $default = null): mixed;
    public function has(string $key): bool;
    public function delete(string $key): bool;
    public function all(): array;
}
```

Legacy aliases: `KislayPHP\Config\Config`

---

## 🔗 Ecosystem

[core](https://github.com/KislayPHP/core) · [gateway](https://github.com/KislayPHP/gateway) · [discovery](https://github.com/KislayPHP/discovery) · [metrics](https://github.com/KislayPHP/metrics) · [queue](https://github.com/KislayPHP/queue) · [eventbus](https://github.com/KislayPHP/eventbus) · **config**

## 📄 License

[Apache License 2.0](LICENSE) · **[Full Docs](https://skelves.com/kislayphp/docs)**
