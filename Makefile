PKG_CONFIG_LIBS = libavformat libavcodec libavutil libswscale libwebp

LDLIBS += `pkg-config --libs ${PKG_CONFIG_LIBS}`
CFLAGS += `pkg-config --libs ${PKG_CONFIG_LIBS}`

thumbnail-grid: main.o
	$(CC) -o $@ $(LDFLAGS) main.o $(LDLIBS)

clean:
	rm -f thumbnail-grid *.o
