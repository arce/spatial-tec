#pragma once
// Minimal GeoTIFF reader used by spatial_tiff2asc.
//
// Scope (see adr/0009-tiff2asc.md): classic TIFF (not BigTIFF), little- or
// big-endian, strips or tiles, compression none / LZW / PackBits, optional
// horizontal predictor (2), 8/16/32-bit integer and 32/64-bit float samples,
// chunky (interleaved) multi-band data. Georeferencing comes from the
// ModelPixelScale + ModelTiepoint GeoTIFF tags; NODATA from GDAL_NODATA.
// Pixels are decoded one image row at a time so that a large raster can be
// streamed (or windowed) without ever being fully loaded in memory.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace Spatial {

struct TiffInfo {
    int width = 0, height = 0;
    int samples_per_pixel = 1;
    int bits_per_sample = 8;
    int sample_format = 1;  // 1 = uint, 2 = int, 3 = IEEE float
    int compression = 1;    // 1 none, 5 LZW, 32773 PackBits
    int predictor = 1;
    int planar = 1;
    bool tiled = false;
    int block_w = 0, block_h = 0;  // strip: width x rows-per-strip; tile: tile size
    bool little_endian = true;

    bool has_scale = false, has_tiepoint = false, has_rotation = false;
    double scale_x = 1.0, scale_y = 1.0;
    double tie_i = 0.0, tie_j = 0.0, tie_x = 0.0, tie_y = 0.0;
    bool pixel_is_point = false;  // GTRasterTypeGeoKey == 2 (PixelIsPoint)
    int epsg = 0;                 // geographic (2048) or projected (3072) code, 0 = unknown/user-defined
    bool has_nodata = false;
    double nodata = 0.0;
};

namespace tiff_detail {

inline bool lzwDecode(const uint8_t* src, size_t n, uint8_t* dst, size_t cap, size_t& out_len) {
    static thread_local int prefix[4096];
    static thread_local uint8_t suffix[4096], first_byte[4096];
    static thread_local int length[4096];
    for (int i = 0; i < 256; ++i) {
        prefix[i] = -1;
        suffix[i] = first_byte[i] = static_cast<uint8_t>(i);
        length[i] = 1;
    }
    int bits = 9, next = 258, prev = -1;
    uint32_t buf = 0;
    int nb = 0;
    size_t pos = 0, o = 0;

    auto emit = [&](int code) -> bool {
        size_t L = static_cast<size_t>(length[code]);
        if (o + L > cap) return false;
        size_t p = o + L;
        int c = code;
        while (c >= 0) {
            dst[--p] = suffix[c];
            c = prefix[c];
        }
        o += L;
        return true;
    };

    while (true) {
        while (nb < bits) {
            if (pos >= n) { out_len = o; return true; }  // no EOI code: treat as end of data
            buf = (buf << 8) | src[pos++];
            nb += 8;
        }
        int code = static_cast<int>((buf >> (nb - bits)) & ((1u << bits) - 1));
        nb -= bits;
        buf &= (nb > 0) ? ((1u << nb) - 1) : 0u;
        if (code == 257) break;
        if (code == 256) { next = 258; bits = 9; prev = -1; continue; }
        if (prev < 0) {
            if (code > 255) return false;
            if (!emit(code)) return false;
            prev = code;
            continue;
        }
        if (code < next) {
            if (!emit(code)) return false;
            if (next < 4096) {
                prefix[next] = prev; suffix[next] = first_byte[code];
                first_byte[next] = first_byte[prev]; length[next] = length[prev] + 1;
                ++next;
            }
        } else if (code == next && next < 4096) {
            prefix[next] = prev; suffix[next] = first_byte[prev];
            first_byte[next] = first_byte[prev]; length[next] = length[prev] + 1;
            ++next;
            if (!emit(code)) return false;
        } else {
            return false;
        }
        prev = code;
        bits = next >= 2047 ? 12 : next >= 1023 ? 11 : next >= 511 ? 10 : 9;
    }
    out_len = o;
    return true;
}

inline bool packBitsDecode(const uint8_t* src, size_t n, uint8_t* dst, size_t cap, size_t& out_len) {
    size_t i = 0, o = 0;
    while (i < n && o < cap) {
        int8_t h = static_cast<int8_t>(src[i++]);
        if (h >= 0) {
            size_t c = static_cast<size_t>(h) + 1;
            if (i + c > n || o + c > cap) return false;
            std::memcpy(dst + o, src + i, c);
            i += c; o += c;
        } else if (h != -128) {
            size_t c = static_cast<size_t>(1 - h);
            if (i >= n || o + c > cap) return false;
            std::memset(dst + o, src[i++], c);
            o += c;
        }
    }
    out_len = o;
    return true;
}

}  // namespace tiff_detail

