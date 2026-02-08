# kislayphp_config

Config service client extension for KislayPHP.

## Build

```sh
phpize
./configure --enable-kislayphp_config
make
```

## Run Locally

```sh
cd /path/to/phpExtension/kislayphp_config
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
