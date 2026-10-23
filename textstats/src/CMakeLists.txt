# config.h é regenerado a cada build para registrar a data/hora da compilação.
add_custom_target(textstats_config ALL
  COMMAND ${CMAKE_COMMAND}
          -DINPUT=${CMAKE_SOURCE_DIR}/src/config.h.in
          -DOUTPUT=${CMAKE_SOURCE_DIR}/src/config.h
          -DVERSION=${PROJECT_VERSION}
          -P ${CMAKE_SOURCE_DIR}/cmake/GenerateConfig.cmake
  COMMENT "Gerando config.h")

add_library(utf8 STATIC utf8/utf8.cpp)

add_library(textstats STATIC textstats.cpp tokenizer.cpp)
add_dependencies(textstats textstats_config)
