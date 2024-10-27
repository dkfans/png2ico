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
#include <cstdarg>
#include <vector>
#include <string>
#include <fstream>
#include <stdexcept>
#include <memory>
#include <filesystem>
#include <spng.h>
#include "VERSION"

void usage()
{
	fprintf(stderr,version"\n");
	fprintf(stderr,"USAGE: png2ico icofile [--colors <num>] pngfile1 [pngfile2 ...]\n");
}

struct formatted_error : std::exception {

	formatted_error(const char * fmt, ...) /* __attribute__((format(printf, 1, 2))) */
	{
		va_list args;
		va_start(args, fmt);
		const auto len = vsnprintf(nullptr, 0, fmt, args);
		va_end(args);
		m_reason.resize(len + 1);
		va_start(args, fmt);
		vsnprintf(m_reason.data(), m_reason.size(), fmt, args);
		va_end(args);
	}

	const char * what() const noexcept override {
		return m_reason.data();
	}

	std::vector<char> m_reason;
};

auto parse_args(int argc, char * argv[])
{
	const char * outfileName = nullptr;
	std::vector<const char *> inputs;

	for (int i = 1; i < argc; ++i) {
		if (strcmp(argv[i],"--colors") == 0) {
			// eat color parameter for compatibility
			++i;
			if (i >= argc) {
				throw formatted_error("Number missing after --colors");
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

struct spng_deleter {
	void operator()(spng_ctx * ctx) const {
		spng_ctx_free(ctx);
	}
};

struct file_deleter {
	void operator()(FILE * file) const {
		fclose(file);
	}
};

using spng_ctx_ptr = std::unique_ptr<spng_ctx, spng_deleter>;
using file_ptr = std::unique_ptr<FILE, file_deleter>;

auto get_dimensions(const char * filename)
{
	file_ptr file(fopen(filename, "rb"));
	if (!file) {
		throw formatted_error("Cannot open %s: %s", filename, strerror(errno));
	}
	spng_ctx_ptr ctx(spng_ctx_new(0));
	if (!ctx) {
		throw formatted_error("Cannot create spng context");
	} else if (const auto res = spng_set_png_file(ctx.get(), file.get()); res != 0) {
		throw formatted_error("Cannot attach file of %s: %s", filename, spng_strerror(res));
	}
	spng_ihdr header;
	if (const auto res = spng_get_ihdr(ctx.get(), &header); res != 0) {
		throw formatted_error("Cannot read PNG header of %s: %s", filename, spng_strerror(res));
	}
	return std::make_pair(
		header.width <= 255 ? header.width : 0,
		header.height <= 255 ? header.height : 0
	);
}

auto make_ifstream(const char * filename)
{
	try {
		std::ifstream stream;
		stream.exceptions(std::ios::badbit);
		stream.open(filename, std::ios::binary | std::ios::in);
		return stream;
	} catch (const std::exception & e) {
		throw formatted_error("Cannot open %s: %s", filename, e.what());
	}
}

auto make_ofstream(const char * filename)
{
	try {
		std::ofstream stream;
		stream.exceptions(std::ios::badbit);
		stream.open(filename, std::ios::binary | std::ios::out | std::ios::trunc);
		return stream;
	} catch (const std::exception & e) {
		throw formatted_error("Cannot open %s: %s", filename, e.what());
	}
}

auto write_ICONDIR(const char * filename, std::ostream & stream, uint16_t count)
{
	const ICONDIR icondir = { 0, 1, count };
	try {
		stream.write(reinterpret_cast<const char *>(&icondir), sizeof(ICONDIR));
	} catch (const std::exception & e) {
		throw formatted_error("Cannot write to %s: %s", filename, e.what());
	}
}

auto get_size(const char * filename)
{
	try {
		return std::filesystem::file_size(filename);
	} catch (const std::exception & e) {
		throw formatted_error("Cannot get size of %s: %s", filename, e.what());
	}
}

auto write_ICONDIRECTORY(const char * filename, std::ostream & stream, uint8_t width, uint8_t height, uint32_t size, uint32_t offset)
{
	const ICONDIRECTORY identry = { width, height, 0, 0, 1, 24, size, offset };
	try {
		stream.write(reinterpret_cast<const char *>(&identry), sizeof(identry));
	} catch (const std::exception & e) {
		throw formatted_error("Cannot write to %s: %s", filename, e.what());
	}
}

auto read(const char * filename, std::istream & stream, std::vector<char> & buffer)
{
	try {
		stream.read(buffer.data(), buffer.size());
		return stream.gcount();
	} catch (const std::exception & e) {
		throw formatted_error("Cannot read from %s: %s", filename, e.what());
	}
}

auto write(const char * filename, std::ostream & stream, const char * buffer, size_t amount)
{
	try {
		stream.write(buffer, amount);
	} catch (const std::exception & e) {
		throw formatted_error("Cannot write to %s: %s", filename, e.what());
	}
}

void create_icon(const char * outfileName, const std::vector<const char *> & inputs)
{
	auto out = make_ofstream(outfileName);
	// write directory
	write_ICONDIR(outfileName, out, inputs.size());
	size_t offset = sizeof(ICONDIR) + (sizeof(ICONDIRECTORY) * inputs.size());
	for (const auto infileName : inputs) {
		const auto size = get_size(infileName);
		const auto [width, height] = get_dimensions(infileName);
		write_ICONDIRECTORY(infileName, out, width, height, size, offset);
		offset += size;
	}
	// write icons
	for (const auto infileName : inputs) {
		auto in = make_ifstream(infileName);
		std::vector<char> buffer(8192);
		while (!in.eof()) {
			const auto len = read(infileName, in, buffer);
			write(outfileName, out, buffer.data(), len);
		}
	}
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
		create_icon(outfileName, inputs);
		return 0;
	} catch (const std::exception & e) {
		fprintf(stderr, "%s\n", e.what());
	}
	return 1;
}
