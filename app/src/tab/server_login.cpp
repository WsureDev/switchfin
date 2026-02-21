/*
    Copyright 2024 WsureDev
    fnOS server login (replaces Jellyfin authenticatebyname)
*/

#include "tab/server_login.hpp"
#include "activity/main_activity.hpp"
#include "api/fnos.hpp"
#include "api/analytics.hpp"
#include "utils/dialog.hpp"

using namespace brls::literals;  // for _i18n

ServerLogin::ServerLogin(const std::string& name, const std::string& url, const std::string& user)
    : url(url) {
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/server_login.xml");
    brls::Logger::debug("ServerLogin: create {}", url);

    this->hdrSigin->setTitle(brls::getStr("main/setting/server/sigin_to", name));
    this->inputUser->init("main/setting/username"_i18n, user);
    this->inputPass->init("main/setting/password"_i18n, "", [](std::string /*text*/) {}, "", "", 256);

    this->btnSignin->registerClickAction([this](...) { return this->onSignin(); });
    // QuickConnect is not supported by fnOS
    this->btnQuickConnect->setVisibility(brls::Visibility::GONE);

    // fnOS has no branding/disclaimer endpoint
    this->labelDisclaimer->setVisibility(brls::Visibility::INVISIBLE);
}

ServerLogin::~ServerLogin() { brls::Logger::debug("ServerLogin Activity: delete"); }

// Stub required by the header (fnOS has no branding endpoint)
void ServerLogin::Disclaimer() {}

bool ServerLogin::onSignin() {
    std::string username = inputUser->getValue();
    std::string password = inputPass->getValue();
    if (username.empty()) {
        Dialog::show("Username is empty");
        return false;
    }

    brls::Application::blockInputs();
    this->btnSignin->setState(brls::ButtonState::DISABLED);

    nlohmann::json data = {
        {"app_name", "trimemedia-web"},
        {"username", username},
        {"password", password},
    };

    std::string loginUrl = this->url;
    ASYNC_RETAIN
    fnos::postJSONPublic<fnos::LoginResult>(
        loginUrl, data,
        [ASYNC_TOKEN, loginUrl](const fnos::LoginResult& r) {
            ASYNC_RELEASE
            if (r.token.empty()) {
                this->btnSignin->setState(brls::ButtonState::ENABLED);
                brls::Application::unblockInputs();
                Dialog::show("Login failed: empty token");
                return;
            }

            // Fetch user info to get the user ID and name
            fnos::AuthxData ax  = fnos::genAuthx(fnos::apiUserInfo);
            HTTP::Header hdr = {
                "Content-Type: application/json",
                "Cookie: mode=relay",
                "Authx: " + ax.header,
                "Authorization: " + r.token,
            };
            try {
                auto resp = HTTP::get(loginUrl + fnos::apiUserInfo, hdr, HTTP::Timeout{});
                fnos::Response<fnos::UserInfo> userResp = nlohmann::json::parse(resp);
                if (userResp.code != 0) throw std::runtime_error(userResp.msg);

                const fnos::UserInfo& info = userResp.data;
                std::string displayName = info.nickname.empty() ? info.username : info.nickname;

                AppUser u = {
                    .id           = std::to_string(info.uid),
                    .name         = displayName,
                    .access_token = r.token,
                    // Use the server URL as the stable server identifier
                    .server_id    = loginUrl,
                };

                brls::sync([ASYNC_TOKEN, u, loginUrl]() {
                    ASYNC_RELEASE
                    AppConfig::instance().addUser(u, loginUrl);
                    this->btnSignin->setState(brls::ButtonState::ENABLED);
                    brls::Application::unblockInputs();
                    brls::Application::clear();
                    brls::Application::pushActivity(new MainActivity(), brls::TransitionAnimation::NONE);
                    GA("login", {{"method", {loginUrl}}});
                });
            } catch (const std::exception& ex) {
                std::string msg = ex.what();
                brls::sync([ASYNC_TOKEN, msg]() {
                    ASYNC_RELEASE
                    this->btnSignin->setState(brls::ButtonState::ENABLED);
                    brls::Application::unblockInputs();
                    Dialog::show(msg);
                });
            }
        },
        [ASYNC_TOKEN](const std::string& msg) {
            ASYNC_RELEASE
            this->btnSignin->setState(brls::ButtonState::ENABLED);
            brls::Application::unblockInputs();
            Dialog::show(msg);
        },
        fnos::apiLogin);
    return true;
}

// QuickConnect is not supported by fnOS
void ServerLogin::doQuickLogin() {}
