#include "timetransle/controller/process_icon.hpp"

#include <Windows.h>
// The project compiles with WIN32_LEAN_AND_MEAN, which omits the OLE headers
// that GDI+ relies on for IStream and friends.
#include <objidl.h>
#include <gdiplus.h>
#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cwctype>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace timetransle::controller {
namespace {

// Matches the WebView contract: "data:image/png;base64," plus base64 must fit
// into 16384 characters, so the encoded PNG is capped accordingly.
constexpr std::size_t maximum_data_url_length = 16'384;
constexpr std::size_t maximum_png_bytes = 12'000;
constexpr std::size_t bytes_per_pixel = 4;

std::unordered_map<std::wstring, std::string>& icon_cache() {
    static std::unordered_map<std::wstring, std::string> cache;
    return cache;
}

bool ensure_gdiplus_started() {
    static const bool started = [] {
        Gdiplus::GdiplusStartupInput input{};
        ULONG_PTR token{};
        return Gdiplus::GdiplusStartup(&token, &input, nullptr) ==
               Gdiplus::Ok;
    }();
    return started;
}

struct IconInfoGuard {
    ICONINFO info{};
    bool valid{};

    [[nodiscard]] bool acquire(const HICON icon) noexcept {
        valid = GetIconInfo(icon, &info) != FALSE;
        return valid;
    }

    ~IconInfoGuard() {
        if (!valid) {
            return;
        }
        if (info.hbmColor != nullptr) {
            DeleteObject(info.hbmColor);
        }
        if (info.hbmMask != nullptr) {
            DeleteObject(info.hbmMask);
        }
    }
};

class StreamGuard {
public:
    StreamGuard() = default;

    [[nodiscard]] bool create() noexcept {
        return CreateStreamOnHGlobal(nullptr, TRUE, &stream_) == S_OK;
    }

    ~StreamGuard() {
        if (stream_ != nullptr) {
            stream_->Release();
        }
    }

    StreamGuard(const StreamGuard&) = delete;
    StreamGuard& operator=(const StreamGuard&) = delete;

