CPPFLAGS=-W -Wall -O2
DEBUG=-g

all: png2ico

png2ico: png2ico.cpp
	g++ $(CPPFLAGS) $(DEBUG) -o $@ $< -lpng

doc/png2ico.txt: doc/png2ico.1
	man $< |sed  -e $$'s/.\b\\(.\\)/\\1/g' -e 's/\(.*\)/\1'$$'\r/' >$@

release: clean png2ico doc/png2ico.txt
	cd .. && \
	tar czf png2ico/png2ico-src.tar.gz png2ico/{LICENSE,Makefile,README,README.unix,README.win,doc/bmp.txt,doc/png2ico.1,makefile.bcc32,makezlib.bcc32,png2ico.cpp} && \
	zip png2ico/png2ico-win.zip png2ico/{LICENSE,README,doc/png2ico.txt,png2ico.exe} 

clean:
	rm -f png2ico *~ doc/*~ *.bak png2ico-src.tar.gz png2ico-win.zip doc/png2ico.txt

