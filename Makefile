
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
	@mkdir -p lib
	gcc -g -W -Wall -shared -O2 -fPIC  -Wl,-soname,mallinfo.so.0 -o $@ $^

bin/mem-monitor: src/mem-monitor.c src/mem-monitor-util.c
	@mkdir -p bin
	gcc -g -W -Wall -O2 -o $@ $+

bin/mem-cpu-monitor: src/mem-cpu-monitor.c src/sp_report.c
	@mkdir -p bin
	gcc -std=c99 -g -W -Wall -O2 -o $@ $+ -lspmeasure

install:
	install -d  $(DESTDIR)/usr/bin
	cp -a bin/* $(DESTDIR)/usr/bin
	install -d  $(DESTDIR)/usr/lib
	cp -a lib/*.so  $(DESTDIR)/usr/lib
	cp -a scripts/* $(DESTDIR)/usr/bin
	install -d      $(DESTDIR)/usr/share/man/man1
	cp -a man/*.1   $(DESTDIR)/usr/share/man/man1
	install -d      $(DESTDIR)/usr/share/sp-memusage-tests/
	cp -a tests/*	$(DESTDIR)/usr/share/sp-memusage-tests/


# other docs are installed by the Debian packaging

