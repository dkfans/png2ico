all: png2ico

png2ico: png2ico.cpp
	g++ -g -Wall -W -o $@ $< -lpng
