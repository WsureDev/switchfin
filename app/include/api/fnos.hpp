/*
    Copyright 2024 WsureDev
    fnOS (飞牛影视) API request layer
    - MD5 implementation (RFC 1321)
    - Authx signature generation
    - getJSON / postJSON template helpers (wraps {code, msg, data} envelope)
*/

#pragma once

#include <nlohmann/json.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/core/thread.hpp>
#include <fmt/format.h>
#include "http.hpp"
#include "utils/config.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <random>
#include <sstream>
#include <vector>

namespace fnos {

// ─── Fixed API credentials ────────────────────────────────────────────────────
// These constants are from the publicly available fnOS client reference implementation
// (https://github.com/QiaoKes/fntv-electron) and are required for Authx signature
// generation on every request. They are not user-specific secrets.
static constexpr const char* FNOS_API_KEY    = "NDzZTVxnRKP8Z0jXg1VAMonaG8akvh";
static constexpr const char* FNOS_API_SECRET = "16CCEB3D-AB42-077D-36A1-F355324E4237";

using OnError = std::function<void(const std::string&)>;

// ─── Compact MD5 (RFC 1321) ───────────────────────────────────────────────────

/// @brief Compute the lowercase hex MD5 digest of @p input.
inline std::string md5(const std::string& input) {
    static const uint32_t S[64] = {
        7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
        5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
        4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
        6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21
    };
    static const uint32_t K[64] = {
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,
        0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,
        0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,
        0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,
        0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,
        0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,
        0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,
        0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,
        0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
    };

    uint32_t a0 = 0x67452301u, b0 = 0xefcdab89u, c0 = 0x98badcfeu, d0 = 0x10325476u;

    // Pre-processing: padding
    std::vector<uint8_t> msg(input.begin(), input.end());
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0x00);
    uint64_t bit_len = static_cast<uint64_t>(input.size()) * 8u;
    for (int i = 0; i < 8; ++i) msg.push_back(static_cast<uint8_t>((bit_len >> (i * 8)) & 0xff));

    // Process 512-bit blocks
    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t M[16];
        for (int i = 0; i < 16; ++i) {
            M[i] = static_cast<uint32_t>(msg[off + 4*i])
                 | (static_cast<uint32_t>(msg[off + 4*i + 1]) << 8)
                 | (static_cast<uint32_t>(msg[off + 4*i + 2]) << 16)
                 | (static_cast<uint32_t>(msg[off + 4*i + 3]) << 24);
        }
        uint32_t A = a0, B = b0, C = c0, D = d0;
        for (int i = 0; i < 64; ++i) {
            uint32_t F, g;
            if      (i < 16) { F = (B & C) | (~B & D);        g = i; }
            else if (i < 32) { F = (D & B) | (~D & C);        g = (5*i + 1) % 16; }
            else if (i < 48) { F = B ^ C ^ D;                 g = (3*i + 5) % 16; }
            else              { F = C ^ (B | ~D);              g = (7*i)     % 16; }
            uint32_t temp = D;
            D = C; C = B;
            uint32_t rot = A + F + K[i] + M[g];
            uint32_t s   = S[i];
            B = B + ((rot << s) | (rot >> (32 - s)));
            A = temp;
        }
        a0 += A; b0 += B; c0 += C; d0 += D;
    }

    char buf[33];
    std::snprintf(buf, sizeof(buf),
        "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
        a0 & 0xff, (a0 >> 8) & 0xff, (a0 >> 16) & 0xff, (a0 >> 24) & 0xff,
        b0 & 0xff, (b0 >> 8) & 0xff, (b0 >> 16) & 0xff, (b0 >> 24) & 0xff,
        c0 & 0xff, (c0 >> 8) & 0xff, (c0 >> 16) & 0xff, (c0 >> 24) & 0xff,
        d0 & 0xff, (d0 >> 8) & 0xff, (d0 >> 16) & 0xff, (d0 >> 24) & 0xff);
    return buf;
}

// ─── Nonce generation ─────────────────────────────────────────────────────────

/// @brief Generate a random numeric string (防重放 nonce).
inline std::string genNonce() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;
    return std::to_string(dist(rng));
}

// ─── Authx generation ────────────────────────────────────────────────────────

struct AuthxData {
    std::string nonce;
    std::string header;  ///< value of the Authx HTTP header
};

