CPPFLAGS=-W -Wall -O2
DEBUG=-g

all: png2ico

png2ico: png2ico.cpp
	g++ $(CPPFLAGS) $(DEBUG) -o $@ $< -lpng

doc/png2ico.txt: doc/png2ico.1
	man $< |sed  -e $$'s/.\b\\(.\\)/\\1/g' -e 's/\(.*\)/\1'$$'\r/' >$@

clean:
	rm -f png2ico *~
