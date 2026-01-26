#include "turboply.hpp"
#include <cassert>
#include <stdexcept>

namespace turboply {

using PlyProperty = PlyElement::Property;

constexpr std::array<std::pair<std::string_view, std::string_view>, 9> scalarKindNames{ {
    { "unused", "unused"  },
    { "char"  , "int8"    },
    { "uchar" , "uint8"   },
    { "short" , "int16"   },
    { "ushort", "uint16"  },
    { "int"   , "int32"   },
    { "uint"  , "uint32"  },
    { "float" , "float32" },
    { "double", "float64" },
} };

inline std::string_view scalarKindToString(ScalarKind k) {
    return scalarKindNames[static_cast<int>(k)].first;
}

inline ScalarKind scalarKindFromString(std::string_view s) {
    for (std::size_t i = 0; i < scalarKindNames.size(); ++i) {
        if (s == scalarKindNames[i].first || s == scalarKindNames[i].second) {
            return static_cast<ScalarKind>(i);
        }
    }

    throw std::runtime_error(std::format("Ply Error: Unsupported scalar type '{}'.", s));
}

//////////////////////////////////////////////////////////////////////////

class FormatHandler {
public:
    virtual ~FormatHandler() = default;

    virtual bool isBinary() = 0;
    virtual std::string_view formatHeader() = 0;

    virtual PlyScalar readScalar(std::istream&, ScalarKind) = 0;
    virtual void writeScalar(std::ostream&, const PlyScalar&) = 0;
    virtual void writeScalar(std::ostream& os, const PlyScalar& v, ScalarKind k) {
        return visitScalar([&]<typename T>(std::ostream&) {
            return this->writeScalar(os, ply_cast<T>(v));
        }, os, k);
    }

    virtual void writeLineEnd(std::ostream&) = 0;

protected:
    template <typename F, typename StreamT>
    static auto visitScalar(F&& func, StreamT& s, ScalarKind k) {
        switch (k) {
        case ScalarKind::INT8:    return func.template operator()<int8_t>(s);
        case ScalarKind::UINT8:   return func.template operator()<uint8_t>(s);
        case ScalarKind::INT16:   return func.template operator()<int16_t>(s);
        case ScalarKind::UINT16:  return func.template operator()<uint16_t>(s);
        case ScalarKind::INT32:   return func.template operator()<int32_t>(s);
        case ScalarKind::UINT32:  return func.template operator()<uint32_t>(s);
        case ScalarKind::FLOAT32: return func.template operator()<float>(s);
        case ScalarKind::FLOAT64: return func.template operator()<double>(s);
        }

        throw std::runtime_error("Ply Error: Unsupported scalar kind.");
    }
};

template <typename Derived>
class ScalarHandler : public FormatHandler {
public:
    virtual PlyScalar readScalar(std::istream& is, ScalarKind k) override {
        return visitScalar([&]<typename T>(std::istream & s) -> PlyScalar {
            return static_cast<Derived*>(this)->template readImpl<T>(s);
        }, is, k);
    }

    virtual void writeScalar(std::ostream& os, const PlyScalar& v) override {
        std::visit([&](auto&& x) {
            using T = std::decay_t<decltype(x)>;
            static_cast<Derived*>(this)->template writeImpl<T>(os, static_cast<T>(x));
            }, v);
    }
};

class BinaryHandler : public ScalarHandler<BinaryHandler> {
    friend class ScalarHandler<BinaryHandler>;

    template<class T> requires std::is_arithmetic_v<T>
    T readImpl(std::istream& is) {
        T v{};
        is.read(reinterpret_cast<char*>(&v), sizeof(T)); 
        return v;
    }

    template<class T> requires std::is_arithmetic_v<T>
    void writeImpl(std::ostream& os, T v) {
        os.write(reinterpret_cast<const char*>(&v), sizeof(T));
    }

public:
    virtual bool isBinary() override { 
        return true; 
    }

    virtual std::string_view formatHeader() override { 
        return "format binary_little_endian 1.0"; 
    }

    virtual void writeLineEnd(std::ostream&) override { 
        /* no-op for binary */ 
    }
};

class AsciiHandler : public ScalarHandler<AsciiHandler> {
    friend class ScalarHandler<AsciiHandler>;

    template<class T> requires std::is_arithmetic_v<T>
    T readImpl(std::istream& in) {
        char buf[64];
        if (!(in >> buf)) return T{};
        T v{};
        auto [ptr, ec] = std::from_chars(buf, buf + std::strlen(buf), v);
        
        if (ec != std::errc())
            throw std::runtime_error(std::format("Ply Read Error: Failed to parse ASCII value '{}'.", buf));

        return v;
    }

