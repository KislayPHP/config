# KislayPHP Config Extension Documentation

## Overview

The KislayPHP Config extension provides a unified configuration management system for PHP applications. It supports both local configuration storage and external configuration servers, with features like hot-reloading, environment variable overrides, and pluggable client interfaces.

## Architecture

### Configuration Sources (in order of precedence)
1. **Environment Variables** - Highest priority
2. **External Config Server** - Via client interface
3. **Local In-Memory Storage** - Default fallback

### Client Interface Pattern
The extension uses a client interface pattern allowing different configuration backends:
- HTTP-based config servers
- KV store
- Database
- Custom implementations

## Installation

### Via PIE
```bash
pie install kislayphp/config
```

### Manual Build
```bash
cd kislayphp_config/
phpize && ./configure --enable-kislayphp_config && make && make install
```

### php.ini Configuration
```ini
extension=kislayphp_config.so
```

## API Reference

### KislayPHP\\Config\\Config Class

The main configuration management class.

#### Constructor
```php
$config = new KislayPHP\\Config\\Config();
```

#### Client Management
```php
$config->setClient(KislayPHP\\Config\\ClientInterface $client): bool
```

Sets a configuration client for external config sources.

#### Configuration Access
```php
$config->get(string $key, mixed $default = null): mixed
$config->set(string $key, mixed $value): void
$config->has(string $key): bool
```

#### Bulk Operations
```php
$config->all(): array
$config->only(array $keys): array
$config->except(array $keys): array
```

### KislayPHP\\Config\\ClientInterface

Interface for configuration clients.

```php
interface ClientInterface {
    public function get(string $key, mixed $default = null): mixed;
    public function set(string $key, mixed $value): bool;
    public function has(string $key): bool;
    public function all(): array;
}
```

## Usage Examples

### Basic Configuration
```php
<?php
use KislayPHP\\Config\\Config;

$config = new Config();

// Set configuration values
$config->set('app.name', 'MyApp');
$config->set('app.version', '1.0.0');
$config->set('database.host', 'localhost');
$config->set('database.port', 3306);

// Get configuration values
$appName = $config->get('app.name');
$dbHost = $config->get('database.host', '127.0.0.1');
$debug = $config->get('app.debug', false);

// Check if key exists
if ($config->has('database.password')) {
    $password = $config->get('database.password');
}
```

### Environment Variable Integration
```php
<?php
$config = new KislayPHP\\Config\\Config();

// Environment variables override config values
// Set APP_NAME=ProductionApp in environment
$appName = $config->get('app.name', 'DefaultApp');
// Returns 'ProductionApp' if APP_NAME is set, otherwise 'DefaultApp'

// Nested keys with environment variables
// Set DATABASE_HOST=prod-db.example.com
$dbHost = $config->get('database.host', 'localhost');
// Returns 'prod-db.example.com' if DATABASE_HOST is set
```

### External Config Server
```php
<?php
use KislayPHP\\Config\\Config;
use KislayPHP\\Config\\HttpClient;

$config = new Config();

// Use HTTP-based config server
$client = new HttpClient('http://config-server:8080');
$config->setClient($client);

// Configuration is now fetched from the server
$appConfig = $config->get('app');
$features = $config->get('features.enabled', []);
```

### Hot-Reload Configuration
```php
<?php
$config = new Config();
$config->setClient(new HttpClient('http://config-server:8080'));

// Configuration automatically reloads on each request
$app->get('/api/config', function($req, $res) use ($config) {
    $currentConfig = $config->all();
    $res->json($currentConfig);
});

// Force refresh (if client supports it)
$app->post('/api/config/refresh', function($req, $res) use ($config) {
    // Implementation depends on client
    $config->refresh();
    $res->json(['status' => 'refreshed']);
});
```

### Configuration Validation
```php
<?php
class ValidatedConfig {
    private $config;

    public function __construct(Config $config) {
        $this->config = $config;
    }

    public function getDatabaseConfig(): array {
        $host = $this->config->get('database.host');
        $port = $this->config->get('database.port', 3306);
        $name = $this->config->get('database.name');

        if (!$host || !$name) {
            throw new InvalidArgumentException('Database configuration incomplete');
        }

        if (!is_numeric($port) || $port < 1 || $port > 65535) {
            throw new InvalidArgumentException('Invalid database port');
        }

        return [
            'host' => $host,
            'port' => (int) $port,
            'name' => $name,
            'user' => $this->config->get('database.user'),
            'password' => $this->config->get('database.password')
        ];
    }
}

// Usage
$validatedConfig = new ValidatedConfig($config);
$dbConfig = $validatedConfig->getDatabaseConfig();
```

