#include "turboply.hpp"

#if TURBOPLY_ENABLE_FILE_MAPPING
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>

namespace {

    class mapped_file_buf : public std::streambuf {
        boost::interprocess::file_mapping fm_;
        boost::interprocess::mapped_region region_;
        std::filesystem::path filename_;
        bool read_only_;

    public:
        mapped_file_buf(const std::filesystem::path& filename, bool read_only, size_t reserve_size)
            : filename_{ filename }, read_only_{ read_only } {

            if (!read_only) {
                if (!std::filesystem::exists(filename))
                    std::ofstream touch(filename, std::ios::binary);
                if (reserve_size > 0)
                    std::filesystem::resize_file(filename, reserve_size);
            }

            fm_ = boost::interprocess::file_mapping(filename.generic_string().c_str()
                , read_only ? boost::interprocess::read_only : boost::interprocess::read_write);
            region_ = boost::interprocess::mapped_region(fm_
                , read_only ? boost::interprocess::read_only : boost::interprocess::read_write);

            char* p = static_cast<char*>(region_.get_address());
            auto size = region_.get_size();
            read_only ? this->setg(p, p, p + size) : this->setp(p, p + size);
        }

        virtual ~mapped_file_buf() {
            if (!read_only_) {
                // 解除映射然后resize为实际文件大小
                region_ = boost::interprocess::mapped_region();
                fm_ = boost::interprocess::file_mapping();
                size_t final_len = pptr() - pbase();
                std::filesystem::resize_file(filename_, final_len);
            }
        }

    protected:
        virtual pos_type seekoff(off_type off, std::ios_base::seekdir dir,
            std::ios_base::openmode which = std::ios_base::in | std::ios_base::out) override {
            if (which & std::ios_base::out) {
                char* target = nullptr;
                if (dir == std::ios_base::beg)      target = pbase() + off;
                else if (dir == std::ios_base::cur) target = pptr() + off;
                else if (dir == std::ios_base::end) target = epptr() + off;

                if (target >= pbase() && target <= epptr()) {
                    setp(pbase(), epptr()); 
                    pbump(static_cast<int>(target - pptr())); 
                    return target - pbase();
                }
            }
            return pos_type(off_type(-1)); 
        }

        virtual pos_type seekpos(pos_type sp, std::ios_base::openmode which = std::ios_base::in | std::ios_base::out) override {
            return seekoff(off_type(sp), std::ios_base::beg, which);
        }
    };

}

#endif

namespace turboply {

PlyFormat detectPlyFormat(const std::filesystem::path& filename) {
    std::ifstream ifs(filename);
    if (!ifs.is_open()) {
        throw std::runtime_error(std::format("Ply Read Error: Cannot open file '{}' for format detection.", filename.string()));
    }

    constexpr std::streamsize N = 1024;
    std::string header(N, '\0');
    ifs.read(header.data(), N);
    header.resize(static_cast<size_t>(ifs.gcount()));

    bool found_ascii = (header.find("format ascii") != std::string::npos);
    bool found_bin_le = (header.find("format binary_little_endian") != std::string::npos);

    if (found_ascii && !found_bin_le) return PlyFormat::ASCII;
    if (found_bin_le && !found_ascii) return PlyFormat::BINARY;

    throw std::runtime_error("Ply Read Error: Unsupported or unrecognized PLY format in header.");
}

//////////////////////////////////////////////////////////////////////////

template <class StreamHandler>
    requires std::same_as<StreamHandler, PlyStreamReader> || std::same_as<StreamHandler, PlyStreamWriter>
PlyFileHandler<StreamHandler>::PlyFileHandler(Resources&& res, PlyFormat format)
    : StreamHandler{ *res.second, format }
    , _mapped_buf{ std::move(res.first) }
    , _managed_stream{ std::move(res.second) } {
}

template <class StreamHandler>
    requires std::same_as<StreamHandler, PlyStreamReader> || std::same_as<StreamHandler, PlyStreamWriter>
typename PlyFileHandler<StreamHandler>::Resources
    PlyFileHandler<StreamHandler>::init(const std::filesystem::path& filename, bool use_mapping, size_t reserve_size) {
    constexpr bool is_reader = std::is_same_v<StreamT, std::istream>;
    Resources res;

    if (use_mapping) {
#if TURBOPLY_ENABLE_FILE_MAPPING
        try {
            res.first = std::make_unique<mapped_file_buf>(filename.c_str(), is_reader, reserve_size);
            res.second = std::make_unique<StreamT>(res.first.get());
        } catch (const std::exception& e) {
            throw std::runtime_error(std::format("Ply Error: Failed to map file '{}': {}.", filename.string(), e.what()));
        }
#else
        throw std::runtime_error("Ply Error: File mapping is disabled in this build (TURBOPLY_ENABLE_FILE_MAPPING is off).");
#endif
    }
    else {
        if constexpr (is_reader)
            res.second = std::make_unique<std::ifstream>(filename, std::ios::binary);
        else
            res.second = std::make_unique<std::ofstream>(filename, std::ios::binary);

        if (!res.second->good())
            throw std::runtime_error(std::format("Ply Error: Failed to open file '{}'.", filename.string()));
    }

    return res;
}

template <class StreamHandler>
    requires std::same_as<StreamHandler, PlyStreamReader> || std::same_as<StreamHandler, PlyStreamWriter>
void PlyFileHandler<StreamHandler>::close() {
    _managed_stream.reset();
    _mapped_buf.reset();
}

template class PlyFileHandler<PlyStreamReader>;
template class PlyFileHandler<PlyStreamWriter>;

}
