SRCS=\
	cube.c

OBJS=$(SRCS:.c=.o)

INCS=\
	gettime.h\
	linmath.h

LIBS=-L/local/xorg/lib -lvulkan -lxcb -lm

TARGET=cube

GLSV=glslangValidator

SPV=cube-vert.spv cube-frag.spv

CFLAGS=-O0 -g -DVK_USE_PLATFORM_XCB_KHR -DVK_USE_PLATFORM_KMS_KEITHP -I/local/xorg/include -I/local/xorg/include/libdrm

all: $(TARGET) $(SPV)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

$(OBJS): $(INCS)

cube-vert.spv: cube.vert
	$(GLSV) -V -o $@ cube.vert

cube-frag.spv: cube.frag
	$(GLSV) -V -o $@ cube.frag

clean:
	rm -f $(TARGET) $(OBJS) $(SPV)
