SRCS=\
	cube.c glew.c

INCS=\
	gettime.h\
	linmath.h

LIBS=-L/local/lib -L/local/xorg/lib -lvulkan -lm -lGL -lGLX
LIBS_XCB=-L/local/xorg/lib -lX11 -lX11-xcb -lxcb-randr -lxcb
LIBS_DISPLAY=-L/local/xorg/lib -lX11 -lX11-xcb -lxcb-randr -lxcb -ldrm
LIBS_WAYLAND=-lwayland-client

TARGETS=cube-xcb cube-display cube-wayland

GLSV=glslangValidator

SPV=cube-vert.spv cube-frag.spv

CFLAGS=-O0 -g -I/local/xorg/include -I/local/xorg/include/libdrm -I/usr/include/GL -DGLEW_STATIC

CFLAGS_DISPLAY=-DVK_USE_PLATFORM_DISPLAY_KHR -DVK_USE_PLATFORM_XLIB_XRANDR_EXT
CFLAGS_XCB=-DVK_USE_PLATFORM_XCB_KHR
CFLAGS_WAYLAND=-DVK_USE_PLATFORM_WAYLAND_KHR

all: $(TARGETS)

cube-xcb: $(SRCS) $(INCS) $(SPV)
	$(CC) $(CFLAGS) $(CFLAGS_XCB) -o $@ $(SRCS) $(LIBS) $(LIBS_XCB)

cube-display: $(SRCS) $(INCS) $(SPV)
	$(CC) $(CFLAGS) $(CFLAGS_DISPLAY) -o $@ $(SRCS) $(LIBS) $(LIBS_DISPLAY)

cube-wayland:  $(SRCS) $(INCS) $(SPV)
	$(CC) $(CFLAGS) $(CFLAGS_WAYLAND) -o $@ $(SRCS) $(LIBS) $(LIBS_WAYLAND)

cube-vert.spv: cube.vert
	$(GLSV) -V -o $@ cube.vert

cube-frag.spv: cube.frag
	$(GLSV) -V -o $@ cube.frag

clean:
	rm -f $(TARGETS) $(SPV)
