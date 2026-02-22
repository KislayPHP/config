# Config Class Reference

Runtime classes exported by `kislayphp/config`.

## Namespace

- Primary: `Kislay\\Config`
- Legacy alias: `KislayPHP\\Config`

## `Kislay\\Config\\ClientInterface`

Contract for pluggable config backends.

- `set(string $key, string $value)`
  - Write a config key.
- `get(string $key, mixed $default = null)`
  - Read a config key.
- `all()`
  - Return full config map.

## `Kislay\\Config\\ConfigClient`

Config store/client with in-memory map and optional backend delegation.

### Constructor

- `__construct()`
  - Create config client.

### Backend Injection

- `setClient(ClientInterface $client)`
  - Attach external config backend.

### Config Operations

- `set(string $key, string $value)`
  - Set key value.
- `get(string $key, mixed $default = null)`
  - Read key value.
- `has(string $key)`
  - Check key existence.
- `remove(string $key)`
  - Delete key.
- `all()`
  - Return all key-values.
