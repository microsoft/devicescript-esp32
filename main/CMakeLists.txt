file(GLOB JDC_FILES 
  ../jacdac-c/jacscript/*.c
  ../jacdac-c/source/*.c
  ../jacdac-c/client/*.c
  ../jacdac-c/storage/*.c
  ../jacdac-c/storage/ff/*.c
  *.c
)

idf_component_register(
  SRCS 
    "../build/version.c"
    "../jacdac-c/source/interfaces/event_queue.c"
    "../jacdac-c/source/interfaces/simple_rx.c"
    "../jacdac-c/source/interfaces/tx_queue.c"
    "../jacdac-c/services/jd_status_light.c"
    "../jacdac-c/services/power.c"
    "../jacdac-c/jacdac/dist/c/jd_spec_pack.c"
    ${JDC_FILES}

  INCLUDE_DIRS 
    "."
    "../jacdac-c/inc"
    "../jacdac-c/jacscript"
    "../jacdac-c"
)