## Client Implementations

### HTTP Client
```php
<?php
class HttpClient implements KislayPHP\\Config\\ClientInterface {
    private $baseUrl;
    private $httpClient;

    public function __construct(string $baseUrl) {
        $this->baseUrl = rtrim($baseUrl, '/');
        $this->httpClient = new GuzzleHttp\\Client();
    }

    public function get(string $key, $default = null) {
        try {
            $response = $this->httpClient->get($this->baseUrl . '/config/' . $key);
            $data = json_decode($response->getBody(), true);
            return $data['value'] ?? $default;
        } catch (Exception $e) {
            return $default;
        }
    }

    public function set(string $key, $value): bool {
        try {
            $this->httpClient->put($this->baseUrl . '/config/' . $key, [
                'json' => ['value' => $value]
            ]);
            return true;
        } catch (Exception $e) {
            return false;
        }
    }

    public function has(string $key): bool {
        try {
            $this->httpClient->head($this->baseUrl . '/config/' . $key);
            return true;
        } catch (Exception $e) {
            return false;
        }
    }

    public function all(): array {
        try {
            $response = $this->httpClient->get($this->baseUrl . '/config');
            return json_decode($response->getBody(), true);
        } catch (Exception $e) {
            return [];
        }
    }
}
```

### KV store Client
```php
<?php
class KeyValueStoreClient implements KislayPHP\\Config\\ClientInterface {
    private $KV store;
    private $prefix;

    public function __construct(string $host = 'localhost', int $port = 6379, string $prefix = 'config:') {
        $this->KV store = new KV store();
        $this->KV store->connect($host, $port);
        $this->prefix = $prefix;
    }

    public function get(string $key, $default = null) {
        $value = $this->KV store->get($this->prefix . $key);
        return $value !== false ? json_decode($value, true) : $default;
    }

    public function set(string $key, $value): bool {
        return $this->KV store->set($this->prefix . $key, json_encode($value));
    }

    public function has(string $key): bool {
        return $this->KV store->exists($this->prefix . $key);
    }

    public function all(): array {
        $keys = $this->KV store->keys($this->prefix . '*');
        $config = [];

        foreach ($keys as $key) {
            $configKey = str_replace($this->prefix, '', $key);
            $config[$configKey] = json_decode($this->KV store->get($key), true);
        }

        return $config;
    }
}
```

### Database Client
```php
<?php
class DatabaseClient implements KislayPHP\\Config\\ClientInterface {
    private $pdo;
    private $table;

    public function __construct(PDO $pdo, string $table = 'config') {
        $this->pdo = $pdo;
        $this->table = $table;
    }

    public function get(string $key, $default = null) {
        $stmt = $this->pdo->prepare("SELECT value FROM {$this->table} WHERE `key` = ?");
        $stmt->execute([$key]);
        $result = $stmt->fetch(PDO::FETCH_ASSOC);

        if ($result) {
            return json_decode($result['value'], true);
        }

        return $default;
    }

    public function set(string $key, $value): bool {
        $jsonValue = json_encode($value);

        $stmt = $this->pdo->prepare("
            INSERT INTO {$this->table} (`key`, value, updated_at)
            VALUES (?, ?, NOW())
            ON DUPLICATE KEY UPDATE value = VALUES(value), updated_at = NOW()
        ");

        return $stmt->execute([$key, $jsonValue]);
    }

    public function has(string $key): bool {
        $stmt = $this->pdo->prepare("SELECT 1 FROM {$this->table} WHERE `key` = ?");
        $stmt->execute([$key]);
        return $stmt->fetch() !== false;
    }

    public function all(): array {
        $stmt = $this->pdo->query("SELECT `key`, value FROM {$this->table}");
        $config = [];

        while ($row = $stmt->fetch(PDO::FETCH_ASSOC)) {
            $config[$row['key']] = json_decode($row['value'], true);
        }

        return $config;
    }
}

// Database schema
/*
CREATE TABLE config (
    `key` VARCHAR(255) PRIMARY KEY,
    value JSON NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);
*/
```

