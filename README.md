# KislayPHP Config

Config service client extension for KislayPHP.

## Repository

- https://github.com/KislayPHP/config

## Related Modules

- https://github.com/KislayPHP/core
- https://github.com/KislayPHP/eventbus
- https://github.com/KislayPHP/discovery
- https://github.com/KislayPHP/gateway
- https://github.com/KislayPHP/metrics
- https://github.com/KislayPHP/queue

## Build

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