    template<class T> requires std::is_arithmetic_v<T>
    void writeImpl(std::ostream& os, T v) {
        char buf[64];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v);
        parseError(ec, "Ply Write: writeScalar format error");
        *ptr++ = ' ';
        os.write(buf, ptr - buf);
    }

    inline void parseError(std::errc ec, const char* msg) {
        assert(ec == std::errc() && msg);
    }

public:
    virtual bool isBinary() override { 
        return false; 
    }
    virtual std::string_view formatHeader() override {
        return "format ascii 1.0"; 
    }

    virtual void writeLineEnd(std::ostream& os) override {
        // 非内存映射的ofstream会触发内核系统调用较慢 大文本应使用文件映射
        os.seekp(os.tellp() - std::streamoff(1));
        os.put('\n');
    }
};

//////////////////////////////////////////////////////////////////////////

PlyBase::PlyBase(Format format) 
    : _comments{}, _elements{}
    , _handler{ nullptr }, _has_header{ false } {

    if(format == Format::BINARY)
        _handler = new BinaryHandler;
    else
        _handler = new AsciiHandler;
}

PlyBase::~PlyBase() {
    if (_handler) delete _handler;
}

//////////////////////////////////////////////////////////////////////////

void PlyStreamReader::parseHeader() {
    if (_has_header)
        return;

    std::string line;
    std::getline(_is, line);
    if (!line.starts_with("ply"))
        throw std::runtime_error("Ply Read Error: Invalid file format (missing 'ply' magic number).");

    std::getline(_is, line);
    if (!line.starts_with(_handler->formatHeader()))
        throw std::runtime_error(std::format("Ply Read Error: Unsupported PLY format. Expected '{}'.", _handler->formatHeader()));

    Element* current = nullptr;

    while (std::getline(_is, line)) {
        if (line.starts_with("end_header"))
            break;

        std::istringstream iss(line);
        std::string token;
        iss >> token;

        if (token == "comment") {
            _comments.push_back(line.substr(8));
        }
        else if (token == "element") {
            Element e;
            iss >> e.name >> e.count;
            _elements.push_back(e);
            current = &_elements.back();
        }
        else if (token == "property") {
            if (!current)
                throw std::runtime_error("Ply Read Error: Property defined without a parent element.");

            std::string t;
            iss >> t;
            PlyProperty p;

            if (t == "list") {
                std::string lt, vt;
                iss >> lt >> vt >> p.name;
                p.listKind = scalarKindFromString(lt);
                p.valueKind = scalarKindFromString(vt);
            }
            else {
                p.valueKind = scalarKindFromString(t);
                iss >> p.name;
            }

            current->properties.push_back(p);
        }
    }

    _has_header = true;
}

const std::vector<std::string>& PlyStreamReader::getComments() const {
    const_cast<PlyStreamReader*>(this)->parseHeader();
    return _comments;
}

const std::vector<PlyElement>& PlyStreamReader::getElements() const {
    const_cast<PlyStreamReader*>(this)->parseHeader();
    return _elements;
}

PlyScalar PlyStreamReader::readScalar(ScalarKind k) {
    return _handler->readScalar(_is, k);
}

//////////////////////////////////////////////////////////////////////////

void PlyStreamWriter::addComment(std::string c) {
    _comments.push_back(std::move(c));
}

void PlyStreamWriter::addElement(Element elem) { 
    // 检查重名
    if (_elements.end() != std::find_if(_elements.begin(), _elements.end()
        , [&elem](const Element& e) { return e.name == elem.name; })
        )
        throw std::runtime_error(std::format("Ply Write Error: Duplicate element name '{}' is not allowed.", elem.name));

    _elements.push_back(std::move(elem));
}

void PlyStreamWriter::writeHeader() {
    if (_has_header)
        throw std::runtime_error("Ply Write Error: Header has already been written.");

    _os << "ply\n";
    _os << _handler->formatHeader() << "\n";

    for (const auto& c : _comments)
        _os << "comment " << c << "\n";

    for (const auto& e : _elements) {
        _os << "element " << e.name << " " << e.count << "\n";
        for (const auto& p : e.properties) {
            if (p.listKind != ScalarKind::UNUSED) {
                _os << "property list "
                    << scalarKindToString(p.listKind) << " "
                    << scalarKindToString(p.valueKind) << " "
                    << p.name << "\n";
            }
            else {
                _os << "property "
                    << scalarKindToString(p.valueKind) << " "
                    << p.name << "\n";
            }
        }
    }

    _os << "end_header\n";

    _has_header = true;
}

void PlyStreamWriter::writeScalar(const PlyScalar& v) {
    _handler->writeScalar(_os, v);
}

void PlyStreamWriter::writeScalar(const PlyScalar& v, ScalarKind k) {
    _handler->writeScalar(_os, v, k);
}

void PlyStreamWriter::writeLineEnd() {
    _handler->writeLineEnd(_os);
}

}