## Advanced Usage

### Configuration Namespaces
```php
<?php
class NamespacedConfig {
    private $config;
    private $namespace;

    public function __construct(Config $config, string $namespace) {
        $this->config = $config;
        $this->namespace = $namespace;
    }

    public function get(string $key, $default = null) {
        return $this->config->get($this->namespace . '.' . $key, $default);
    }

    public function set(string $key, $value): void {
        $this->config->set($this->namespace . '.' . $key, $value);
    }
}

// Usage
$apiConfig = new NamespacedConfig($config, 'api');
$rateLimit = $apiConfig->get('rate_limit', 100);

$dbConfig = new NamespacedConfig($config, 'database');
$host = $dbConfig->get('host', 'localhost');
```

### Configuration Caching
```php
<?php
class CachedConfig extends Config {
    private $cache = [];
    private $cacheExpiry = [];

    public function get(string $key, $default = null) {
        // Check cache first
        if (isset($this->cache[$key]) &&
            (!isset($this->cacheExpiry[$key]) || $this->cacheExpiry[$key] > time())) {
            return $this->cache[$key];
        }

        // Fetch from parent and cache
        $value = parent::get($key, $default);
        $this->cache[$key] = $value;
        $this->cacheExpiry[$key] = time() + 300; // Cache for 5 minutes

        return $value;
    }

    public function invalidateCache(string $key = null): void {
        if ($key === null) {
            $this->cache = [];
            $this->cacheExpiry = [];
        } else {
            unset($this->cache[$key], $this->cacheExpiry[$key]);
        }
    }
}
```

### Configuration Encryption
```php
<?php
class EncryptedConfig extends Config {
    private $encryptionKey;

    public function __construct(string $encryptionKey) {
        $this->encryptionKey = $encryptionKey;
        parent::__construct();
    }

    public function get(string $key, $default = null) {
        $encrypted = parent::get($key, null);
        if ($encrypted === null) {
            return $default;
        }

        return $this->decrypt($encrypted);
    }

    public function set(string $key, $value): void {
        $encrypted = $this->encrypt($value);
        parent::set($key, $encrypted);
    }

    private function encrypt($value): string {
        $json = json_encode($value);
        // Use crypto_encrypt or similar
        return crypto_encrypt($json, 'aes-256-cbc', $this->encryptionKey, 0, $this->getIv());
    }

    private function decrypt(string $encrypted): mixed {
        $decrypted = crypto_decrypt($encrypted, 'aes-256-cbc', $this->encryptionKey, 0, $this->getIv());
        return json_decode($decrypted, true);
    }

    private function getIv(): string {
        return substr(hash('sha256', $this->encryptionKey), 0, 16);
    }
}
```

### Configuration Validation
```php
<?php
class ValidatingConfig extends Config {
    private $validators = [];

    public function addValidator(string $key, callable $validator): void {
        $this->validators[$key] = $validator;
    }

    public function set(string $key, $value): void {
        if (isset($this->validators[$key])) {
            $validator = $this->validators[$key];
            if (!$validator($value)) {
                throw new InvalidArgumentException("Invalid value for config key: $key");
            }
        }

        parent::set($key, $value);
    }

    // Predefined validators
    public static function string(): callable {
        return function($value) { return is_string($value); };
    }

    public static function int(): callable {
        return function($value) { return is_int($value); };
    }

    public static function url(): callable {
        return function($value) {
            return is_string($value) && filter_var($value, FILTER_VALIDATE_URL);
        };
    }

    public static function oneOf(array $allowed): callable {
        return function($value) use ($allowed) {
            return in_array($value, $allowed, true);
        };
    }
}

// Usage
$config = new ValidatingConfig();
$config->addValidator('database.host', ValidatingConfig::string());
$config->addValidator('database.port', ValidatingConfig::int());
$config->addValidator('app.env', ValidatingConfig::oneOf(['development', 'staging', 'production']));

$config->set('database.host', 'localhost'); // OK
$config->set('database.port', 3306); // OK
$config->set('app.env', 'production'); // OK
$config->set('app.env', 'invalid'); // Throws exception
```

## Integration Examples

