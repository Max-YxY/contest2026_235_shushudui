# Contest 2026 team 235 shushudui - camera_diag (energy label inspection)
if(CONFIG_EXAMPLES_CAMERA_DIAG)
  nuttx_add_application(
    NAME camera_diag
    SRCS camera_diag_main.c esp32p4_dsi.c esp32p4_csi.c
         esp32p4_csi_isp.c sc2336.c camera_diag_tflm_init.cc
         src/decision.c src/event_log.c src/inspection.c src/parameter_file.c
    STACKSIZE 16384
    CXXFLAGS -I${CMAKE_CURRENT_SOURCE_DIR}
             -I${CMAKE_CURRENT_SOURCE_DIR}/include)
endif()