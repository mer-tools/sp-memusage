
BINS = bin/memusage
LIBS = lib/mallinfo.so

all: $(BINS) $(LIBS)

clean:
	$(RM) src/*.o *~ */*~

distclean: clean
	$(RM) $(BINS) $(LIBS)

tags:
	ctags *.c *.h

lib/mallinfo.so: src/mallinfo.c
	gcc -g -W -Wall -shared -O2 -fPIC -o $@ $^

bin/memusage: src/memusage.c
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

