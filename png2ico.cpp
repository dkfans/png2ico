/* Copyright (C) 2002 Matthias S. Benkmann <matthias@winterdrache.de>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; version 2
of the License (ONLY THIS VERSION).

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>
#include <fstream>
#include <stdexcept>
#include "VERSION"

void usage()
{
	fprintf(stderr,version"\n");
	fprintf(stderr,"USAGE: png2ico icofile [--colors <num>] pngfile1 [pngfile2 ...]\n");
}

auto parse_args(int argc, char * argv[])
{
	const char * outfileName = nullptr;
	std::vector<const char *> inputs;

	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i],"--colors") == 0) {
			// eat color parameter for compatibility
			++i;
			if (i >= argc) {
				throw std::runtime_error("Number missing after --colors");
			}
			continue;
		} else if (!outfileName) {
			outfileName = argv[i];
			continue;
		} else {
			inputs.emplace_back(argv[i]);
		}
	}
	return std::make_pair(outfileName, std::move(inputs));
}

#pragma pack(1)
struct ICONDIR {
	uint16_t idReserved;
	uint16_t idType;
	uint16_t idCount;
};

struct ICONDIRECTORY {
	uint8_t bWidth;
	uint8_t bHeight;
	uint8_t bColorCount;
	uint8_t bReserved;
	uint16_t wPlanes;
	uint16_t wBitCount;
	uint32_t dwBytesInRes;
	uint32_t dwImageOffset;
};
#pragma pack()

int create_icon(const char * outfileName, const std::vector<const char *> & inputs)
{
	std::ofstream out;
	out.exceptions(std::ios::badbit);
	try {
		out.open(outfileName, std::ios::binary | std::ios::out | std::ios::trunc);
	} catch (const std::ios::failure & e) {
		fprintf(stderr, "Cannot open %s: %s\n", outfileName, e.what());
		return 1;
	}
	// write icon directory header
	const ICONDIR icondir = { 0, 1, uint16_t(inputs.size()) };
	try {
		out.write(reinterpret_cast<const char *>(&icondir), sizeof(ICONDIR));
	} catch (const std::ios::failure & e) {
		fprintf(stderr, "Cannot write to %s: %s\n", outfileName, e.what());
		return 1;
	}
	// write icon entry headers
	size_t offset = sizeof(ICONDIR) + (sizeof(ICONDIRECTORY) * inputs.size());
	for (const auto infileName : inputs) {
		std::ifstream in;
		in.exceptions(std::ios::badbit);
		try {
			in.open(infileName, std::ios::binary | std::ios::in | std::ios::ate);
		} catch (const std::ios::failure & e) {
			fprintf(stderr, "Cannot open %s: %s\n", infileName, e.what());
			return 1;
		}
		const auto size = in.tellg();
		// lie a bit, claim all our icons are compressed and 24-bpp
		const ICONDIRECTORY identry = { 0, 0, 0, 0, 1, 24, uint32_t(size), uint32_t(offset) };
		offset += size;
		try {
			out.write(reinterpret_cast<const char *>(&identry), sizeof(identry));
		} catch (const std::ios::failure & e) {
			fprintf(stderr, "Cannot write to %s: %s\n", outfileName, e.what());
			return 1;
		}
	}
	// write icons
	for (const auto infileName : inputs) {
		std::ifstream in;
		in.exceptions(std::ios::badbit);
		try {
			in.open(infileName, std::ios::binary | std::ios::in);
		} catch (const std::ios::failure & e) {
			fprintf(stderr, "Cannot open %s: %s\n", infileName, e.what());
			return 1;
		}
		char buffer[8192];
		while (!in.eof()) {
			try {
				in.read(buffer, sizeof(buffer));
			} catch (const std::ios::failure & e) {
				fprintf(stderr, "Cannot read from %s: %s\n", infileName, e.what());
				return 1;
			}
			try {
				out.write(buffer, in.gcount());
			} catch (const std::ios::failure & e) {
				fprintf(stderr, "Cannot write to %s: %s\n", outfileName, e.what());
				return 1;
			}
		}
	}
	return 0;
}

int main(int argc, char* argv[])
{
	try {
		const auto [ outfileName, inputs ] = parse_args(argc, argv);
		if (!outfileName) {
			usage();
			throw std::runtime_error("No output file specified");
		} else if (inputs.empty()) {
			usage();
			throw std::runtime_error("No input files specified");
		}
		return create_icon(outfileName, inputs);
	} catch (const std::exception & e) {
		fprintf(stderr, "%s\n", e.what());
	}
	return 1;
}
