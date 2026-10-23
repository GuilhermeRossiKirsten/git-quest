# Gera o config.h da textstats com a versão e a data/hora do build.
# Roda a cada build (alvo "textstats_config" em src/CMakeLists.txt):
#
#   cmake -DINPUT=<config.h.in> -DOUTPUT=<config.h> -DVERSION=<x.y.z> -P GenerateConfig.cmake
#
# Assim o "wordfreq --version" sempre mostra quando o binário foi compilado.
string(TIMESTAMP TEXTSTATS_BUILD_STAMP "%Y-%m-%d %H:%M:%S" UTC)
set(TEXTSTATS_VERSION "${VERSION}")
configure_file("${INPUT}" "${OUTPUT}" @ONLY)