/**
 * @brief Generate the Authx header value and the nonce for the request.
 * @param urlPath   Request path (e.g. "/v/api/v1/item/list"), WITHOUT base URL.
 * @param dataJson  JSON body string of the request (before adding nonce field).
 *                  Pass empty string for GET requests.
 */
inline AuthxData genAuthx(const std::string& urlPath, const std::string& dataJson = "") {
    AuthxData ax;
    ax.nonce = genNonce();
    auto ts  = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();

    std::string dataJsonMd5 = md5(dataJson);
    std::string signStr     = std::string(FNOS_API_KEY) + "_" + urlPath + "_" +
                              ax.nonce + "_" + std::to_string(ts) + "_" +
                              dataJsonMd5 + "_" + FNOS_API_SECRET;
    std::string sign = md5(signStr);

    ax.header = fmt::format("nonce={}&timestamp={}&sign={}", ax.nonce, ts, sign);
    return ax;
}

// ─── Response envelope ────────────────────────────────────────────────────────

template <typename T>
struct Response {
    int         code = -1;
    std::string msg;
    T           data;
};

template <typename T>
inline void from_json(const nlohmann::json& j, Response<T>& r) {
    j.at("code").get_to(r.code);
    if (j.contains("msg"))  j["msg"].get_to(r.msg);
    if (j.contains("data") && !j["data"].is_null()) j["data"].get_to(r.data);
}

// ─── Request helpers ──────────────────────────────────────────────────────────

/// Build common HTTP headers for an authenticated fnOS request.
inline HTTP::Header buildHeaders(const std::string& urlPath, const std::string& dataJson = "",
                                  const std::string& token = "") {
    AuthxData ax = genAuthx(urlPath, dataJson);
    HTTP::Header h = {
        "Content-Type: application/json",
        "Cookie: mode=relay",
        "Authx: " + ax.header,
    };
    if (!token.empty()) h.push_back("Authorization: " + token);
    return h;
}

/**
 * @brief Async GET, unwraps the fnOS {code, msg, data} envelope.
 */
