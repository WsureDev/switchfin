#include "activity/player_view.hpp"
#include "api/jellyfin.hpp"
#include "api/fnos.hpp"
#include "utils/dialog.hpp"
#include "utils/misc.hpp"
#include "view/danmaku_core.hpp"
#include "view/mpv_core.hpp"
#include "view/player_setting.hpp"
#include "view/video_view.hpp"
#include "view/video_profile.hpp"
#include <tinyxml2.h>

using namespace brls::literals;

PlayerView::PlayerView(const jellyfin::Item& item, const uint64_t seekTicks) : itemId(item.Id), itemType(item.Type) {
    float width = brls::Application::contentWidth;
    float height = brls::Application::contentHeight;
    view = new VideoView();
    view->setDimensions(width, height);
    view->setWidthPercentage(100);
    view->setHeightPercentage(100);
    view->setId("video");
    this->setDimensions(width, height);
    this->addView(view);
    view->registerVideoQuality([this](...) { return this->toggleQuality(); });

    auto& mpv = MPVCore::instance();

    brls::Application::pushActivity(new brls::Activity(this), brls::TransitionAnimation::NONE);

    playSubscribeID = view->getPlayEvent()->subscribe([this](int index) { this->playIndex(index); });

    settingSubscribeID = view->getSettingEvent()->subscribe([this]() {
        brls::View* setting = new PlayerSetting(&this->stream);
        brls::Application::pushActivity(new brls::Activity(setting));
    });

    eventSubscribeID = mpv.getEvent()->subscribe([this](MpvEventEnum event) {
        auto& mpv = MPVCore::instance();
        // brls::Logger::info("mpv event => : {}", event);
        switch (event) {
        case MpvEventEnum::MPV_RESUME:
            this->reportPlay();
            view->getProfile()->init(this->playMethod);
            break;
        case MpvEventEnum::MPV_PAUSE:
            this->reportPlay(true);
            break;
        case MpvEventEnum::LOADING_END:
            this->reportStart();
            break;
        case MpvEventEnum::MPV_STOP:
            this->reportStop();
            break;
        case MpvEventEnum::MPV_LOADED: {
            auto& svr = AppConfig::instance().getUrl();
            const char* flag = MPVCore::SUBS_FALLBACK ? "select" : "auto";
            // 移除其他备用链接
            for (auto& s : this->stream.MediaStreams) {
                if (s.Type == jellyfin::streamTypeSubtitle) {
                    if (s.DeliveryUrl.size() > 0 && (s.IsExternal || this->playMethod == jellyfin::methodTranscode)) {
                        std::string url = svr + s.DeliveryUrl;
                        mpv.command("sub-add", url.c_str(), flag, s.DisplayTitle.c_str());
                    }
                }
            }
            if (PlayerSetting::selectedSubtitle > 0 && this->playMethod == jellyfin::methodDirectPlay) {
                mpv.setInt("sid", PlayerSetting::selectedSubtitle);
            }
            if (DanmakuCore::PLUGIN_ACTIVE && !this->stream.IsInfiniteStream) {
                this->requestDanmaku();
            }
            break;
        }
        case MpvEventEnum::UPDATE_PROGRESS:
            if (mpv.video_progress % 10 == 0) this->reportPlay();
            break;
        default:;
        }
    });
    // 自定义的mpv事件
    customEventSubscribeID = mpv.getCustomEvent()->subscribe([this](const std::string& event, void* data) {
        if (event == QUALITY_CHANGE) {
            this->playMedia(MPVCore::instance().playback_time * jellyfin::PLAYTICKS);
        } else if (event == SYNC_STOP) {
            VideoView::close();
        } else if (event == "PreviousTrack") {
            this->view->playNext(-1);
        } else if (event == "NextTrack") {
            this->view->playNext(1);
        }
    });

    this->setChapters(item.Chapters, item.RunTimeTicks);
    this->playMedia(seekTicks > 0 ? seekTicks : item.UserData.PlaybackPositionTicks);

    // Report stop when application exit
    this->exitSubscribeID = brls::Application::getExitEvent()->subscribe([this]() {
        if (!MPVCore::instance().isStopped()) this->reportStop();
    });
}

