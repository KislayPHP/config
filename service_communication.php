<?php
// php -d extension=modules/kislayphp_config.so service_communication.php

if (!extension_loaded('kislayphp_config')) {
    fwrite(STDERR, "kislayphp_config extension is not loaded\n");
    exit(1);
}

$cfg = new Kislay\Config\ConfigClient();

$cfg->set('services.inventory.base_url', 'http://127.0.0.1:9101');
$cfg->set('services.inventory.timeout_ms', '1500');
$cfg->set('services.inventory.retries', '2');
$cfg->set('services.payment.base_url', 'http://127.0.0.1:9102');
$cfg->set('services.payment.timeout_ms', '1200');
$cfg->set('services.payment.retries', '1');

$inventoryPolicy = [
    'base_url' => $cfg->get('services.inventory.base_url', ''),
    'timeout_ms' => (int)$cfg->get('services.inventory.timeout_ms', '1000'),
    'retries' => (int)$cfg->get('services.inventory.retries', '1'),
];

$paymentPolicy = [
    'base_url' => $cfg->get('services.payment.base_url', ''),
    'timeout_ms' => (int)$cfg->get('services.payment.timeout_ms', '1000'),
    'retries' => (int)$cfg->get('services.payment.retries', '1'),
];

echo json_encode([
    'inventory' => $inventoryPolicy,
    'payment' => $paymentPolicy,
], JSON_PRETTY_PRINT) . PHP_EOL;
