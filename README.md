# KislayPHP Config

[![PHP Version](https://img.shields.io/badge/PHP-8.2%2B-blue.svg)](https://php.net)
[![License](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)
[![Build Status](https://img.shields.io/github/actions/workflow/status/KislayPHP/config/ci.yml?branch=main&label=CI)](https://github.com/KislayPHP/config/actions)
[![PIE](https://img.shields.io/badge/install-pie-blueviolet)](https://github.com/php/pie)

`kislayphp/config` is a centralized configuration service and runtime client for multi-node PHP services.

Phase 1 adds:
- standalone config server
- runtime client bootstrap with local cache support
- layered config resolution for environment, project, service, and node scopes
- local file, environment variable, and runtime overrides

This package is part of the [KislayPHP ecosystem](https://skelves.com/kislayphp/docs).

## Installation

```bash
pie install kislayphp/config
```

Enable the extension:

```ini
extension=kislayphp_config.so
```

## Quick Start

### 1. Start a config server

```php
<?php

$server = new Kislay\Config\Server();
$server->listen('0.0.0.0', 9011);

$server->setGlobal([
    'app' => ['name' => 'commerce-platform'],
    'db' => ['port' => 3306],
]);

$server->setEnvironment('prod', [
    'app' => ['debug' => false],
]);

$server->setProject('commerce', [
    'log' => ['level' => 'warn'],
]);

$server->setService('commerce', 'order-service', [
    'db' => ['name' => 'orders'],
    'gateway' => ['timeout_ms' => 1200],
]);

$server->setNode('commerce', 'order-service', 'order-1', [
    'metrics' => ['enabled' => false],
]);

$server->run();
```

### 2. Load config inside a service

```php
<?php

use Kislay\Config\Config;

Config::boot([
    'server' => 'http://127.0.0.1:9011',
    'environment' => 'prod',
    'project' => 'commerce',
    'service' => 'order-service',
    'node' => 'order-1',
    'cache_file' => '/tmp/order-service-config.json',
]);

$dbName = Config::getString('db.name');
$timeout = Config::getInt('gateway.timeout_ms', 1000);
$debug = Config::getBool('app.debug', true);
```

### 3. Add local overrides

```php
<?php

Config::loadLocal('/etc/kislay/order-service.local.json');
Config::setOverride('gateway.timeout_ms', 1500);
```

## Resolution Order

Config is merged in this order:

1. global
2. environment
3. project
4. service
5. node
6. local file override
7. environment variable override
8. runtime override

Environment variables use `KISLAY_CFG_` by default.

Example:

```bash
export KISLAY_CFG_DB__HOST=127.0.0.1
export KISLAY_CFG_LOG__LEVEL=error
```

Double underscore becomes `.` in the final config key.

## Public API

### `Kislay\Config\Config`

```php
Config::boot(array $options): bool
Config::loadLocal(string $path): bool
Config::refresh(): bool
Config::setOverride(string $key, mixed $value): bool
Config::has(string $key): bool
Config::get(string $key, mixed $default = null): mixed
Config::getString(string $key, ?string $default = null): ?string
Config::getInt(string $key, int $default = 0): int
Config::getBool(string $key, bool $default = false): bool
Config::getArray(string $key, array $default = []): array
Config::all(): array
Config::version(): string
Config::checksum(): string
```

### `Kislay\Config\Server`

```php
$server = new Kislay\Config\Server(['host' => '127.0.0.1', 'port' => 9011]);
$server->listen('0.0.0.0', 9011);
$server->setGlobal(array $config): bool;
$server->setEnvironment(string $environment, array $config): bool;
$server->setProject(string $project, array $config): bool;
$server->setService(string $project, string $service, array $config): bool;
$server->setNode(string $project, string $service, string $node, array $config): bool;
$server->resolve(?string $environment = null, ?string $project = null, ?string $service = null, ?string $node = null): array;
$server->version(): string;
$server->save(string $path): bool;
$server->load(string $path): bool;
$server->run(): bool;
$server->stop(): bool;
```

### `Kislay\Config\ConfigClient`

Compatibility client retained for simple in-memory or delegated use:

```php
$client = new Kislay\Config\ConfigClient();
$client->set('db.host', '127.0.0.1');
$client->get('db.host');
$client->all();
```

## HTTP Endpoints

The standalone server exposes:

- `GET /health`
- `GET /v1/config/version`
- `GET /v1/config/resolve?environment=prod&project=commerce&service=order-service&node=order-1`
- `PUT /v1/config/global`
- `PUT /v1/config/environments/{environment}`
- `PUT /v1/config/projects/{project}`
- `PUT /v1/config/projects/{project}/services/{service}`
- `PUT /v1/config/projects/{project}/services/{service}/nodes/{node}`

`PUT` bodies accept JSON objects. Nested objects are flattened into dotted keys inside the runtime snapshot.

## Production Notes

Current Phase 1 behavior:
- runtime refresh is explicit with `Config::refresh()`
- `cache_file` stores the last successful remote snapshot
- array values are stored as JSON and returned through `getArray()`
- `all()` returns the resolved flat dotted-key map

Not in Phase 1 yet:
- background polling thread
- auth tokens / TLS
- config revision history / rollback API
- clustered config servers

## Example

See `/tmp/kislayphp_config_phase1/example.php` for a minimal server/client walkthrough.

## License

[Apache License 2.0](LICENSE)
