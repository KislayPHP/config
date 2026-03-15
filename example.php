<?php

if (!extension_loaded('kislayphp_config')) {
    fwrite(STDERR, "kislayphp_config extension is not loaded\n");
    exit(1);
}

$mode = $argv[1] ?? null;

if ($mode === 'server') {
    $server = new Kislay\Config\Server(['host' => '127.0.0.1', 'port' => 9011]);
    $server->setGlobal([
        'app' => ['name' => 'commerce-platform'],
        'db' => ['port' => 3306],
    ]);
    $server->setEnvironment('prod', [
        'app' => ['debug' => false],
    ]);
    $server->setProject('commerce', [
        'log' => ['level' => 'warn'],
    ]);
    $server->setService('commerce', 'order-service', [
        'db' => ['name' => 'orders'],
        'workers' => ['queues' => ['orders', 'billing']],
    ]);
    $server->setNode('commerce', 'order-service', 'order-1', [
        'metrics' => ['enabled' => false],
    ]);

    echo "Config server running on http://127.0.0.1:9011\n";
    $server->run();
    exit(0);
}

if ($mode === 'client') {
    file_put_contents(__DIR__ . '/example.local.json', json_encode([
        'db' => ['host' => '127.0.0.1'],
        'feature' => ['checkout' => true],
    ], JSON_PRETTY_PRINT));

    putenv('KISLAY_CFG_LOG__LEVEL=error');

    Kislay\Config\Config::boot([
        'server' => 'http://127.0.0.1:9011',
        'environment' => 'prod',
        'project' => 'commerce',
        'service' => 'order-service',
        'node' => 'order-1',
        'cache_file' => __DIR__ . '/example.cache.json',
        'local_file' => __DIR__ . '/example.local.json',
    ]);

    Kislay\Config\Config::setOverride('db.port', 3307);

    echo json_encode([
        'app.name' => Kislay\Config\Config::getString('app.name'),
        'app.debug' => Kislay\Config\Config::getBool('app.debug', true),
        'db.host' => Kislay\Config\Config::getString('db.host'),
        'db.port' => Kislay\Config\Config::getInt('db.port'),
        'db.name' => Kislay\Config\Config::getString('db.name'),
        'workers.queues' => Kislay\Config\Config::getArray('workers.queues'),
        'metrics.enabled' => Kislay\Config\Config::getBool('metrics.enabled', true),
        'log.level' => Kislay\Config\Config::getString('log.level'),
        'version' => Kislay\Config\Config::version(),
        'checksum' => Kislay\Config\Config::checksum(),
    ], JSON_PRETTY_PRINT), PHP_EOL;
    exit(0);
}

fwrite(STDERR, "Usage:\n");
fwrite(STDERR, "  php -d extension=modules/kislayphp_config.so example.php server\n");
fwrite(STDERR, "  php -d extension=modules/kislayphp_config.so example.php client\n");
exit(1);