PlayerView::~PlayerView() {
    auto& mpv = MPVCore::instance();
    mpv.getEvent()->unsubscribe(eventSubscribeID);
    mpv.getCustomEvent()->unsubscribe(customEventSubscribeID);
    view->getPlayEvent()->unsubscribe(playSubscribeID);
    view->getSettingEvent()->unsubscribe(settingSubscribeID);

    brls::sync([&mpv]() { mpv.getCustomEvent()->fire(VIDEO_CLOSE, nullptr); });

    if (DanmakuCore::PLUGIN_ACTIVE) {
        DanmakuCore::instance().reset();
    }

    PlayerSetting::selectedSubtitle = 0;
    PlayerSetting::selectedAudio = 0;

    if (!mpv.isStopped()) this->reportStop();
    brls::Application::getExitEvent()->unsubscribe(this->exitSubscribeID);
    brls::Logger::debug("trying delete PlayerView...");
}

void PlayerView::setSeries(const std::string& seriesId) {
    ASYNC_RETAIN
    fnos::getJSON<std::vector<fnos::PlayListItem>>(
        [ASYNC_TOKEN](const std::vector<fnos::PlayListItem>& items) {
            ASYNC_RELEASE
            int index = -1;
            std::vector<std::string> values;
            std::vector<jellyfin::Episode> eps;
            eps.reserve(items.size());
            for (size_t i = 0; i < items.size(); i++) {
                auto& item = items.at(i);
                if (item.guid == this->itemId) index = static_cast<int>(i);
                values.push_back(fmt::format("S{}E{} - {}", item.season_number, item.episode_number, item.title));
                eps.push_back(fnos::toJellyfinEpisode(item));
            }
            view->setList(values, index);
            this->episodes = std::move(eps);
        },
        [ASYNC_TOKEN](const std::string& error) {
            ASYNC_RELEASE
            Dialog::show(error);
        },
        fnos::apiEpisodeList, seriesId);
}

void PlayerView::setTitie(const std::string& title) { this->view->setTitie(title); }

void PlayerView::setChapters(const std::vector<jellyfin::MediaChapter>& chaps, uint64_t duration) {
    std::vector<float> clips;
    for (auto& c : chaps) {
        clips.push_back(float(c.StartPositionTicks) / float(duration));
    }
    this->view->setClipPoint(clips);
}

bool PlayerView::playIndex(int index) {
    if (index < 0 || index >= (int)this->episodes.size()) {
        return VideoView::close();
    }
    MPVCore::instance().reset();

    auto item = this->episodes.at(index);
    this->itemId = item.Id;
    this->setChapters(item.Chapters, item.RunTimeTicks);
    this->playMedia(0);
    view->setTitie(fmt::format("S{}E{} - {}", item.ParentIndexNumber, item.IndexNumber, item.Name));
    return true;
}

void PlayerView::playMedia(const uint64_t seekTicks) {
    // fnOS: call play/info to get the media_guid, then stream from media/range/{media_guid}
    ASYNC_RETAIN
    fnos::postJSON<fnos::PlayInfo>(
        {{"item_guid", this->itemId}},
        [ASYNC_TOKEN, seekTicks](const fnos::PlayInfo& info) {
            ASYNC_RELEASE

            if (info.guid.empty()) {
                Dialog::show("fnOS: empty media_guid", []() { VideoView::close(); });
                return;
            }

            auto& mpv = MPVCore::instance();
            auto& svr = AppConfig::instance().getUrl();

            // Build the direct-play stream URL: /v/api/v1/media/range/{media_guid}
            std::string mediaPath = fmt::format(fmt::runtime(fnos::apiMediaRange), info.guid);
            std::string mediaUrl  = svr + mediaPath;

            // MPV options
            std::stringstream ssextra;
            ssextra << fmt::format("network-timeout={}", HTTP::TIMEOUT / 100);

            // Resume from saved position or explicit seekTicks
            uint64_t resumeTicks = seekTicks > 0 ? seekTicks
                                                  : static_cast<uint64_t>(info.ts) * 10000LL;
            if (resumeTicks > 0)
                ssextra << ",start=" << misc::sec2Time(resumeTicks / jellyfin::PLAYTICKS);

            if (HTTP::PROXY_STATUS) ssextra << ",http-proxy=\"" << HTTP::PROXY << "\"";

            // Pass the fnOS Authorization token as an HTTP header to MPV so that
            // range requests during streaming are authenticated.
            std::string token = AppConfig::instance().getToken();
            if (!token.empty()) {
                ssextra << ",http-header-fields=\"Authorization: " << token
                        << ",Cookie: mode=relay\"";
            }

            this->playMethod    = jellyfin::methodDirectPlay;
            this->playSessionId = info.guid;

            // Store minimal stream metadata for progress reporting
            this->stream.Id       = info.guid;
            this->stream.Name     = info.item.title;
            this->stream.Bitrate  = 0;

            brls::Logger::info("fnOS play: {}", mediaUrl);
            mpv.setUrl(mediaUrl, ssextra.str());
            view->getProfile()->init(this->playMethod);
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            Dialog::show(ex, []() { VideoView::close(); });
        },
        fnos::apiPlayInfo);
}

