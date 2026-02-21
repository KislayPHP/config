# Service Communication Guide (Config)

This extension provides runtime configuration for service-to-service calls.

## Namespace

- Primary: `Kislay\Config\ConfigClient`
- Backward compatible alias: `KislayPHP\Config\ConfigClient`

## Pattern

Store communication policy in config keys:

- `services.<name>.base_url`
- `services.<name>.timeout_ms`
- `services.<name>.retries`
- `services.<name>.circuit_breaker.threshold`

At request time, read config once and apply the policy in the caller.

## Minimal Example

See `service_communication.php` in this repository.

## Recommended Cross-Module Setup

1. Discover live endpoints with `kislayphp/discovery`.
2. Keep default endpoints and call policy in this module.
3. Track actual behavior with `kislayphp/metrics`.
4. Route external traffic through `kislayphp/gateway`.