### KislayPHP Integration
```php
<?php
// config/kislay.php
return [
    'client' => env('KISLAY_CLIENT', 'http'),
    'server_url' => env('KISLAY_SERVER_URL', 'http://config-server:8080'),
];

// In service provider
use Illuminate\\Support\\ServiceProvider;
use KislayPHP\\Config\\Config;
use KislayPHP\\Config\\HttpClient;

class KislayServiceProvider extends ServiceProvider {
    public function register() {
        $this->app->singleton(Config::class, function($app) {
            $config = new Config();

            $clientType = config('kislay.client');
            if ($clientType === 'http') {
                $client = new HttpClient(config('kislay.server_url'));
                $config->setClient($client);
            }

            return $config;
        });
    }
}

// Usage in controllers
class UserController extends Controller {
    public function index(Config $config) {
        $usersPerPage = $config->get('pagination.users_per_page', 15);
        // ... rest of controller logic
    }
}
```

### Framework Integration
```php
<?php
// src/Service/KislayConfig.php
namespace App\\Service;

use KislayPHP\\Config\\Config;
use KislayPHP\\Config\\HttpClient;

class KislayConfig extends Config {
    public function __construct(string $serverUrl = null) {
        parent::__construct();

        if ($serverUrl) {
            $client = new HttpClient($serverUrl);
            $this->setClient($client);
        }
    }
}

// services.yaml
services:
    App\\Service\\KislayConfig:
        arguments:
            $serverUrl: '%env(KISLAY_SERVER_URL)%'

// Usage in controllers
class DefaultController extends AbstractController {
    public function index(KislayConfig $config) {
        $features = $config->get('features.enabled', []);
        // ... controller logic
    }
}
```

### Microservice Configuration
```php
<?php
// Microservice configuration loader
class MicroserviceConfig {
    private $config;
    private $serviceName;

    public function __construct(string $serviceName) {
        $this->serviceName = $serviceName;
        $this->config = new Config();

        // Try to connect to config server
        $configServer = getenv('CONFIG_SERVER_URL') ?: 'http://config-server:8080';
        try {
            $client = new HttpClient($configServer);
            $this->config->setClient($client);
        } catch (Exception $e) {
            // Fallback to local config
            $this->loadLocalConfig();
        }
    }

    private function loadLocalConfig(): void {
        // Load from local config file
        $configFile = __DIR__ . '/../config/' . $this->serviceName . '.json';
        if (file_exists($configFile)) {
            $localConfig = json_decode(file_get_contents($configFile), true);
            foreach ($localConfig as $key => $value) {
                $this->config->set($key, $value);
            }
        }
    }

    public function get(string $key, $default = null) {
        return $this->config->get($key, $default);
    }

    public function getServiceConfig(): array {
        return $this->config->get($this->serviceName, []);
    }
}

// Usage
$config = new MicroserviceConfig('user-service');
$databaseUrl = $config->get('database.url');
$serviceConfig = $config->getServiceConfig();
```

## Testing

### Unit Testing
```php
<?php
use PHPUnit\\Framework\\TestCase;
use KislayPHP\\Config\\Config;

class ConfigTest extends TestCase {
    private $config;

    protected function setUp(): void {
        $this->config = new Config();
    }

    public function testSetAndGet() {
        $this->config->set('test.key', 'test_value');
        $this->assertEquals('test_value', $this->config->get('test.key'));
    }

    public function testDefaultValues() {
        $this->assertEquals('default', $this->config->get('nonexistent.key', 'default'));
    }

    public function testHasKey() {
        $this->config->set('existing.key', 'value');
        $this->assertTrue($this->config->has('existing.key'));
        $this->assertFalse($this->config->has('nonexistent.key'));
    }

    public function testEnvironmentOverride() {
        putenv('TEST_CONFIG_KEY=test_env_value');
        $this->config->set('test.config.key', 'config_value');

        // Environment variable should take precedence
        $this->assertEquals('test_env_value', $this->config->get('test.config.key'));

        putenv('TEST_CONFIG_KEY'); // Clean up
    }
}
```

