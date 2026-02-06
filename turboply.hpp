/**
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 TAO 12804985@qq.com
 *
 * @file    turboply.hpp
* @brief    TurboPLY: A lightweight, high-performance PLY file I/O library.
 *          Supports binary little-endian and ASCII formats, and provides
 *          optional memory-mapped file I/O for zero-copy, high-throughput
 *          access to large datasets. Big-endian format is intentionally 
 *          not supported for simplicity and performance.
 * 
 * @version 1.0.4
 * @date    2026-02-07
 *
 * -----------------------------------------------------------------------------
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * -----------------------------------------------------------------------------
 */

#pragma once

#if defined(_MSC_VER)
#  if _MSVC_LANG < 202002L
#    error "Requires C++20 (use -std=c++20 or newer)"
#  endif
#elif defined(__clang__) || defined(__GNUC__)
#  if __cplusplus < 202002L
#    error "Requires C++20 (use /std:c++20 or newer)"
#  endif
#endif

#define TURBOPLY_ENABLE_FILE_MAPPING 1

#include <string>
#include <vector>
#include <variant>
#include <sstream>
#include <fstream>
#include <filesystem>

namespace turboply {

using PlyScalar = std::variant<int8_t, uint8_t, int16_t, uint16_t, int32_t, uint32_t, float, double>;

enum class ScalarKind : uint8_t { UNUSED, INT8, UINT8, INT16, UINT16, INT32, UINT32, FLOAT32, FLOAT64 };

template <typename T> 
T ply_cast(const PlyScalar& v) { return std::visit([](auto&& x) { return static_cast<T>(x); }, v); }

//////////////////////////////////////////////////////////////////////////

class PlyBase {
public:
    enum class Format : uint8_t { BINARY, ASCII };

    struct Element {
        struct Property {
            std::string name;
            ScalarKind valueKind = ScalarKind::UNUSED;
            ScalarKind listKind = ScalarKind::UNUSED;
        };

        std::string name;
        size_t count = 0;
        std::vector<Property> properties;
    };

public:
    PlyBase(Format format);
	virtual ~PlyBase();

private:
    PlyBase(const PlyBase& ) = delete;
    PlyBase& operator=(const PlyBase& ) = delete;

protected:
	std::vector<std::string> _comments;
	std::vector<Element> _elements;
    class FormatHandler* _handler;
    bool _has_header;
};

using PlyFormat  = PlyBase::Format;
using PlyElement = PlyBase::Element;

//////////////////////////////////////////////////////////////////////////

class PlyStreamReader : public PlyBase {
public:
    using StreamT = std::istream;

    explicit PlyStreamReader(std::istream& is, Format format = Format::BINARY) 
        : PlyBase{ format }, _is{ is } {
    }
    virtual ~PlyStreamReader() = default;

	void parseHeader();
    const std::vector<std::string>& getComments() const;
    const std::vector<PlyElement>& getElements() const;

    PlyScalar readScalar(ScalarKind );

private:
	std::istream& _is;
};

class PlyStreamWriter : public PlyBase {
public:
    using StreamT = std::ostream;

    explicit PlyStreamWriter(std::ostream& os, Format format = Format::BINARY) 
        : PlyBase(format), _os(os) {
    }
    virtual ~PlyStreamWriter() = default;
    
    void addComment(std::string c);
    void addElement(Element elem);
    
    void writeHeader();
    void writeScalar(const PlyScalar& v);
    void writeScalar(const PlyScalar& v, ScalarKind k);
    void writeLineEnd();

    void flush() { _os.flush(); }

private:
    std::ostream& _os;
};

//////////////////////////////////////////////////////////////////////////

PlyFormat detectPlyFormat(const std::filesystem::path& filename);

template <class StreamHandler>
    requires std::same_as<StreamHandler, PlyStreamReader> || std::same_as<StreamHandler, PlyStreamWriter>
class PlyFileHandler : public StreamHandler {
public:
    using StreamT = typename StreamHandler::StreamT;

    PlyFileHandler(const std::filesystem::path& filename, bool enable_file_mapping = false)
        requires std::same_as<StreamHandler, PlyStreamReader>
        : PlyFileHandler{ init(filename, enable_file_mapping, 0), detectPlyFormat(filename) } {
    }
    PlyFileHandler(const std::filesystem::path& filename, PlyFormat format = PlyFormat::BINARY
        , bool enable_file_mapping = false, size_t reserve_size = 100 * 1024 * 1024) 
        requires std::same_as<StreamHandler, PlyStreamWriter>
        : PlyFileHandler{ init(filename, enable_file_mapping, reserve_size), format } {
    }
    virtual ~PlyFileHandler() { close(); }

    void close();

private:
    std::unique_ptr<std::streambuf> _mapped_buf;
    std::unique_ptr<StreamT> _managed_stream;

    using Resources = std::pair<std::unique_ptr<std::streambuf>, std::unique_ptr<StreamT>>;
    static Resources init(const std::filesystem::path& filename, bool use_mapping, size_t reserve_size);

    PlyFileHandler(Resources&& , PlyFormat );
};

using PlyFileReader = PlyFileHandler<PlyStreamReader>;
using PlyFileWriter = PlyFileHandler<PlyStreamWriter>;

}

//////////////////////////////////////////////////////////////////////////

#include <array>

namespace turboply::ext {

void insertGeoPlyHeadr(
    const std::filesystem::path& filename,
    const std::string& label, const int srid,
    const std::array<double, 6>& bbox,
    const std::array<double, 3>& offset,
    const std::array<double, 3>& scale
);

bool fetchGeoPlyHeadr(
    const std::filesystem::path& filename,
    std::string& label,
    int& srid,
    std::array<double, 6>& bbox,
    std::array<double, 3>& offset,
    std::array<double, 3>& scale
);

class GeoPlyFileReader final : public PlyFileReader {
public:
    using PlyFileReader::PlyFileReader;

    bool parseHeader(
        std::string& label, int& srid, std::array<double, 6>& bbox,
        std::array<double, 3>& offset, std::array<double, 3>& scale);
    bool parseTexturePath(std::vector<std::string>& textures);

private:
    using PlyFileReader::getComments;
    using PlyFileReader::parseHeader;
};

class GeoPlyFileWriter final : public PlyFileWriter {
public:
    using PlyFileWriter::PlyFileWriter;

    void addHeader(
        const std::string& label, int srid, const std::array<double, 6>& bbox,
        const std::array<double, 3>& offset, const std::array<double, 3>& scale);
    void writeTexturePath(const std::vector<std::string>& textures);

private:
    using PlyFileWriter::addComment;
};

}

#include "turboply_util.hpp"


