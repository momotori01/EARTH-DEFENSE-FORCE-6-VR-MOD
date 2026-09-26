#pragma once
#include <cstdint>
#include <cstring>

namespace multislot {
// Read-only startup diagnostic; no runtime patching and no API calls through
// the proxy. The package, separately, pins the complete loader file hash.
inline const char* LoaderProxyStyle(const void* function) {
    if (!function) return "unavailable";
    __try {
        const auto p = static_cast<const std::uint8_t*>(function);
        if (!std::memcmp(p, "\x48\x8b\x05", 3)) {
            if (p[7] == 0xff && p[8] == 0xe0) return "direct-register";
            if (!std::memcmp(p + 7, "\x48\x89\x05", 3) && p[14] == 0xe9) return "shared-dispatch (legacy)";
        }
    } __except (1) {
    }
    return "other";
}
}
