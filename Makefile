CPPFLAGS=-W -Wall -O2 -finline-functions
#CPPFLAGS=-O0 -W -Wall
DEBUG=-g

all: png2ico

png2ico: png2ico.cpp
	g++ $(CPPFLAGS) $(DEBUG) -o $@ $< -lpng -lz -lm

doc/png2ico.txt: doc/png2ico.1
	man $< |sed  -e $$'s/.\b\\(.\\)/\\1/g' -e 's/\(.*\)/\1'$$'\r/' >$@

release: clean png2ico doc/png2ico.txt
	echo $$'\nHAVE YOU UPDATED VERSION IN BOTH THE UNIX AND THE WINDOWS BUILD?\n'
	cd .. && \
	version=$$(sed 's/^.* \([0-9]*-[0-9]*-[0-9]*\) .*$$/\1/' png2ico/VERSION) && \
	tar --owner=root --group=root -czf png2ico/png2ico-src-$${version}.tar.gz png2ico/{LICENSE,VERSION,Makefile,README,README.unix,README.win,doc/bmp.txt,doc/png2ico.1,makefile.bcc32,makezlib.bcc32,png2ico.cpp} && \
	zip png2ico/png2ico-win-$${version}.zip png2ico/{LICENSE,VERSION,README,doc/png2ico.txt,png2ico.exe} 

clean:
	rm -f png2ico *~ doc/*~ *.bak png2ico-src-*.tar.gz png2ico-win-*.zip doc/png2ico.txt

