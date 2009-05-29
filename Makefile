
BINS = bin/mem-monitor bin/mem-cpu-monitor
LIBS = lib/mallinfo.so

all: $(BINS) $(LIBS)

clean:
	$(RM) src/*.o *~ */*~ $(BINS)

distclean: clean
	$(RM) $(BINS) $(LIBS)

tags:
	ctags *.c *.h

lib/mallinfo.so: src/mallinfo.c
	gcc -g -W -Wall -shared -O2 -fPIC  -Wl,-soname,mallinfo.so.0 -o $@ $^

bin/mem-monitor: src/mem-monitor.c
	gcc -g -DTESTING -W -Wall -O2 -o $@ $^

bin/mem-cpu-monitor: src/mem-cpu-monitor.c
	gcc -g -DTESTING -W -Wall -O2 -o $@ $^

install:
	install -d  $(DESTDIR)/usr/bin
	cp -a bin/* $(DESTDIR)/usr/bin
	install -d  $(DESTDIR)/usr/lib
	cp -a lib/*.so  $(DESTDIR)/usr/lib
	cp -a scripts/* $(DESTDIR)/usr/bin
	install -d      $(DESTDIR)/usr/share/man/man1
	cp -a man/*.1   $(DESTDIR)/usr/share/man/man1

# other docs are installed by the Debian packaging

