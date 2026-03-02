# KislayPHP Config

Configuration runtime for KislayPHP.

Primary runtime namespace is `Kislay\Config` with backward-compatible aliases under `KislayPHP\Config`.

## Concurrency Mode

- Default API mode is synchronous.
- Reads/writes return immediate values and do not use Promise/Fiber types.
- Optional RPC transport can be enabled internally, but public behavior remains synchronous.

This module is sync-first so configuration reads remain deterministic inside request lifecycles.

## Installation

```bash
pie install kislayphp/config
```

Enable in `php.ini`:

```ini
extension=kislayphp_config.so
```

## Public API

`Kislay\Config\ConfigClient`:

- `__construct()`
- `setClient(Kislay\Config\ClientInterface $client): bool`
- `set(string $key, string $value): bool`
- `get(string $key, mixed $default = null): mixed`
- `all(): array`
- `has(string $key): bool`
- `remove(string $key): bool`

`Kislay\Config\ClientInterface`:

- `set(string $key, string $value): bool`
- `get(string $key, mixed $default = null): mixed`
- `all(): array`

Legacy aliases:

- `KislayPHP\Config\ConfigClient`
- `KislayPHP\Config\ClientInterface`

## Quick Start

```php
<?php

$config = new Kislay\Config\ConfigClient();

$config->set('app.name', 'kislay-service');
$config->set('app.env', 'dev');

var_dump($config->get('app.name'));
var_dump($config->has('app.env'));
var_dump($config->all());
```

## Notes

- Recommended merge order at framework level: `env > local > remote > defaults`.
- Use a custom client implementation via `setClient()` for remote config backends.
- For service communication policy, see `SERVICE_COMMUNICATION.md`.

