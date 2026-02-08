PHP_ARG_ENABLE(kislayphp_config, whether to enable kislayphp_config,
[  --enable-kislayphp_config   Enable kislayphp_config support])

if test "$PHP_KISLAYPHP_CONFIG" != "no"; then
  PHP_REQUIRE_CXX()
  PHP_NEW_EXTENSION(kislayphp_config, kislayphp_config.cpp, $ext_shared)
fi
