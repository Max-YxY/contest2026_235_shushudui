############################################################################
# Contest 2026 team 235 shushudui - camera_diag (energy label inspection)
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = camera_diag
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = $(CONFIG_EXAMPLES_CAMERA_DIAG_STACKSIZE)

# C sources
CSRCS  = camera_diag_main.c
CSRCS += esp32p4_dsi.c
CSRCS += esp32p4_csi.c
CSRCS += esp32p4_csi_isp.c
CSRCS += sc2336.c
CSRCS += $(wildcard src/*.c)

# C++ sources (TFLite Micro)
CXXSRCS = camera_diag_tflm_init.cc

MAINSRC = camera_diag_main.c

# Include paths
CFLAGS   += -I$(APPDIR)/examples/camera_diag
CFLAGS   += -I$(APPDIR)/examples/camera_diag/include
CXXFLAGS += -I$(APPDIR)/examples/camera_diag
CXXFLAGS += -I$(APPDIR)/examples/camera_diag/include

# TFLite Micro include paths (adjust to your NuttX tree)
CXXFLAGS += -I$(APPDIR)/mlearning/tflite-micro/tflite-micro
CXXFLAGS += -I$(APPDIR)/mlearning/tflite-micro/tflite-micro/tensorflow/lite
CXXFLAGS += -I$(APPDIR)/mlearning/tflite-micro/tflite-micro/tensorflow/lite/micro
CXXFLAGS += -I$(APPDIR)/mlearning/tflite-micro/tflite-micro/third_party/flatbuffers/include
CXXFLAGS += -I$(APPDIR)/mlearning/tflite-micro/tflite-micro/third_party/gemmlowp

MODULE = $(CONFIG_EXAMPLES_CAMERA_DIAG)

include $(APPDIR)/Application.mk