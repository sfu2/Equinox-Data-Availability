add_library(wpds
  common.cpp
  dictionary.cpp
  keys.cpp
  key_source.cpp)

target_include_directories(wpds PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/ewpds)
