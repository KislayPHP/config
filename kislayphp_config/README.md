# KislayConfig

> Synchronous configuration runtime for KislayPHP — get, set, and refresh key-value config with typed accessors and pluggable remote backends.

[![PHP Version](https://img.shields.io/badge/PHP-8.2+-blue.svg)](https://php.net)
[![License](https://img.shields.io/badge/License-Apache%202.0-green.svg)](LICENSE)

## Installation

**Via PIE (recommended):**
```bash
pie install kislayphp/config
```

Add to `php.ini`:
```ini
extension=kislayphp_config.so
```

**Build from source:**
```bash
git clone https://github.com/KislayPHP/config.git
cd config && phpize && ./configure --enable-kislayphp_config && make && sudo make install
```

## Requirements

- PHP 8.2+

## Quick Start

```php
<?php
$config = new Kislay\Config\ConfigClient();

$config->set('app.name', 'my-service');
$config->set('app.env',  'production');
$config->set('db.pool',  '10');

echo $config->get('app.name');          // 'my-service'
echo $config->getInt('db.pool');        // 10
echo $config->getBool('feature.flag');  // false (default)

var_dump($config->all());
```

## API Reference

### `ConfigClient`

#### `__construct()`
Creates a new in-process config store. Use `setClient()` to back it with a remote config service.

#### `setClient(Kislay\Config\ClientInterface $client): bool`
Delegates all config operations to a remote client (Consul, etcd, custom HTTP service, etc.).

#### `set(string $key, string $value): bool`
Stores a string value under `$key`. Keys support dot notation for namespacing.
- Returns `true` on success

```php
$config->set('server.port', '8080');
```

#### `get(string $key, mixed $default = null): mixed`
Returns the value for `$key`, or `$default` if not set.

#### `has(string $key): bool`
Returns `true` if `$key` exists in the store.

#### `all(): array`
Returns all key-value pairs as an associative array.

#### `remove(string $key): bool`
Deletes a key from the store. Returns `true` if the key existed.

#### `refresh(): bool`
Re-fetches all values from the remote client (no-op for in-process store).
Use after a deployment to pick up updated remote config.

#### `getInt(string $key, int $default = 0): int`
Returns the value cast to `int`. Returns `$default` if the key is absent or non-numeric.

#### `getBool(string $key, bool $default = false): bool`
Returns the value as `bool`. Truthy strings: `'1'`, `'true'`, `'yes'`, `'on'`.

#### `getArray(string $key, array $default = []): array`
Returns the value JSON-decoded as an array. Returns `$default` if absent or not valid JSON.

---

### `ClientInterface`

| Method | Signature | Description |
|--------|-----------|-------------|
| `set` | `set(string $key, string $value): bool` | Store a value |
| `get` | `get(string $key, mixed $default = null): mixed` | Fetch a value |
| `all` | `all(): array` | Fetch all key-value pairs |

## Configuration

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `KISLAY_CONFIG_REMOTE_URL` | `` | Remote config service base URL (if using built-in HTTP client) |
| `KISLAY_RPC_ENABLED` | `0` | Enable RPC transport for remote config |
| `KISLAY_RPC_TIMEOUT_MS` | `200` | RPC call timeout in ms |

### Recommended Merge Order

```
environment variables > local file > remote config service > hard-coded defaults
```

Use `setClient()` for remote layers, and pre-populate the local store from env/files before the server starts.

## Examples

### Bootstrap from Environment

```php
<?php
$config = new Kislay\Config\ConfigClient();

// Seed from environment at startup
$config->set('db.host', getenv('DB_HOST') ?: 'localhost');
$config->set('db.port', getenv('DB_PORT') ?: '3306');
$config->set('db.name', getenv('DB_NAME') ?: 'app');

// Use typed accessors
$port = $config->getInt('db.port');   // 3306
$host = $config->get('db.host');      // 'localhost'
```

### Remote Config Backend

```php
<?php
class ConsulConfigClient implements Kislay\Config\ClientInterface {
    public function set(string $key, string $value): bool {
        // PUT to Consul KV
    }
    public function get(string $key, mixed $default = null): mixed {
        // GET from Consul KV
    }
    public function all(): array {
        // LIST from Consul KV
    }
}

$config = new Kislay\Config\ConfigClient();
$config->setClient(new ConsulConfigClient());

// Force refresh after deploy
$config->refresh();
```

### Config in Request Handler

```php
$app->get('/config/reload', function ($req, $res) use ($config) {
    $config->refresh();
    $res->json(['status' => 'refreshed']);
});
```

### Feature Flags

```php
$config->set('feature.new_checkout', 'true');

if ($config->getBool('feature.new_checkout')) {
    // new path
}
```

## Related Extensions

| Extension | Use Case |
|-----------|----------|
| [kislayphp/core](https://github.com/KislayPHP/core) | HTTP server where config is consumed |
| [kislayphp/persistence](https://github.com/KislayPHP/persistence) | Uses config for DB connection parameters |
| [kislayphp/metrics](https://github.com/KislayPHP/metrics) | Track config refresh counts |

## License

Licensed under the [Apache License 2.0](LICENSE).