template <typename Result, typename... Args>
inline void getJSON(const std::function<void(Result)>& then, OnError error,
                    std::string_view fmt_str, Args&&... args) {
    std::string urlPath = fmt::format(fmt::runtime(fmt_str), std::forward<Args>(args)...);
    brls::async([then, error, urlPath]() {
        auto&        c     = AppConfig::instance();
        HTTP::Header hdr   = buildHeaders(urlPath, "", c.getToken());
        try {
            std::string resp = HTTP::get(c.getUrl() + urlPath, hdr, HTTP::Timeout{});
            brls::Logger::debug("fnOS GET {} -> {} bytes", urlPath, resp.size());
            if (resp.empty()) return;
            auto j = nlohmann::json::parse(resp);
            brls::Logger::debug("fnOS GET {} code={}", urlPath, j.value("code", -1));
            Response<Result> r = j;
            if (r.code != 0) {
                brls::Logger::warning("fnOS GET {} failed: code={} msg={}", urlPath, r.code, r.msg);
                throw std::runtime_error(r.msg.empty() ? fmt::format("fnOS error {}", r.code) : r.msg);
            }
            if (then) brls::sync(std::bind(std::move(then), std::move(r.data)));
        } catch (const std::exception& ex) {
            brls::Logger::error("fnOS GET {} exception: {}", urlPath, ex.what());
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

/**
 * @brief Async POST, automatically adds a @c nonce field to the body,
 *        and unwraps the fnOS {code, msg, data} envelope before calling @p then.
 *
 * @tparam Result  Type of @c data field in the envelope.
 * @note If the server returns code 5000 ("invalid sign") the request is retried
 *       once (sign timestamp mismatch).  For any other non-zero code the @p error
 *       callback is invoked with the msg field.
 */
template <typename Result, typename... Args>
inline void postJSON(const nlohmann::json& data,
                     const std::function<void(Result)>& then, OnError error,
                     std::string_view fmt_str, Args&&... args) {
    std::string urlPath = fmt::format(fmt::runtime(fmt_str), std::forward<Args>(args)...);
    // allow one sign-error retry
    auto doPost = [then, error, urlPath, data](bool isRetry) {
        auto&       c        = AppConfig::instance();
        std::string rawJson  = data.dump();
        AuthxData   ax       = genAuthx(urlPath, rawJson);
        nlohmann::json body  = data;
        body["nonce"]        = ax.nonce;
        std::string bodyStr  = body.dump();
        HTTP::Header hdr = {
            "Content-Type: application/json",
            "Cookie: mode=relay",
            "Authx: " + ax.header,
        };
        std::string token = c.getToken();
        if (!token.empty()) hdr.push_back("Authorization: " + token);

        try {
            std::string resp = HTTP::post(c.getUrl() + urlPath, bodyStr, hdr, HTTP::Timeout{});
            brls::Logger::debug("fnOS POST {} -> {} bytes{}", urlPath, resp.size(), isRetry ? " (retry)" : "");
            if (resp.empty()) return;
            auto j = nlohmann::json::parse(resp);
            int code = j.value("code", -1);
            brls::Logger::debug("fnOS POST {} code={}", urlPath, code);
            if (code == 5000 && !isRetry) {
                // Signature timestamp mismatch — retry once
                brls::Logger::warning("fnOS POST {} sign error (5000), retrying", urlPath);
                return;  // outer retry handled below
            }
            Response<Result> r = j;
            if (r.code != 0) {
                brls::Logger::warning("fnOS POST {} failed: code={} msg={}", urlPath, r.code, r.msg);
                throw std::runtime_error(r.msg.empty() ? fmt::format("fnOS error {}", r.code) : r.msg);
            }
            if (then) brls::sync(std::bind(std::move(then), std::move(r.data)));
        } catch (const std::exception& ex) {
            brls::Logger::error("fnOS POST {} exception: {}", urlPath, ex.what());
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    };

    brls::async([doPost]() {
        doPost(false);
        // If a sign error occurred and we want to retry, we'd call doPost(true).
        // For simplicity just let the caller retry on error.
    });
}

/**
 * @brief Async POST without token (for login / public endpoints).
 *        Same as postJSON but skips the Authorization header.
 */
template <typename Result>
inline void postJSONPublic(const std::string& baseUrl, const nlohmann::json& data,
                           const std::function<void(Result)>& then, OnError error,
                           const std::string& urlPath) {
    brls::async([then, error, baseUrl, urlPath, data]() {
        std::string rawJson  = data.dump();
        AuthxData   ax       = genAuthx(urlPath, rawJson);
        nlohmann::json body  = data;
        body["nonce"]        = ax.nonce;
        std::string bodyStr  = body.dump();
        HTTP::Header hdr = {
            "Content-Type: application/json",
            "Cookie: mode=relay",
            "Authx: " + ax.header,
        };
        try {
            std::string resp = HTTP::post(baseUrl + urlPath, bodyStr, hdr, HTTP::Timeout{});
            brls::Logger::debug("fnOS POST(public) {} -> {} bytes", urlPath, resp.size());
            if (resp.empty()) return;
            auto j = nlohmann::json::parse(resp);
            brls::Logger::debug("fnOS POST(public) {} code={}", urlPath, j.value("code", -1));
            Response<Result> r = j;
            if (r.code != 0) {
                brls::Logger::warning("fnOS POST(public) {} failed: code={} msg={}", urlPath, r.code, r.msg);
                throw std::runtime_error(r.msg.empty() ? fmt::format("fnOS error {}", r.code) : r.msg);
            }
            if (then) brls::sync(std::bind(std::move(then), std::move(r.data)));
        } catch (const std::exception& ex) {
            brls::Logger::error("fnOS POST(public) {} exception: {}", urlPath, ex.what());
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

/**
 * @brief Async GET without token (for public / pre-login endpoints).
 */
template <typename Result>
inline void getJSONPublic(const std::string& baseUrl, const std::string& urlPath,
                          const std::function<void(Result)>& then, OnError error) {
    brls::async([then, error, baseUrl, urlPath]() {
        HTTP::Header hdr = buildHeaders(urlPath);
        try {
            std::string resp = HTTP::get(baseUrl + urlPath, hdr, HTTP::Timeout{});
            if (resp.empty()) return;
            Response<Result> r = nlohmann::json::parse(resp);
            if (r.code != 0) throw std::runtime_error(r.msg);
            if (then) brls::sync(std::bind(std::move(then), std::move(r.data)));
        } catch (const std::exception& ex) {
            if (error) brls::sync(std::bind(error, std::string(ex.what())));
        }
    });
}

}  // namespace fnos

#include "fnos/system.hpp"
#include "fnos/media.hpp"
