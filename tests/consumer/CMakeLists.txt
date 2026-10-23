# Projeto CONSUMIDOR do avaliador: usa a textstats INSTALADA, como faria outra equipe.
# Repare no que ele NÃO faz: não escolhe padrão de C++, não adiciona include dirs,
# não liga warnings e não sabe nada sobre como a biblioteca foi construída.
cmake_minimum_required(VERSION 3.25)
project(GqConsumer LANGUAGES CXX)

option(GQ_NOISY "Adiciona um arquivo que gera warnings (só warnings, não erros)" OFF)
option(GQ_PROBES "Adiciona as sondas de C++23 e de headers privados" OFF)

find_package(TextStats 1.4 CONFIG REQUIRED)

get_target_property(gq_type TextStats::textstats TYPE)
message(STATUS "GQ_TARGET_TYPE=${gq_type}")
message(STATUS "GQ_PACKAGE_VERSION=${TextStats_VERSION}")

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE TextStats::textstats)
if(GQ_NOISY)
  target_sources(consumer PRIVATE noisy.cpp)
endif()

if(GQ_PROBES)
  # Quem usa a biblioteca pode querer um padrão MAIS novo que o mínimo dela.
  add_executable(consumer_cxx23 cxx23.cpp)
  target_link_libraries(consumer_cxx23 PRIVATE TextStats::textstats)
  set_target_properties(consumer_cxx23 PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)

  # Headers internos da biblioteca não podem estar ao alcance do consumidor.
  add_executable(probe_private probe_private.cpp)
  target_link_libraries(probe_private PRIVATE TextStats::textstats)
endif()
