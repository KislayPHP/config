<?php
// Run from this folder with:
// php -d extension=modules/kislayphp_config.so example.php

function fail(string $message): void {
	echo "FAIL: {$message}\n";
	exit(1);
}

if (!extension_loaded('kislayphp_config')) {
	fail('kislayphp_config not loaded');
}

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

$cfg->setClient(new ArrayConfigClient());

$cfg->set('db.host', '127.0.0.1');
$cfg->set('db.port', '5432');

$host = $cfg->get('db.host');
if ($host !== '127.0.0.1') {
	fail('db.host mismatch');
}

$all = $cfg->all();
if (!is_array($all) || ($all['db.host'] ?? null) !== '127.0.0.1') {
	fail('all() missing db.host');
}

echo "OK: config example passed\n";
