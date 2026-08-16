// Host-native tests for the pure core logic (no Geode dependency).
// Build & run:  clang++ -std=c++20 -o /tmp/core_tests mod/test/core_tests.cpp && /tmp/core_tests

#include "../src/core/resolve.hpp"
#include "../src/core/wire.hpp"

#include <cstdio>
#include <cstdlib>

static int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

using namespace autodeafen;

static void testThresholdResolution() {
    // opt-in: no config or disabled config -> inert
    CHECK(!resolveThreshold(nullptr, 0, 0));
    LevelConfig off;
    off.enabled = false;
    off.sp[0] = 50;
    CHECK(!resolveThreshold(&off, 0, 0));

    LevelConfig cfg;
    cfg.enabled = true;
    cfg.sp[0] = 40;
    cfg.sp[3] = 75;

    // exact lookups
    CHECK(resolveThreshold(&cfg, 0, 0) == 40);
    CHECK(resolveThreshold(&cfg, 3, 60) == 75);

    // no entry for this startpos -> no deafen (no fallback)
    CHECK(!resolveThreshold(&cfg, 1, 0));
    CHECK(!resolveThreshold(&cfg, -1, 0));

    // arming guard: threshold must exceed spawn percent
    CHECK(!resolveThreshold(&cfg, 3, 75));
    CHECK(!resolveThreshold(&cfg, 3, 80));
    CHECK(resolveThreshold(&cfg, 3, 74) == 75);

    // bounds sanity
    LevelConfig weird;
    weird.enabled = true;
    weird.sp[0] = 0;
    weird.sp[1] = 101;
    weird.sp[2] = 1;
    weird.sp[3] = 100;
    CHECK(!resolveThreshold(&weird, 0, 0));
    CHECK(!resolveThreshold(&weird, 1, 0));
    CHECK(resolveThreshold(&weird, 2, 0) == 1);
    CHECK(resolveThreshold(&weird, 3, 99) == 100);
}

static void testSha256() {
    using autodeafen::wire::sha256;
    auto hex = [](std::array<uint8_t, 32> const& d) {
        std::string out;
        for (auto b : d) {
            char buf[3];
            std::snprintf(buf, sizeof(buf), "%02x", b);
            out += buf;
        }
        return out;
    };
    CHECK(hex(sha256("", 0))
          == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(hex(sha256("abc", 3))
          == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::string fox = "The quick brown fox jumps over the lazy dog";
    CHECK(hex(sha256(fox.data(), fox.size()))
          == "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
    // 56 bytes: forces the padding into a second block
    std::string s56(56, 'a');
    CHECK(hex(sha256(s56.data(), s56.size()))
          == "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
}

static void testBase64Url() {
    using autodeafen::wire::base64UrlEncode;
    auto enc = [](std::string_view s) {
        return base64UrlEncode(reinterpret_cast<uint8_t const*>(s.data()), s.size());
    };
    CHECK(enc("") == "");
    CHECK(enc("f") == "Zg");          // no padding
    CHECK(enc("fo") == "Zm8");
    CHECK(enc("foo") == "Zm9v");
    // url-safe alphabet: 0xfb 0xff -> "-_8" in url encoding ("+/8" in standard)
    uint8_t urlBytes[] = {0xfb, 0xff};
    CHECK(base64UrlEncode(urlBytes, 2) == "-_8");

    // PKCE S256 known vector from RFC 7636 appendix B
    std::string verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
    auto digest = autodeafen::wire::sha256(verifier.data(), verifier.size());
    CHECK(base64UrlEncode(digest.data(), digest.size())
          == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
}

static void testIpcFraming() {
    using namespace autodeafen::wire;
    auto h = ipcHeader(IpcOp::Handshake, 0x01020304);
    uint8_t raw[8];
    std::memcpy(raw, h.data(), 8);
    CHECK(raw[0] == 0 && raw[1] == 0 && raw[2] == 0 && raw[3] == 0);  // op 0 LE
    CHECK(raw[4] == 0x04 && raw[5] == 0x03 && raw[6] == 0x02 && raw[7] == 0x01);

    auto parsed = parseIpcHeader(raw);
    CHECK(parsed.op == IpcOp::Handshake && parsed.length == 0x01020304);

    uint8_t frame[8] = {1, 0, 0, 0, 42, 0, 0, 0};
    auto f = parseIpcHeader(frame);
    CHECK(f.op == IpcOp::Frame && f.length == 42);
}

int main() {
    testThresholdResolution();
    testSha256();
    testBase64Url();
    testIpcFraming();
    if (g_failures) {
        std::printf("%d check(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
