# KislayPHP Config Extension - Technical Reference

**Version:** 1.0  
**Author:** Kislay Technologies  
**Last Updated:** 2024

---

## Table of Contents

1. [Architecture](#architecture)
2. [Configuration Reference](#configuration-reference)
3. [API Reference](#api-reference)
4. [Usage Patterns & Recipes](#usage-patterns--recipes)
5. [Performance Notes](#performance-notes)
6. [Troubleshooting Guide](#troubleshooting-guide)

---

## Architecture

### Core Design

The KislayPHP Config extension implements a **thread-safe, strategy-based key-value configuration system** written in C/C++. The internal architecture prioritizes both performance (O(1) local reads) and flexibility (pluggable remote client backends).

#### Internal Storage Model

The internal architecture consists of:
- **Unified string storage**: All values stored internally as UTF-8 strings, with lazy type conversion at read time
- **Platform abstraction**: Unix systems use pthread_mutex_t; Windows uses CRITICAL_SECTION (both mapped to identical semantics)
- **Per-instance locking**: Each ConfigClient object has its own mutex—multiple instances do not contend for locks
- **std::unordered_map**: C++ hash table provides O(1) average-case performance for get/set operations

#### Strategy Pattern: Client Delegation

The extension uses the Strategy pattern for configuration backends:

**Delegation rules:**
1. If no client set: operations use local store directly
2. If client set via setClient(): ALL subsequent operations (set, get, remove, has, all) delegate to client
3. **Critical**: Once a client is set, there is **no fallback** to local store
4. Client replacement: Call setClient() again with a different implementation

**Why this design:** Decouples configuration source (local vs remote) while maintaining identical PHP API.

### Thread Safety & Synchronization

**Mutex scope:**
- Local store access is protected by mutex
- Client delegation calls are **not protected by mutex** (client must provide its own thread safety)
- Refresh operation acquires lock for the duration of clear + repopulate

**Lock contention:**
- Typical lock hold time: <1ms for local store operations
- No deadlock risk: single lock per object, no nested locking
- Windows: CRITICAL_SECTION → Unix: pthread_mutex_t (semantically equivalent)

---

## Configuration Reference

### Environment Variables

Configuration behavior is controlled via PHP environment variables (read at extension load time):

| Variable | Type | Default | Description |
|----------|------|---------|-------------|
| KISLAY_RPC_ENABLED | bool | false | Enable gRPC remote configuration backend |
| KISLAY_RPC_TIMEOUT_MS | long | 200 | Timeout (milliseconds) for gRPC calls to remote config service |
| KISLAY_RPC_PLATFORM_ENDPOINT | string | "127.0.0.1:9100" | gRPC endpoint address and port for platform config service |

**Example: Enable remote config with custom endpoint**

```bash
export KISLAY_RPC_ENABLED=1
export KISLAY_RPC_TIMEOUT_MS=500
export KISLAY_RPC_PLATFORM_ENDPOINT="config.internal:9100"
php script.php
```

### ClientInterface Contract

To implement a custom remote backend, implement the Kislay\Config\ClientInterface:

```php
interface ClientInterface {
    /**
     * Store a configuration key-value pair
     * @param string $key Configuration key
     * @param string $value Configuration value (must be string)
     * @return bool True on success, false on failure
     */
    public function set(string $key, string $value): bool;

    /**
     * Retrieve a configuration value
     * @param string $key Configuration key
     * @param mixed $default Default value if key not found
     * @return mixed Configuration value or default
     */
    public function get(string $key, mixed $default = null): mixed;

    /**
     * Get all configuration key-value pairs as array
     * @return array Complete configuration snapshot [key => value]
     */
    public function all(): array;
}
```

**Validation:** setClient() validates that the provided object implements ClientInterface. Type checking is strict—passing a non-compliant object throws TypeError.

---

## API Reference

### Constructor

```php
public function __construct()
```

Creates a new ConfigClient instance with:
- Empty local key-value store
- Initialized mutex (platform-specific)
- No client set (null)

**Example:**
```php
$config = new Kislay\Config\ConfigClient();
```

### setClient()

```php
public function setClient(Kislay\Config\ClientInterface $client): bool
```

**Parameters:**
- `$client`: Object implementing ClientInterface

**Returns:**
- true on success
- false if validation fails

**Behavior:**
- Stores reference to client object
- Increments reference count (PHP engine handles cleanup)
- All subsequent operations delegate to client
- No local store access once client is set

**Throws:**
- TypeError if argument does not implement ClientInterface

**Example:**
```php
class RedisConfigClient implements Kislay\Config\ClientInterface {
    private $redis;

    public function __construct($redisClient) {
        $this->redis = $redisClient;
    }

    public function set(string $key, string $value): bool {
        return $this->redis->set("config:$key", $value) !== false;
    }

    public function get(string $key, mixed $default = null): mixed {
        $value = $this->redis->get("config:$key");
        return $value !== false ? $value : $default;
    }

    public function all(): array {
        return $this->redis->hgetall('config');
    }
}

$config = new Kislay\Config\ConfigClient();
$client = new RedisConfigClient($redisConnection);
$config->setClient($client);
```

### set()

```php
public function set(string $key, string $value): bool
```

**Parameters:**
- `$key`: Configuration key (non-empty string)
- `$value`: Configuration value (must be string; numeric/bool values are NOT auto-converted)

**Returns:**
- true on success
- false if key is empty or value is not a string

**Behavior:**
- If client set: delegates to client->set()
- Otherwise: stores in local map (O(1) average case)
- Thread-safe via mutex

**Edge cases:**
- Empty string key → returns false
- Non-string value → returns false (no auto-conversion)
- Duplicate key → overwrites previous value

**Example:**
```php
$config->set('app.debug', 'true');        // Correct
$config->set('app.timeout', '30');        // Correct (string)
$config->set('app.retries', 3);           // Fails (int, not string)
$config->set('', 'value');                // Fails (empty key)
```

### get()

```php
public function get(string $key, mixed $default = null): mixed
```

**Parameters:**
- `$key`: Configuration key
- `$default`: Value to return if key not found (default: null)

**Returns:**
- String value if key exists
- `$default` if key not found or store is empty

**Behavior:**
- If client set: delegates to client->get()
- Otherwise: looks up in local map (O(1) average case)
- Thread-safe via mutex
- **No type conversion** (returns raw string)

**Example:**
```php
$config->set('database.host', 'localhost');
echo $config->get('database.host');       // "localhost"
echo $config->get('missing.key', 'default');  // "default"
echo $config->get('missing.key');         // null
```

### all()

```php
public function all(): array
```

**Returns:**
- Associative array of all key-value pairs: [key => value, ...]
- Empty array if store is empty

**Behavior:**
- If client set: delegates to client->all()
- Otherwise: returns snapshot of local map
- Thread-safe via mutex

**Example:**
```php
$config->set('db.host', 'localhost');
$config->set('db.port', '5432');
print_r($config->all());
// Output: ['db.host' => 'localhost', 'db.port' => '5432']
```

### has()

```php
public function has(string $key): bool
```

**Parameters:**
- `$key`: Configuration key

**Returns:**
- true if key exists
- false if key not found

**Behavior:**
- If client set: delegates to client->get() with sentinel default
- Otherwise: checks local map (O(1) average case)
- Thread-safe

**Example:**
```php
$config->set('feature.new_ui', 'true');
$config->has('feature.new_ui');      // true
$config->has('feature.missing');     // false
```

### remove()

```php
public function remove(string $key): bool
```

**Parameters:**
- `$key`: Configuration key

**Returns:**
- true on successful removal
- false if key not found or operation fails

**Behavior:**
- If client set: delegates to client (if client supports removal)
- Otherwise: removes from local map

**Example:**
```php
$config->set('temp.session', 'abc123');
$config->remove('temp.session');     // true
$config->remove('temp.session');     // false (already removed)
```

### refresh()

```php
public function refresh(): bool
```

**Returns:**
- true on successful refresh
- false if no client set, or if client->all() fails

**Behavior:**
1. Checks if client is set (returns false if not)
2. Calls client->all() to fetch remote snapshot
3. Validates return value is array
4. **Acquires mutex**
5. Clears local store completely
6. Populates local store from fetched array
7. **Releases mutex**
8. Returns true

**Critical properties:**
- **Atomic update**: No partial reads possible during refresh
- **Failure-safe**: If fetch or validation fails, local store unchanged
- **Snapshot consistency**: Momentary lock ensures no concurrent reads

**Example:**
```php
$config = new Kislay\Config\ConfigClient();
// ... set client ...

// Re-fetch all from remote source
if ($config->refresh()) {
    echo "Config refreshed";
} else {
    echo "Refresh failed: client not set or remote call failed";
}
```

### getInt()

```php
public function getInt(string $key, int $default = 0): int
```

**Parameters:**
- `$key`: Configuration key
- `$default`: Default integer value if key missing or conversion fails

**Returns:**
- Parsed integer value (base-10 via strtoll())
- `$default` if key missing, empty string, or non-numeric

**Behavior:**
- Retrieves value via get()
- Parses as base-10 long integer
- Handles negative numbers: "-42" → -42
- Non-numeric strings (e.g., "abc") → `$default`
- Thread-safe

**Example:**
```php
$config->set('app.timeout', '30');
$config->set('app.retries', '0');

echo $config->getInt('app.timeout');          // 30
echo $config->getInt('app.retries');          // 0
echo $config->getInt('missing', 100);         // 100
echo $config->getInt('malformed', 99);        // 99
```

### getBool()

```php
public function getBool(string $key, bool $default = false): bool
```

**Parameters:**
- `$key`: Configuration key
- `$default`: Default boolean value if key missing

**Returns:**
- true if value is one of: "1", "true" (case-insensitive), "yes", "on"
- false for all other values (including empty string, "0", "false", etc.)
- `$default` if key missing

**Behavior:**
- Truthy strings (case-insensitive): "1", "true", "TRUE", "yes", "on"
- Everything else is falsy
- Optimized for feature flags and boolean configs

**Example:**
```php
$config->set('features.beta', 'true');
$config->set('features.deprecated', 'false');
$config->set('debug', '1');

echo $config->getBool('features.beta');           // true
echo $config->getBool('features.deprecated');     // false
echo $config->getBool('debug');                   // true
echo $config->getBool('missing', true);           // true
echo $config->getBool('missing', false);          // false
```

### getArray()

```php
public function getArray(string $key, array $default = []): array
```

**Parameters:**
- `$key`: Configuration key (expected to contain JSON)
- `$default`: Default array if key missing or JSON invalid

**Returns:**
- Parsed array from JSON value
- `$default` if key missing, value is not valid JSON, or depth exceeds 512

**Behavior:**
- Retrieves value via get()
- Decodes as JSON using php_json_decode_ex() with PHP_JSON_OBJECT_AS_ARRAY
- Objects in JSON are converted to associative arrays
- Maximum nesting depth: 512 levels
- Malformed JSON → `$default` (no exception thrown)

**Example:**
```php
$config->set('app.database', '{"host":"localhost","port":5432,"ssl":true}');
$config->set('app.features', '[1,2,3]');

$db = $config->getArray('app.database');
// $db = ['host' => 'localhost', 'port' => 5432, 'ssl' => true]

$features = $config->getArray('app.features');
// $features = [1, 2, 3]

$config->getArray('missing.key', ['default' => true]);
// ['default' => true]
```

---

## Usage Patterns & Recipes

### Pattern 1: Environment-Based Configuration

Populate config from environment variables at startup:

```php
$config = new Kislay\Config\ConfigClient();

$env = [
    'APP_DEBUG' => 'debug',
    'APP_ENV' => 'environment',
    'DB_HOST' => 'database.host',
    'DB_PORT' => 'database.port',
];

foreach ($env as $envVar => $configKey) {
    if ($value = getenv($envVar)) {
        $config->set($configKey, $value);
    }
}

echo $config->get('database.host');  // From DB_HOST env var
```

### Pattern 2: Dynamic Configuration Refresh

Implement periodic refresh from remote config service:

```php
class ConfigManager {
    private $config;
    private $lastRefresh = 0;
    private $refreshInterval = 60; // seconds

    public function __construct(Kislay\Config\ClientInterface $client) {
        $this->config = new Kislay\Config\ConfigClient();
        $this->config->setClient($client);
        $this->refresh(); // Initial load
    }

    public function get(string $key, mixed $default = null): mixed {
        $now = time();
        if ($now - $this->lastRefresh > $this->refreshInterval) {
            $this->refresh();
        }
        return $this->config->get($key, $default);
    }

    private function refresh(): void {
        if ($this->config->refresh()) {
            $this->lastRefresh = time();
            error_log('Config refreshed');
        }
    }
}
```

### Pattern 3: Feature Flags Using getBool()

Manage feature toggles elegantly:

```php
$config = new Kislay\Config\ConfigClient();
// Populate from source...

class FeatureFlags {
    public function __construct(private Kislay\Config\ConfigClient $config) {}

    public function isEnabled(string $feature): bool {
        return $this->config->getBool("features.$feature");
    }

    public function enable(string $feature): bool {
        return $this->config->set("features.$feature", 'true');
    }

    public function disable(string $feature): bool {
        return $this->config->set("features.$feature", 'false');
    }
}

$flags = new FeatureFlags($config);
if ($flags->isEnabled('new_dashboard')) {
    // Show new dashboard
}
```

### Pattern 4: Multi-Source Configuration (Layered)

Combine local defaults with remote overrides:

```php
$defaults = new Kislay\Config\ConfigClient();
$defaults->set('app.timeout', '30');
$defaults->set('app.retries', '3');
$defaults->set('log.level', 'info');

$config = new Kislay\Config\ConfigClient();
$config->setClient($remoteClient);
$config->refresh();

// Merged access: remote if exists, else local default
function getConfig(string $key, mixed $default = null) use ($config, $defaults) {
    if ($config->has($key)) {
        return $config->get($key);
    }
    return $defaults->get($key, $default);
}

echo getConfig('app.timeout');  // From remote or local default
```

### Pattern 5: Type-Safe Configuration Class

Wrap config with type hints:

```php
class AppConfig {
    public function __construct(private Kislay\Config\ConfigClient $config) {}

    public function isDebug(): bool {
        return $this->config->getBool('app.debug', false);
    }

    public function getTimeout(): int {
        return $this->config->getInt('app.timeout', 30);
    }

    public function getDatabaseConfig(): array {
        return $this->config->getArray('database', [
            'host' => 'localhost',
            'port' => 5432,
        ]);
    }

    public function getEnv(): string {
        return $this->config->get('app.env', 'production');
    }
}

// Usage
$appConfig = new AppConfig($config);
if ($appConfig->isDebug()) {
    ini_set('display_errors', '1');
}
```

---

## Performance Notes

### Local Store Operations (No Client Set)

**Complexity: O(1) average case**

- get(), set(), has(), remove(): Direct hash table lookup/insert
- Lock hold time: <1ms typically
- No allocation for local reads (existing keys)

**Benchmark (typical):**
- 1M get() calls: ~50ms
- 100k set() calls: ~5ms
- No GC pressure (string values pre-allocated)

### Client Delegation

**Overhead: Client-dependent**

- Local lock: ~1 microsecond
- Client call: Network/IPC latency dominates
- Example: gRPC call to remote config → 50-200ms typical

**Mitigation:** Cache frequently-accessed keys or batch reads.

### refresh() Operation

**Complexity: O(n) where n = total keys**

- Lock held for entire operation
- Safe for typical configs (<10k keys)
- Not recommended in hot paths

**Best practice:** Call refresh() infrequently (e.g., on deployment event, not per-request).

### Memory Usage

- **Local store**: ~50 bytes per key-value pair overhead (std::string storage)
- **Mutex**: ~64 bytes (pthread_mutex_t) or ~32 bytes (Windows CRITICAL_SECTION)
- **Example**: 1000 keys approximately 50KB + strings content

---

## Troubleshooting Guide

### refresh() Returns false

**Symptom:** $config->refresh() always returns false

**Causes & Solutions:**

1. **No client set**
   ```php
   if (!$config->refresh()) {
       // Ensure setClient() was called first
       echo "Error: No client set";
   }
   ```

2. **client->all() fails**
   - Check remote service is reachable
   - Verify client implementation catches exceptions properly

3. **client->all() returns non-array**
   - Validation fails if return type is not array
   - Check client implementation returns [string => mixed] array

**Debug:**
```php
$all = $config->all(); // Should be non-empty after successful refresh
var_dump($all);
```

### getArray() Returns Default Unexpectedly

**Symptom:** getArray('key') returns default instead of parsed value

**Causes & Solutions:**

1. **Key doesn't exist**
   ```php
   if (!$config->has('key')) {
       // Key not found; default is returned
   }
   ```

2. **Invalid JSON**
   ```php
   $raw = $config->get('key');
   $decoded = json_decode($raw, true);
   if ($decoded === null && json_last_error() !== JSON_ERROR_NONE) {
       echo "Invalid JSON: " . json_last_error_msg();
   }
   ```

3. **Exceeds max depth (512)**
   ```php
   // Simplify JSON or increase extension max depth
   ```

### setClient() Throws TypeError

**Symptom:** TypeError when calling setClient() with custom client

**Cause:** Object doesn't implement Kislay\Config\ClientInterface

**Solution:**
```php
class MyClient implements Kislay\Config\ClientInterface {
    // Implement all three methods: set(), get(), all()
}
```

### Concurrent Access Issues

**Symptom:** Inconsistent values in multi-threaded environment

**Diagnosis:**
- Local store access is mutex-protected (OK)
- Client delegation is NOT mutex-protected
- Ensure your ClientInterface implementation is thread-safe

**Solution:**
```php
class ThreadSafeClient implements Kislay\Config\ClientInterface {
    private $lock;

    public function set(string $key, string $value): bool {
        lock_acquire($this->lock);
        try {
            // Actual implementation
            return true;
        } finally {
            lock_release($this->lock);
        }
    }
    // ... similar for get() and all()
}
```

---

## Summary

| Concern | Answer |
|---------|--------|
| **Thread-safe?** | Yes, local store + local reference counting. Client implementations must be thread-safe. |
| **Type conversion?** | String storage only; use getInt(), getBool(), getArray() for conversion. |
| **Remote config?** | Implement ClientInterface or use gRPC backend via KISLAY_RPC_* env vars. |
| **Performance?** | O(1) local reads; refresh O(n). Suitable for <100k keys with infrequent refresh. |
| **Backward compatible?** | Yes. Namespace aliases: KislayPHP\Config\ConfigClient, KislayPHP\Config\ClientInterface. |

---

**End of Reference**
