#  If compiling fails with the error that png.h or -lpng can't be found,
#  because libpng is installed under /usr/local/ on your system, then
#  remove the # in the following 2 lines.
INCLUDES=#-I/usr/local/include
LDFLAGS=#-L/usr/local/lib

TAR=tar
CXX=g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pedantic -Werror -O3
DEBUG ?= 1

ifeq ($(DEBUG), 1)
	CXXFLAGS += -DDEBUG -g
else
	CXXFLAGS += -DNDEBUG
endif

all: png2ico

png2ico: png2ico.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(LDFLAGS) -o $@ png2ico.cpp

doc/png2ico.txt: doc/man1/png2ico.1
	man -M "`pwd`"/doc png2ico |sed  -e $$'s/.\b\\(.\\)/\\1/g' -e 's/\(.*\)/\1'$$'\r/' >$@

release: SHELL=bash
release: maintainer-clean png2ico doc/png2ico.txt
	pwd="`pwd`" && pwd="$${pwd##*/}" && cd .. && \
	version=$$(sed 's/^.* \([0-9]*-[0-9]*-[0-9]*\) .*$$/\1/' "$$pwd"/VERSION) && \
	$(TAR) --owner=0 --group=0 -czf "$$pwd"/png2ico-src-$${version}.tar.gz "$$pwd"/{LICENSE,VERSION,Makefile,README,README.unix,README.win,README.verifying,doc/bmp.txt,doc/man1/png2ico.1,makefile.bcc32,makezlib.bcc32,png2ico.cpp} && \
	zip "$$pwd"/png2ico-win-$${version}.zip "$$pwd"/{LICENSE,VERSION,README,README.verifying,doc/png2ico.txt,png2ico.exe}
	@echo
	@echo '****************************************************************'
	@echo 'HAVE YOU UPDATED VERSION IN BOTH THE UNIX AND THE WINDOWS BUILD?'
	@echo '****************************************************************'
	@echo

clean distclean clobber:
	rm -f png2ico *~ doc/*~ doc/man1/*~ *.bak png2ico-src-*.tar.gz* png2ico-win-*.zip*

maintainer-clean: distclean
	rm -f doc/png2ico.txt


SIGN_KEY_ID=<matthias@winterdrache.de>
sign:
	version=$$(sed 's/^.* \([0-9]*-[0-9]*-[0-9]*\) .*$$/\1/' VERSION) && \
	gpg --default-key '$(SIGN_KEY_ID)' --sign --armor --detach-sig --output png2ico-src-$${version}.tar.gz.sig png2ico-src-$${version}.tar.gz && \
	gpg --default-key '$(SIGN_KEY_ID)' --sign --armor --detach-sig --output png2ico-win-$${version}.zip.sig png2ico-win-$${version}.zip

#  The verify target is only meant for the maintainer to test if the files
#  have been properly signed. Do *not* use this target in build scripts to
#  feel secure. It's a false sense of security. If the archive is compromised,
#  this target may be compromised, too.
verify:
	version=$$(sed 's/^.* \([0-9]*-[0-9]*-[0-9]*\) .*$$/\1/' VERSION) && \
	gpg --verify png2ico-src-$${version}.tar.gz.sig png2ico-src-$${version}.tar.gz && \
	gpg --verify png2ico-win-$${version}.zip.sig png2ico-win-$${version}.zip
