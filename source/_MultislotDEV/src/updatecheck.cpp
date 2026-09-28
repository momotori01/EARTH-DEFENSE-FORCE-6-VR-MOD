#include "updatecheck.h"

#include <winhttp.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>

#include "log.h"

namespace multislot {
namespace {

constexpr const wchar_t* kHost = L"api.github.com";
constexpr const wchar_t* kPath = L"/repos/momotori01/EARTH-DEFENSE-FORCE-6-VR-MOD/releases/latest";
constexpr const wchar_t* kAgent = L"EDF6MultiSlot";
constexpr DWORD kTimeoutMs = 8000;
constexpr std::size_t kNoticeChars = 64;
constexpr std::size_t kReplyBytes = 256 * 1024;  // a release JSON is a few KB; this is the ceiling

wchar_t notice[kNoticeChars]{};
std::atomic<bool> noticeReady{false};
wchar_t folder[MAX_PATH]{};

// Reads a whole file into `out`, up to `size - 1` bytes, and terminates it. False when it cannot.
bool ReadWholeFile(const wchar_t* path, char* out, std::size_t size, DWORD& read) {
    read = 0;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD chunk = 0;
    bool ok = true;
    while (read + 1 < size && ReadFile(file, out + read, static_cast<DWORD>(size - 1 - read), &chunk, nullptr) && chunk)
        read += chunk;
    if (read + 1 >= size) ok = false;  // bigger than we are willing to hold
    CloseHandle(file);
    out[read] = 0;
    return ok && read > 0;
}

// The first x.y.z at or after `at`.
Version VersionAt(const char* at) {
    Version v;
    for (; at && *at; ++at) {
        if (*at < '0' || *at > '9') continue;
        int a = -1, b = -1, c = -1, used = 0;
        if (sscanf_s(at, "%d.%d.%d%n", &a, &b, &c, &used) == 3 && used > 0 && a >= 0 && b >= 0 && c >= 0) {
            v.major = a;
            v.minor = b;
            v.patch = c;
            return v;
        }
        while (at[1] >= '0' && at[1] <= '9') ++at;  // skip the rest of a number that was not one
    }
    return v;
}

// The value of a JSON string field, without a JSON parser: "name" : "value".
const char* FieldValue(const char* json, const char* name) {
    const char* at = std::strstr(json, name);
    if (!at) return nullptr;
    at = std::strchr(at + std::strlen(name), ':');
    if (!at) return nullptr;
    at = std::strchr(at, '"');
    return at ? at + 1 : nullptr;
}

Version FromManifest() {
    wchar_t path[MAX_PATH]{};
    if (_snwprintf_s(path, _TRUNCATE, L"%ls\\EDF6VR\\PACKAGE_MANIFEST.json", folder) < 0) return {};
    static char text[64 * 1024];
    DWORD read = 0;
    if (!ReadWholeFile(path, text, sizeof(text), read)) return {};
    return VersionAt(FieldValue(text, "\"version\""));
}

// The DLL carries "EDF6VR x.y.z cockpit loading". A player on flat has it renamed to .disabled and never
// loaded, but the file still says which package they have.
Version FromDll() {
    static const wchar_t* const names[] = {L"EDF6VR.dll", L"EDF6VR.dll.disabled"};
    for (const wchar_t* name : names) {
        wchar_t path[MAX_PATH]{};
        if (_snwprintf_s(path, _TRUNCATE, L"%ls\\Mods\\Plugins\\%ls", folder, name) < 0) continue;
        HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) continue;
        // Scanned in overlapping blocks so the marker cannot be split across a boundary.
        constexpr std::size_t kBlock = 1 << 20, kOverlap = 64;
        static char block[kBlock + kOverlap];
        Version found;
        DWORD read = 0;
        std::size_t keep = 0;
        while (!found.valid() && ReadFile(file, block + keep, kBlock, &read, nullptr) && read) {
            const std::size_t have = keep + read;
            block[have < sizeof(block) ? have : sizeof(block) - 1] = 0;
            for (const char* at = block; (at = static_cast<const char*>(
                     std::memchr(at, 'E', static_cast<std::size_t>(block + have - at)))) != nullptr;
                 ++at) {
                if (static_cast<std::size_t>(block + have - at) < 16) break;
                if (std::memcmp(at, "EDF6VR ", 7) != 0) continue;
                const Version v = VersionAt(at + 7);
                if (v.valid()) {
                    found = v;
                    break;
                }
            }
            keep = have > kOverlap ? kOverlap : have;
            std::memmove(block, block + have - keep, keep);
        }
        CloseHandle(file);
        if (found.valid()) return found;
    }
    return {};
}

// One GET, into `out`. False on any failure at all - there is nothing to report and nothing to retry.
bool Fetch(char* out, std::size_t size) {
    bool ok = false;
    const HINTERNET session = WinHttpOpen(kAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                          WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    WinHttpSetTimeouts(session, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    if (const HINTERNET connection = WinHttpConnect(session, kHost, INTERNET_DEFAULT_HTTPS_PORT, 0)) {
        if (const HINTERNET request = WinHttpOpenRequest(connection, L"GET", kPath, nullptr, WINHTTP_NO_REFERER,
                                                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)) {
            if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(request, nullptr)) {
                DWORD status = 0, statusSize = sizeof(status);
                WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
                if (status == 200) {
                    DWORD total = 0, read = 0;
                    while (total + 1 < size &&
                           WinHttpReadData(request, out + total, static_cast<DWORD>(size - 1 - total), &read) && read)
                        total += read;
                    out[total] = 0;
                    ok = total > 0;
                }
            }
            WinHttpCloseHandle(request);
        }
        WinHttpCloseHandle(connection);
    }
    WinHttpCloseHandle(session);
    return ok;
}

DWORD WINAPI CheckThread(LPVOID) {
    const Version installed = InstalledVrVersion(folder);
    if (!installed.valid()) {
        Log("UPDATE no EDF6VR found beside the game; nothing to compare");
        return 0;
    }
    static char reply[kReplyBytes];
    if (!Fetch(reply, sizeof(reply))) {
        Log("UPDATE could not reach GitHub; nothing shown (this is not an error)");
        return 0;
    }
    const Version latest = VersionAt(FieldValue(reply, "\"tag_name\""));
    if (!latest.valid()) {
        Log("UPDATE the reply carried no version; nothing shown");
        return 0;
    }
    if (!latest.NewerThan(installed)) {
        Log("UPDATE EDF6VR %d.%d.%d is installed, %d.%d.%d is the latest; nothing shown", installed.major,
            installed.minor, installed.patch, latest.major, latest.minor, latest.patch);
        return 0;
    }
    _snwprintf_s(notice, _TRUNCATE, L"NEW EDF6VR %d.%d.%d → %d.%d.%d - Update_EDF6VR.bat", installed.major,
                 installed.minor, installed.patch, latest.major, latest.minor, latest.patch);
    noticeReady.store(true);
    Log("UPDATE EDF6VR %d.%d.%d is installed, %d.%d.%d is out; the menu says so", installed.major, installed.minor,
        installed.patch, latest.major, latest.minor, latest.patch);
    return 0;
}

}  // namespace

bool Version::NewerThan(const Version& other) const {
    if (!valid() || !other.valid()) return false;
    if (major != other.major) return major > other.major;
    if (minor != other.minor) return minor > other.minor;
    return patch > other.patch;
}

Version ParseVersion(const char* text) { return text ? VersionAt(text) : Version{}; }

Version InstalledVrVersion(const wchar_t* gameFolder) {
    if (gameFolder && gameFolder != folder) {
        const std::size_t length = wcslen(gameFolder);
        if (length >= MAX_PATH) return {};
        wmemcpy(folder, gameFolder, length + 1);
    }
    const Version fromManifest = FromManifest();
    return fromManifest.valid() ? fromManifest : FromDll();
}

const wchar_t* UpdateNotice() { return noticeReady.load() ? notice : L""; }

void SetUpdateNoticeForTest(const wchar_t* text) {
    if (!text || !text[0]) {
        notice[0] = 0;
        noticeReady.store(false);
        return;
    }
    _snwprintf_s(notice, _TRUNCATE, L"%ls", text);
    noticeReady.store(true);
}

void StartUpdateCheck(const wchar_t* gameFolder, bool enabled) {
    if (!enabled) {
        Log("Update check: off (Update/CheckEDF6VR=0); nothing is contacted");
        return;
    }
    if (!gameFolder) return;
    const std::size_t length = wcslen(gameFolder);
    if (length >= MAX_PATH) return;
    wmemcpy(folder, gameFolder, length + 1);
    if (const HANDLE thread = CreateThread(nullptr, 0, &CheckThread, nullptr, 0, nullptr)) {
        CloseHandle(thread);
        Log("Update check: asking GitHub once whether a newer EDF6VR package exists (read only; nothing is "
            "downloaded or changed). Update/CheckEDF6VR=0 turns it off");
    }
}

}  // namespace multislot