### Mock Client for Testing
```php
<?php
class MockConfigClient implements KislayPHP\\Config\\ClientInterface {
    private $data = [];

    public function get(string $key, $default = null) {
        return $this->data[$key] ?? $default;
    }

    public function set(string $key, $value): bool {
        $this->data[$key] = $value;
        return true;
    }

    public function has(string $key): bool {
        return isset($this->data[$key]);
    }

    public function all(): array {
        return $this->data;
    }

    public function setData(array $data): void {
        $this->data = $data;
    }
}

// Usage in tests
class ServiceTest extends TestCase {
    public function testServiceUsesConfig() {
        $mockClient = new MockConfigClient();
        $mockClient->setData([
            'service.timeout' => 30,
            'service.retries' => 3
        ]);

        $config = new Config();
        $config->setClient($mockClient);

        $service = new MyService($config);

        $this->assertEquals(30, $service->getTimeout());
        $this->assertEquals(3, $service->getRetries());
    }
}
```

## Troubleshooting

### Common Issues

#### Client Connection Failures
**Symptoms:** Configuration values not loading from external server

**Solutions:**
1. Check network connectivity to config server
2. Verify server URL and authentication
3. Implement retry logic with exponential backoff
4. Add fallback to local configuration

#### Environment Variable Precedence Issues
**Symptoms:** Configuration not using expected values

**Solutions:**
1. Check environment variable naming (should be uppercase with underscores)
2. Verify variable is set in correct shell/session
3. Use `getenv()` to debug environment variables
4. Consider using `.env` files for local development

#### Memory Issues with Large Configurations
**Symptoms:** High memory usage with large config datasets

**Solutions:**
1. Implement lazy loading for config values
2. Use caching with TTL for frequently accessed values
3. Consider pagination for large config datasets
4. Profile memory usage with config operations

### Debug Logging
```php
<?php
class DebugConfig extends Config {
    public function get(string $key, $default = null) {
        $value = parent::get($key, $default);

        error_log(sprintf(
            'Config access: %s = %s (from %s)',
            $key,
            json_encode($value),
            $this->getSource($key)
        ));

        return $value;
    }

    private function getSource(string $key): string {
        $envKey = strtoupper(str_replace('.', '_', $key));
        if (getenv($envKey) !== false) {
            return 'environment';
        }

        if ($this->hasClient() && $this->getClient()->has($key)) {
            return 'client';
        }

        return 'local';
    }
}
```

### Performance Monitoring
```php
<?php
class MonitoredConfig extends Config {
    private $accessCount = [];
    private $accessTime = [];

    public function get(string $key, $default = null) {
        $start = microtime(true);
        $value = parent::get($key, $default);
        $duration = microtime(true) - $start;

        $this->accessCount[$key] = ($this->accessCount[$key] ?? 0) + 1;
        $this->accessTime[$key] = ($this->accessTime[$key] ?? 0) + $duration;

        return $value;
    }

    public function getStats(): array {
        $stats = [];
        foreach ($this->accessCount as $key => $count) {
            $stats[$key] = [
                'access_count' => $count,
                'total_time' => $this->accessTime[$key],
                'avg_time' => $this->accessTime[$key] / $count
            ];
        }
        return $stats;
    }
}

// Usage
$config = new MonitoredConfig();
// ... use config ...
$stats = $config->getStats();
print_r($stats);
```

## Best Practices

### Configuration Organization
1. **Use hierarchical keys**: `database.host`, `database.port`, `api.rate_limit`
2. **Group related settings**: `cache.kv_store.host`, `cache.kv_store.port`, `cache.kv_store.ttl`
3. **Use consistent naming**: snake_case for keys, camelCase for values when appropriate
4. **Document all configuration keys** in your application documentation

### Security Considerations
1. **Never store secrets in code** - always use environment variables or secure config servers
2. **Encrypt sensitive configuration** values at rest
3. **Use HTTPS for config server** communications
4. **Implement proper authentication** for config server access
5. **Audit configuration access** in production environments

### Performance Optimization
1. **Cache frequently accessed** configuration values
2. **Use connection pooling** for config server clients
3. **Implement circuit breakers** for config server failures
4. **Lazy load configuration** sections as needed
5. **Monitor configuration access** patterns and performance

### Error Handling
1. **Always provide sensible defaults** for configuration values
2. **Implement graceful degradation** when config server is unavailable
3. **Log configuration errors** without exposing sensitive information
4. **Validate configuration values** on load
5. **Handle network timeouts** appropriately

This comprehensive documentation covers all aspects of the KislayPHP Config extension, from basic usage to advanced implementations and best practices.