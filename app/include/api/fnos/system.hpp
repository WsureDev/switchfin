/*
    Copyright 2024 WsureDev
    fnOS (飞牛影视) system/auth API routes and data structures
*/

#pragma once

#include <nlohmann/json.hpp>

namespace fnos {

// Auth / System API routes
const std::string apiLogin     = "/v/api/v1/login";
const std::string apiLogout    = "/v/api/v1/logout";
const std::string apiUserInfo  = "/v/api/v1/user/info";
const std::string apiSysConfig = "/v/api/v1/sys/config";

/// @brief Response from /v/api/v1/login
struct LoginResult {
    std::string token;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LoginResult, token);

/// @brief Response from /v/api/v1/user/info
struct UserInfo {
    int         uid      = 0;
    std::string username;
    std::string nickname;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(UserInfo, uid, username, nickname);

/// @brief Response from /v/api/v1/sys/config
struct SysConfig {
    std::string hostname;
    std::string version;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SysConfig, hostname, version);

}  // namespace fnos