class TiffReader {
public:
    bool open(const std::string& path, std::string& err) {
        file_.open(path, std::ios::binary);
        if (!file_) { err = "Could not open file: " + path; return false; }
        file_.seekg(0, std::ios::end);
        file_size_ = static_cast<uint64_t>(file_.tellg());
        file_.seekg(0);
        uint8_t h[8];
        file_.read(reinterpret_cast<char*>(h), 8);
        if (!file_) { err = "File too small to be a TIFF"; return false; }
        if (h[0] == 'I' && h[1] == 'I') info_.little_endian = true;
        else if (h[0] == 'M' && h[1] == 'M') info_.little_endian = false;
        else { err = "Not a TIFF file (bad byte-order mark)"; return false; }
        uint32_t magic = get16(h + 2);
        if (magic == 43) { err = "BigTIFF is not supported"; return false; }
        if (magic != 42) { err = "Not a TIFF file (bad magic number)"; return false; }
        uint32_t ifd = get32(h + 4);
        return readIFD(ifd, err) && validate(err);
    }

    const TiffInfo& info() const { return info_; }

    // Reads columns [c0, c1] of image row `row` for `band` (0-based) into out (converted to double).
    bool readRow(int row, int c0, int c1, int band, std::vector<double>& out, std::string& err) {
        const int bps = info_.bits_per_sample / 8;
        const int spp = info_.samples_per_pixel;
        out.assign(static_cast<size_t>(c1 - c0 + 1), 0.0);
        if (!info_.tiled) {
            int strip = row / info_.block_h;
            if (!loadBlock(0, strip, strip, err)) return false;
            const uint8_t* line = blocks_[0].data() + static_cast<size_t>(row - strip * info_.block_h) *
                                                         info_.width * spp * bps;
            for (int c = c0; c <= c1; ++c)
                out[c - c0] = sample(line + (static_cast<size_t>(c) * spp + band) * bps);
        } else {
            int tr = row / info_.block_h;
            int tc0 = c0 / info_.block_w, tc1 = c1 / info_.block_w;
            int tiles_across = (info_.width + info_.block_w - 1) / info_.block_w;
            for (int tc = tc0; tc <= tc1; ++tc) {
                int idx = tr * tiles_across + tc;
                if (!loadBlock(static_cast<size_t>(tc - tc0), idx, idx, err, tc0)) return false;
                const uint8_t* line = blocks_[tc - tc0].data() +
                                      static_cast<size_t>(row - tr * info_.block_h) * info_.block_w * spp * bps;
                int cs = std::max(c0, tc * info_.block_w);
                int ce = std::min(c1, (tc + 1) * info_.block_w - 1);
                for (int c = cs; c <= ce; ++c)
                    out[c - c0] = sample(line + (static_cast<size_t>(c - tc * info_.block_w) * spp + band) * bps);
            }
        }
        return true;
    }

private:
    struct Entry { uint16_t tag, type; uint32_t count; uint64_t value_pos; };

