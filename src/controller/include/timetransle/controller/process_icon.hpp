#pragma once

#include <string>

namespace timetransle::controller {

// Returns the executable icon of `image_path` as a PNG data URL
// ("data:image/png;base64,..."), or an empty string when the icon cannot be
// extracted or the encoded payload exceeds the WebView contract limit.
// Results are cached per case-insensitive path; the list is only requested
// from the UI thread, so the cache needs no synchronization.
[[nodiscard]] std::string extract_process_icon_data_url(
    const std::wstring& image_path);

}  // namespace timetransle::controller
