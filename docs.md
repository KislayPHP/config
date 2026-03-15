# KislayPHP Config Extension Documentation

Primary namespace is `Kislay\Config` with backward-compatible aliases under `KislayPHP\Config`.

## What It Is

`kislayphp/config` provides a centralized configuration model for long-running services:
- a standalone config server for shared configuration
- a runtime client for fast in-process reads
- layered overrides for environment, project, service, and node scopes

The runtime reads from a resolved local snapshot. It does not perform a network call on every `get()`.

## Architecture

```text
               +----------------------+
               | Kislay\Config\Server |
               | port 9011            |
               +----------+-----------+
                          |
               resolved config snapshot
                          |
     +--------------------+--------------------+
     |                    |                    |
+----v-----+        +-----v----+         +-----v----+
| gateway  |        | orders   |         | payments |
| Config   |        | Config   |         | Config   |
+----------+        +----------+         +----------+
        local cache + env/local/runtime overrides
```

## Resolution Order

The resolved snapshot is built in this order:

1. global
2. environment
3. project
4. service
5. node
6. local file override
7. environment variable override
8. runtime override

Later layers overwrite earlier values.

## Installation

### PIE

```bash
pie install kislayphp/config
```

### Manual Build

```bash
phpize
./configure --enable-kislayphp_config
make
sudo make install
```

Enable the extension:

```ini
extension=kislayphp_config.so
```

## Server API

### Start a server

```php
<?php

$server = new Kislay\Config\Server(['host' => '127.0.0.1', 'port' => 9011]);
$server->setGlobal([
    'app' => ['name' => 'commerce-platform'],
    'db' => ['port' => 3306],
]);
$server->run();
```

### Scope writers

```php
$server->setGlobal(['log' => ['level' => 'info']]);
$server->setEnvironment('prod', ['app' => ['debug' => false]]);
$server->setProject('commerce', ['log' => ['level' => 'warn']]);
$server->setService('commerce', 'order-service', ['db' => ['name' => 'orders']]);
$server->setNode('commerce', 'order-service', 'order-1', ['metrics' => ['enabled' => false]]);
```

### Resolve without HTTP

```php
$config = $server->resolve('prod', 'commerce', 'order-service', 'order-1');
```

### Persist the server state

```php
$server->save('/var/lib/kislay/config.snapshot.json');
$server->load('/var/lib/kislay/config.snapshot.json');
```

## Runtime Client API

### Boot from a remote server

```php
use Kislay\Config\Config;

Config::boot([
    'server' => 'http://127.0.0.1:9011',
    'environment' => 'prod',
    'project' => 'commerce',
    'service' => 'order-service',
    'node' => 'order-1',
    'cache_file' => '/tmp/order-config-cache.json',
    'local_file' => '/etc/kislay/order.local.json',
    'env_prefix' => 'KISLAY_CFG_',
]);
```

### Read values

```php
$dbHost = Config::getString('db.host', '127.0.0.1');
$dbPort = Config::getInt('db.port', 3306);
$debug = Config::getBool('app.debug', false);
$queues = Config::getArray('workers.queues', []);
$all = Config::all();
$version = Config::version();
$checksum = Config::checksum();
```

### Refresh and runtime override

```php
Config::refresh();
Config::setOverride('gateway.timeout_ms', 1500);
```

### Local file override format

```json
{
  "db": {
    "host": "127.0.0.1"
  },
  "feature": {
    "checkout": true
  }
}
```

### Environment variable override format

```bash
export KISLAY_CFG_DB__HOST=127.0.0.1
export KISLAY_CFG_LOG__LEVEL=error
```

Final keys become:
- `db.host`
- `log.level`

## HTTP API

### Resolve config

```bash
curl 'http://127.0.0.1:9011/v1/config/resolve?environment=prod&project=commerce&service=order-service&node=order-1'
```

Example response:

```json
{
  "version": "5",
  "checksum": "5d62c5b92aa6de2b",
  "config": {
    "app.name": "commerce-platform",
    "app.debug": "false",
    "db.port": "3306",
    "db.name": "orders",
    "metrics.enabled": "false"
  }
}
```

### Update scopes remotely

```bash
curl -X PUT http://127.0.0.1:9011/v1/config/projects/commerce \
  -H 'Content-Type: application/json' \
  -d '{"log":{"level":"warn"}}'
```

## Compatibility API

`Kislay\Config\ConfigClient` remains available for lightweight in-memory use or delegated external clients:

```php
$client = new Kislay\Config\ConfigClient();
$client->set('app.name', 'demo');
$client->get('app.name');
```

## Operational Constraints

Phase 1 is intentionally limited:
- no authentication on the standalone server yet
- no background watch/poll thread yet
- no built-in rollback API yet
- no secret management layer yet
- `all()` returns a flat dotted-key map, not a nested tree

## Recommended Use Right Now

Use this release when you need:
- one config node for several services
- fast local config reads inside services
- layered overrides for environment, project, service, and node scopes
- manual refresh and file-backed cache fallback

Do not treat this release as a complete config-control plane yet.
