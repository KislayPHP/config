<?php
// Run from this folder with:
// php -d extension=modules/kislayphp_config.so example.php

extension_loaded('kislayphp_config') or die('kislayphp_config not loaded');

$cfg = new KislayPHP\Config\ConfigClient();

class ArrayConfigClient implements KislayPHP\Config\ClientInterface {
	private array $data = [];

	public function set(string $key, string $value): bool {
		$this->data[$key] = $value;
		return true;
	}

	public function get(string $key, mixed $default = null): mixed {
		return $this->data[$key] ?? $default;
	}

	public function all(): array {
		return $this->data;
	}
}

$use_client = false;
if ($use_client) {
	$cfg->setClient(new ArrayConfigClient());
}
$cfg->set('db.host', '127.0.0.1');
$cfg->set('db.port', '5432');

var_dump($cfg->get('db.host'));
print_r($cfg->all());