    [[nodiscard]] IStream* get() const noexcept {
        return stream_;
    }

private:
    IStream* stream_{};
};

[[nodiscard]] bool read_bitmap_bits(
    const HBITMAP bitmap,
    const int width,
    const int height,
    const UINT bit_count,
    void* destination) noexcept {
    BITMAPINFOHEADER header{};
    header.biSize = sizeof(BITMAPINFOHEADER);
    header.biWidth = width;
    header.biHeight = -height;
    header.biPlanes = 1;
    header.biBitCount = static_cast<WORD>(bit_count);
    header.biCompression = BI_RGB;
    const auto screen_dc = GetDC(nullptr);
    if (screen_dc == nullptr) {
        return false;
    }
    const auto scan_lines = GetDIBits(
        screen_dc,
        bitmap,
        0,
        static_cast<UINT>(height),
        destination,
        reinterpret_cast<BITMAPINFO*>(&header),
        DIB_RGB_COLORS);
    ReleaseDC(nullptr, screen_dc);
    return scan_lines == height;
}

[[nodiscard]] bool find_png_encoder(CLSID& encoder) {
    UINT codec_count = 0;
    UINT codec_bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&codec_count, &codec_bytes) !=
            Gdiplus::Ok ||
        codec_count == 0 ||
        codec_bytes == 0) {
        return false;
    }
    std::vector<std::uint8_t> buffer(codec_bytes);
    auto* const codecs =
        reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    if (Gdiplus::GetImageEncoders(codec_count, codec_bytes, codecs) !=
        Gdiplus::Ok) {
        return false;
    }
    for (UINT index = 0; index < codec_count; ++index) {
        if (wcscmp(codecs[index].MimeType, L"image/png") == 0) {
            encoder = codecs[index].Clsid;
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::string base64_encode(
    const std::uint8_t* data,
    const std::size_t size) {
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((size + 2) / 3) * 4);
    std::size_t offset = 0;
    while (offset + 3 <= size) {
        const std::uint32_t chunk =
            (static_cast<std::uint32_t>(data[offset]) << 16U) |
            (static_cast<std::uint32_t>(data[offset + 1]) << 8U) |
            static_cast<std::uint32_t>(data[offset + 2]);
        output.push_back(table[(chunk >> 18U) & 0x3FU]);
        output.push_back(table[(chunk >> 12U) & 0x3FU]);
        output.push_back(table[(chunk >> 6U) & 0x3FU]);
        output.push_back(table[chunk & 0x3FU]);
        offset += 3;
    }
    const auto remaining = size - offset;
    if (remaining == 1) {
        const std::uint32_t chunk =
            static_cast<std::uint32_t>(data[offset]) << 16U;
        output.push_back(table[(chunk >> 18U) & 0x3FU]);
        output.push_back(table[(chunk >> 12U) & 0x3FU]);
        output.push_back('=');
        output.push_back('=');
    } else if (remaining == 2) {
        const std::uint32_t chunk =
            (static_cast<std::uint32_t>(data[offset]) << 16U) |
            (static_cast<std::uint32_t>(data[offset + 1]) << 8U);
        output.push_back(table[(chunk >> 18U) & 0x3FU]);
        output.push_back(table[(chunk >> 12U) & 0x3FU]);
        output.push_back(table[(chunk >> 6U) & 0x3FU]);
        output.push_back('=');
    }
    return output;
}

[[nodiscard]] std::string encode_icon_png_data_url(const HICON icon) {
    if (!ensure_gdiplus_started()) {
        return {};
    }
    IconInfoGuard icon_info;
    if (!icon_info.acquire(icon)) {
        return {};
    }
    BITMAP bitmap{};
    if (GetObjectW(icon_info.info.hbmColor, sizeof(BITMAP), &bitmap) == 0 ||
        bitmap.bmWidth <= 0 ||
        bitmap.bmHeight == 0) {
        return {};
    }
    const auto width = bitmap.bmWidth;
    const auto height =
        bitmap.bmHeight < 0 ? -bitmap.bmHeight : bitmap.bmHeight;
    const std::size_t pixel_count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<std::uint8_t> pixels(pixel_count * bytes_per_pixel);
    if (!read_bitmap_bits(
            icon_info.info.hbmColor,
            width,
            height,
            32,
            pixels.data())) {
        return {};
    }

    bool has_alpha = false;
    for (std::size_t offset = bytes_per_pixel - 1; offset < pixels.size();
         offset += bytes_per_pixel) {
        if (pixels[offset] != 0) {
            has_alpha = true;
            break;
        }
    }
    if (!has_alpha) {
        const std::size_t mask_row_bytes =
            ((static_cast<std::size_t>(width) + 31) / 32) * 4;
        std::vector<std::uint8_t> mask(
            mask_row_bytes * static_cast<std::size_t>(height));
        if (read_bitmap_bits(
                icon_info.info.hbmMask,
                width,
                height,
                1,
                mask.data())) {
            for (std::size_t y = 0; y < static_cast<std::size_t>(height);
                 ++y) {
                for (std::size_t x = 0; x < static_cast<std::size_t>(width);
                     ++x) {
                    const std::size_t bit_index =
                        y * mask_row_bytes * 8 + x;
                    const bool transparent =
                        (mask[bit_index / 8] >> (7 - (bit_index % 8))) & 1U;
                    pixels[(y * static_cast<std::size_t>(width) + x) *
                               bytes_per_pixel +
                           3] = transparent ? 0 : 255;
                }
            }
        } else {
            for (std::size_t offset = bytes_per_pixel - 1;
                 offset < pixels.size();
                 offset += bytes_per_pixel) {
                pixels[offset] = 255;
            }
        }
    }

    // The icon bits arrive premultiplied; convert to straight alpha, which is
    // what PNG and GDI+'s PixelFormat32bppARGB expect. Opaque pixels (the
    // common case) are unaffected.
    for (std::size_t offset = 0; offset < pixels.size();
         offset += bytes_per_pixel) {
        const auto alpha = pixels[offset + 3];
        if (alpha == 0 || alpha == 255) {
            continue;
        }
        for (std::size_t channel = 0; channel < 3; ++channel) {
            auto& value = pixels[offset + channel];
            value = static_cast<std::uint8_t>(std::min(
                255,
                (static_cast<int>(value) * 255 + alpha / 2) / alpha));
        }
    }

    Gdiplus::Bitmap png_source(
        width,
        height,
        width * static_cast<int>(bytes_per_pixel),
        PixelFormat32bppARGB,
        pixels.data());
    if (png_source.GetLastStatus() != Gdiplus::Ok) {
        return {};
    }
    CLSID png_encoder{};
    if (!find_png_encoder(png_encoder)) {
        return {};
    }
    StreamGuard stream;
    if (!stream.create()) {
        return {};
    }
    if (png_source.Save(stream.get(), &png_encoder, nullptr) !=
        Gdiplus::Ok) {
        return {};
    }
    STATSTG stream_info{};
    if (stream.get()->Stat(&stream_info, STATFLAG_NONAME) != S_OK ||
        stream_info.cbSize.QuadPart <= 0 ||
        static_cast<std::size_t>(stream_info.cbSize.QuadPart) >
            maximum_png_bytes) {
        return {};
    }
    const auto png_bytes =
        static_cast<std::size_t>(stream_info.cbSize.QuadPart);
    const LARGE_INTEGER beginning{};
    if (stream.get()->Seek(beginning, STREAM_SEEK_SET, nullptr) != S_OK) {
        return {};
    }
    std::string png_buffer(png_bytes, '\0');
    ULONG bytes_read = 0;
    if (stream.get()->Read(
            png_buffer.data(),
            static_cast<ULONG>(png_bytes),
            &bytes_read) != S_OK ||
        bytes_read != png_bytes) {
        return {};
    }
    std::string data_url = base64_encode(
        reinterpret_cast<const std::uint8_t*>(png_buffer.data()),
        png_bytes);
    data_url.insert(0, "data:image/png;base64,");
    if (data_url.size() > maximum_data_url_length) {
        return {};
    }
    return data_url;
}

}  // namespace

std::string extract_process_icon_data_url(const std::wstring& image_path) {
    auto& cache = icon_cache();
    std::wstring cache_key;
    cache_key.reserve(image_path.size());
    for (const wchar_t character : image_path) {
        cache_key.push_back(
            static_cast<wchar_t>(towlower(character)));
    }
    const auto cached = cache.find(cache_key);
    if (cached != cache.end()) {
        return cached->second;
    }

    SHFILEINFOW shell_info{};
    std::string data_url;
    if (SHGetFileInfoW(
            image_path.c_str(),
            0,
            &shell_info,
            sizeof(shell_info),
            SHGFI_ICON | SHGFI_LARGEICON) != 0 &&
        shell_info.hIcon != nullptr) {
        data_url = encode_icon_png_data_url(shell_info.hIcon);
        DestroyIcon(shell_info.hIcon);
    }
    cache.emplace(std::move(cache_key), data_url);
    return data_url;
}

}  // namespace timetransle::controller