    uint32_t get16(const uint8_t* p) const {
        return info_.little_endian ? (p[0] | (p[1] << 8)) : ((p[0] << 8) | p[1]);
    }
    uint32_t get32(const uint8_t* p) const {
        return info_.little_endian
                   ? (uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24))
                   : ((uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]));
    }

    static size_t typeSize(uint16_t t) {
        switch (t) {
            case 1: case 2: case 6: case 7: return 1;
            case 3: case 8: return 2;
            case 4: case 9: case 11: return 4;
            case 5: case 10: case 12: return 8;
            default: return 0;
        }
    }

    std::vector<uint8_t> entryBytes(const Entry& e) {
        size_t n = typeSize(e.type) * e.count;
        std::vector<uint8_t> b(n);
        if (n == 0) return b;
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(e.value_pos));
        file_.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(n));
        return b;
    }

    std::vector<double> entryNumbers(const Entry& e) {
        std::vector<uint8_t> b = entryBytes(e);
        std::vector<double> v(e.count);
        for (uint32_t i = 0; i < e.count; ++i) {
            const uint8_t* p = b.data() + i * typeSize(e.type);
            switch (e.type) {
                case 1: case 7: v[i] = p[0]; break;
                case 3: v[i] = get16(p); break;
                case 4: v[i] = get32(p); break;
                case 6: v[i] = static_cast<int8_t>(p[0]); break;
                case 8: v[i] = static_cast<int16_t>(get16(p)); break;
                case 9: v[i] = static_cast<int32_t>(get32(p)); break;
                case 11: { uint32_t u = get32(p); float f; std::memcpy(&f, &u, 4); v[i] = f; break; }
                case 12: {
                    uint64_t u;
                    uint32_t a = get32(p), c = get32(p + 4);
                    u = info_.little_endian ? ((uint64_t(c) << 32) | a) : ((uint64_t(a) << 32) | c);
                    double d; std::memcpy(&d, &u, 8); v[i] = d; break;
                }
                default: v[i] = 0;
            }
        }
        return v;
    }

    bool readIFD(uint32_t offset, std::string& err) {
        uint8_t nb[2];
        file_.clear();
        file_.seekg(offset);
        file_.read(reinterpret_cast<char*>(nb), 2);
        if (!file_) { err = "Corrupt TIFF: cannot read directory"; return false; }
        uint32_t n = get16(nb);
        std::vector<uint8_t> raw(static_cast<size_t>(n) * 12);
        file_.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
        if (!file_) { err = "Corrupt TIFF: truncated directory"; return false; }

        std::vector<Entry> es;
        for (uint32_t i = 0; i < n; ++i) {
            const uint8_t* p = raw.data() + i * 12;
            Entry e{static_cast<uint16_t>(get16(p)), static_cast<uint16_t>(get16(p + 2)), get32(p + 4), 0};
            size_t sz = typeSize(e.type) * e.count;
            e.value_pos = sz <= 4 ? offset + 2 + i * 12 + 8 : get32(p + 8);
            es.push_back(e);
        }
        auto find = [&](uint16_t tag) -> const Entry* {
            for (const auto& e : es) if (e.tag == tag) return &e;
            return nullptr;
        };
        auto num = [&](uint16_t tag, double def) {
            const Entry* e = find(tag);
            if (!e || e->count == 0) return def;
            return entryNumbers(*e)[0];
        };

        info_.width = static_cast<int>(num(256, 0));
        info_.height = static_cast<int>(num(257, 0));
        info_.samples_per_pixel = static_cast<int>(num(277, 1));
        if (const Entry* e = find(258)) info_.bits_per_sample = static_cast<int>(entryNumbers(*e)[0]);
        info_.sample_format = static_cast<int>(num(339, 1));
        info_.compression = static_cast<int>(num(259, 1));
        info_.predictor = static_cast<int>(num(317, 1));
        info_.planar = static_cast<int>(num(284, 1));

        const Entry* so = find(273);
        const Entry* sc = find(279);
        const Entry* to = find(324);
        const Entry* tc = find(325);
        if (to && tc) {
            info_.tiled = true;
            info_.block_w = static_cast<int>(num(322, 0));
            info_.block_h = static_cast<int>(num(323, 0));
            offsets_ = entryNumbers(*to);
            counts_ = entryNumbers(*tc);
        } else if (so && sc) {
            info_.block_w = info_.width;
            info_.block_h = static_cast<int>(num(278, info_.height));
            if (info_.block_h <= 0 || info_.block_h > info_.height) info_.block_h = info_.height;
            offsets_ = entryNumbers(*so);
            counts_ = entryNumbers(*sc);
        } else {
            err = "Corrupt or unsupported TIFF: no strip or tile offsets";
            return false;
        }

        if (const Entry* e = find(33550)) {
            auto v = entryNumbers(*e);
            if (v.size() >= 2) { info_.has_scale = true; info_.scale_x = v[0]; info_.scale_y = v[1]; }
        }
        if (const Entry* e = find(33922)) {
            auto v = entryNumbers(*e);
            if (v.size() >= 6) {
                info_.has_tiepoint = true;
                info_.tie_i = v[0]; info_.tie_j = v[1]; info_.tie_x = v[3]; info_.tie_y = v[4];
            }
        }
        if (find(34264)) {  // ModelTransformation: only the pure scale+translate form is handled
            auto v = entryNumbers(*find(34264));
            if (v.size() >= 16) {
                if (std::fabs(v[1]) > 1e-12 || std::fabs(v[4]) > 1e-12) info_.has_rotation = true;
                else if (!info_.has_scale) {
                    info_.has_scale = true; info_.scale_x = v[0]; info_.scale_y = -v[5];
                    info_.has_tiepoint = true;
                    info_.tie_i = info_.tie_j = 0; info_.tie_x = v[3]; info_.tie_y = v[7];
                }
            }
        }
        if (const Entry* e = find(34735)) {  // GeoKeyDirectory
            auto v = entryNumbers(*e);
            for (size_t k = 4; k + 3 < v.size(); k += 4) {
                int key = static_cast<int>(v[k]);
                bool direct = static_cast<int>(v[k + 1]) == 0;
                int val = static_cast<int>(v[k + 3]);
                if (key == 1025 && direct) info_.pixel_is_point = (val == 2);
                if ((key == 2048 || key == 3072) && direct && val > 0 && val < 32767) info_.epsg = val;
            }
        }
        if (const Entry* e = find(42113)) {
            auto b = entryBytes(*e);
            std::string s(b.begin(), b.end());
            s = s.substr(0, s.find('\0'));
            char* endp = nullptr;
            double d = std::strtod(s.c_str(), &endp);
            if (endp != s.c_str()) { info_.has_nodata = true; info_.nodata = d; }
        }
        return true;
    }

    bool validate(std::string& err) {
        const TiffInfo& t = info_;
        if (t.width <= 0 || t.height <= 0) { err = "Corrupt TIFF: invalid image size"; return false; }
        if (t.block_w <= 0 || t.block_h <= 0) { err = "Corrupt TIFF: invalid strip/tile size"; return false; }
        if (t.compression != 1 && t.compression != 5 && t.compression != 32773) {
            err = "Unsupported compression (code " + std::to_string(t.compression) +
                  "); supported: none, LZW, PackBits. Re-save the file with: gdal_translate -co COMPRESS=LZW in.tif out.tif";
            return false;
        }
        if (t.predictor != 1 && t.predictor != 2) {
            err = "Unsupported predictor (code " + std::to_string(t.predictor) + "); supported: none, horizontal";
            return false;
        }
        bool ok_type = (t.sample_format == 3 && (t.bits_per_sample == 32 || t.bits_per_sample == 64)) ||
                       ((t.sample_format == 1 || t.sample_format == 2) &&
                        (t.bits_per_sample == 8 || t.bits_per_sample == 16 || t.bits_per_sample == 32));
        if (!ok_type) {
            err = "Unsupported sample type (" + std::to_string(t.bits_per_sample) + " bits, format " +
                  std::to_string(t.sample_format) + ")";
            return false;
        }
        if (t.predictor == 2 && t.sample_format == 3) { err = "Horizontal predictor on float data is not supported"; return false; }
        if (t.planar != 1 && t.samples_per_pixel > 1) { err = "Multi-band TIFF with separate planes (PlanarConfiguration=2) is not supported"; return false; }
        if (t.has_rotation) { err = "Rotated/sheared GeoTIFF (ModelTransformation) is not supported"; return false; }
        size_t need = static_cast<size_t>(
            t.tiled ? ((t.height + t.block_h - 1) / t.block_h) * ((t.width + t.block_w - 1) / t.block_w)
                    : (t.height + t.block_h - 1) / t.block_h);
        if (offsets_.size() < need || counts_.size() < need) { err = "Corrupt TIFF: missing strip/tile table entries"; return false; }
        return true;
    }

    double sample(const uint8_t* p) const {
        const int bits = info_.bits_per_sample;
        uint64_t u = 0;
        const int n = bits / 8;
        if (info_.little_endian) for (int i = n - 1; i >= 0; --i) u = (u << 8) | p[i];
        else for (int i = 0; i < n; ++i) u = (u << 8) | p[i];
        switch (info_.sample_format) {
            case 1: return static_cast<double>(u);
            case 2:
                if (bits == 8) return static_cast<int8_t>(u);
                if (bits == 16) return static_cast<int16_t>(u);
                return static_cast<int32_t>(u);
            default:
                if (bits == 32) { uint32_t w = static_cast<uint32_t>(u); float f; std::memcpy(&f, &w, 4); return f; }
                double d; std::memcpy(&d, &u, 8); return d;
        }
    }

    void undoPredictor(uint8_t* buf, size_t rows, size_t row_bytes, size_t width) const {
        const int spp = info_.samples_per_pixel;
        const int bps = info_.bits_per_sample / 8;
        for (size_t r = 0; r < rows; ++r) {
            uint8_t* line = buf + r * row_bytes;
            for (size_t x = 1; x < width; ++x)
                for (int s = 0; s < spp; ++s) {
                    uint8_t* cur = line + (x * spp + s) * bps;
                    const uint8_t* prv = line + ((x - 1) * spp + s) * bps;
                    if (bps == 1) {
                        cur[0] = static_cast<uint8_t>(cur[0] + prv[0]);
                    } else {
                        uint32_t a = 0, b = 0;
                        for (int i = 0; i < bps; ++i) {
                            int k = info_.little_endian ? bps - 1 - i : i;
                            a = (a << 8) | cur[k];
                            b = (b << 8) | prv[k];
                        }
                        uint32_t sum = a + b;
                        for (int i = 0; i < bps; ++i) {
                            int k = info_.little_endian ? i : bps - 1 - i;
                            cur[k] = static_cast<uint8_t>(sum >> (8 * i));
                        }
                    }
                }
        }
    }

    // Decodes block `idx` into blocks_[slot], reusing it if it's already there.
    bool loadBlock(size_t slot, int idx, int id, std::string& err, int group = -1) {
        if (blocks_.size() <= slot) { blocks_.resize(slot + 1); block_id_.resize(slot + 1, -1); }
        if (group_ != group) {  // tile column window changed: invalidate cached tiles
            std::fill(block_id_.begin(), block_id_.end(), -1);
            group_ = group;
        }
        if (block_id_[slot] == id) return true;
        const int spp = info_.samples_per_pixel, bps = info_.bits_per_sample / 8;
        size_t row_bytes = static_cast<size_t>(info_.block_w) * spp * bps;
        size_t rows = static_cast<size_t>(info_.block_h);
        if (!info_.tiled) {
            size_t start = static_cast<size_t>(idx) * info_.block_h;
            rows = std::min<size_t>(rows, info_.height - start);
        }
        size_t expected = rows * row_bytes;
        uint64_t off = static_cast<uint64_t>(offsets_[idx]), cnt = static_cast<uint64_t>(counts_[idx]);
        if (off + cnt > file_size_) { err = "Corrupt TIFF: strip/tile data beyond end of file"; return false; }
        std::vector<uint8_t> comp(cnt);
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(off));
        file_.read(reinterpret_cast<char*>(comp.data()), static_cast<std::streamsize>(cnt));
        if (!file_) { err = "Corrupt TIFF: cannot read strip/tile data"; return false; }

        std::vector<uint8_t>& dst = blocks_[slot];
        dst.assign(expected, 0);
        size_t got = 0;
        if (info_.compression == 1) {
            got = std::min<size_t>(expected, comp.size());
            std::memcpy(dst.data(), comp.data(), got);
        } else if (info_.compression == 5) {
            if (!tiff_detail::lzwDecode(comp.data(), comp.size(), dst.data(), expected, got)) {
                err = "Corrupt LZW data in strip/tile " + std::to_string(idx);
                return false;
            }
        } else {
            if (!tiff_detail::packBitsDecode(comp.data(), comp.size(), dst.data(), expected, got)) {
                err = "Corrupt PackBits data in strip/tile " + std::to_string(idx);
                return false;
            }
        }
        if (got < expected) { err = "Corrupt TIFF: strip/tile " + std::to_string(idx) + " is shorter than expected"; return false; }
        if (info_.predictor == 2) undoPredictor(dst.data(), rows, row_bytes, static_cast<size_t>(info_.block_w));
        block_id_[slot] = id;
        return true;
    }

    std::ifstream file_;
    uint64_t file_size_ = 0;
    TiffInfo info_;
    std::vector<double> offsets_, counts_;
    std::vector<std::vector<uint8_t>> blocks_;
    std::vector<int> block_id_;
    int group_ = -2;
};

}  // namespace Spatial
