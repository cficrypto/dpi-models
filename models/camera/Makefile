DPI_MODELS += camera

camera_SRCS = camera/camera.cpp

ifndef MAGICK
  MAGICK=$(shell pkg-config --exists GraphicsMagick --atleast-version=1.3.23 || echo FAILED)
endif

ifeq '$(MAGICK)' ''
camera_CFLAGS += $(shell pkg-config GraphicsMagick --cflags)
camera_CFLAGS += -D__MAGICK__
camera_LDFLAGS += -lGraphicsMagick++ -lGraphicsMagick
#LDFLAGS += $(shell Magick++-config --ldflags)
endif