void PlayerView::reportStart() {
    // fnOS: record play position (ts in milliseconds)
    int64_t ts_ms = static_cast<int64_t>(MPVCore::instance().playback_time * 1000.0);
    fnos::postJSON<nlohmann::json>(
        {{"item_guid", this->itemId}, {"ts", ts_ms}},
        [](...) {}, nullptr, fnos::apiPlayRecord);
}

void PlayerView::reportStop() {
    // fnOS: save final play position before stopping
    int64_t ts_ms = static_cast<int64_t>(MPVCore::instance().playback_time * 1000.0);
    fnos::postJSON<nlohmann::json>(
        {{"item_guid", this->itemId}, {"ts", ts_ms}},
        [](...) {}, nullptr, fnos::apiPlayRecord);

    brls::Logger::debug("PlayerView reportStop {}", this->itemId);
    this->playSessionId.clear();
}

void PlayerView::reportPlay(bool isPaused) {
    if (isPaused) return;  // Only report progress while playing, not on pause
    int64_t ts_ms = static_cast<int64_t>(MPVCore::instance().video_progress * 1000.0);
    fnos::postJSON<nlohmann::json>(
        {{"item_guid", this->itemId}, {"ts", ts_ms}},
        [](...) {}, nullptr, fnos::apiPlayRecord);
}

/// 获取视频弹幕 (not supported on fnOS)
void PlayerView::requestDanmaku() {
    // fnOS does not have a Danmaku endpoint; disable danmaku display
    brls::sync([this]() {
        DanmakuCore::instance().reset();
        view->setDanmakuEnable(brls::Visibility::GONE);
    });
}

bool PlayerView::toggleQuality() {
    static std::set<std::string> codecs = {"hevc", "av1", "vp9"};
    std::vector<std::string> options = {"main/player/auto"_i18n};
    std::vector<int64_t> values = {0};
    int64_t videoBitRate = this->stream.Bitrate;
    if (videoBitRate <= 20000000) {
        for (const auto& stream : this->stream.MediaStreams) {
            if (stream.Type == "Video" && codecs.count(stream.Codec) > 0) {
                videoBitRate = round(videoBitRate * 1.5);
                break;
            }
        }
    }

    if (videoBitRate >= 15000000) options.push_back("20 Mbps"), values.push_back(20000000);
    if (videoBitRate >= 10000000) options.push_back("15 Mbps"), values.push_back(15000000);
    if (videoBitRate >= 8000000) options.push_back("10 Mbps"), values.push_back(10000000);
    if (videoBitRate >= 6000000) options.push_back("8 Mbps"), values.push_back(8000000);
    if (videoBitRate >= 4000000) options.push_back("6 Mbps"), values.push_back(6000000);
    if (videoBitRate >= 3000000) options.push_back("4 Mbps"), values.push_back(4000000);
    if (videoBitRate >= 1500000) options.push_back("3 Mbps"), values.push_back(3000000);
    if (videoBitRate >= 720000) options.push_back("1.5 Mbps"), values.push_back(1500000);
    options.push_back("720 kbps"), values.push_back(720000);
    options.push_back("420 kbps"), values.push_back(420000);

    auto it = std::find(values.begin(), values.end(), MPVCore::VIDEO_QUALITY);
    if (it == values.end()) it = values.begin();

    brls::Dropdown* dropdown = new brls::Dropdown(
        "main/player/quality"_i18n, options,
        [values](int selected) {
            MPVCore::VIDEO_QUALITY = values[selected];
            MPVCore::instance().getCustomEvent()->fire(QUALITY_CHANGE, nullptr);
            return true;
        },
        std::distance(values.begin(), it));

    brls::Application::pushActivity(new brls::Activity(dropdown));
    return true;
}
