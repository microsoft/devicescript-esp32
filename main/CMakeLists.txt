file(GLOB MAIN_FILES 
  *.c
)

idf_component_register(
  SRCS 
    "../build/version.c"
    ${MAIN_FILES}

  INCLUDE_DIRS 
    "."
)

target_link_libraries(${COMPONENT_LIB} PUBLIC jacdac)
