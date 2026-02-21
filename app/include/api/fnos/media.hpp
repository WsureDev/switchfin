/*
    Copyright 2024 WsureDev
    fnOS (飞牛影视) media API routes and data structures
*/

#pragma once

#include <nlohmann/json.hpp>
#include <api/jellyfin/media.hpp>

namespace fnos {

// Media API routes
const std::string apiItemList   = "/v/api/v1/item/list";
const std::string apiEpisodeList = "/v/api/v1/episode/list/{}";
const std::string apiPlayInfo   = "/v/api/v1/play/info";
const std::string apiPlayQuality = "/v/api/v1/play/quality";
const std::string apiStreamList = "/v/api/v1/stream/list/{}";
const std::string apiMediaRange = "/v/api/v1/media/range/{}";
const std::string apiItemWatched = "/v/api/v1/item/watched";
const std::string apiPlayRecord = "/v/api/v1/play/record";
const std::string apiGetStream  = "/v/api/v1/stream";
const std::string apiSubtitleDl = "/v/api/v1/subtitle/dl/{}";

// fnOS item type values
const int ITEM_TYPE_MOVIE   = 1;
const int ITEM_TYPE_SERIES  = 2;
const int ITEM_TYPE_SEASON  = 3;
const int ITEM_TYPE_EPISODE = 4;
const int ITEM_TYPE_FOLDER  = 5;

/// @brief A media item entry returned by item/list and episode/list
struct PlayListItem {
    std::string guid;
    std::string lan;
    std::string title;
    std::string tv_title;
    std::string parent_guid;
    std::string parent_title;
    int         type          = 0;
    std::string poster;       ///< URL path relative to server base
    int         poster_width  = 0;
    int         poster_height = 0;
    float       vote_average  = 0.0f;
    int         runtime       = 0;  ///< seconds
    std::string overview;
    bool        is_favorite   = false;
    int         season_number  = 0;
    int         episode_number = 0;
    int64_t     duration      = 0;  ///< milliseconds
    int64_t     ts            = 0;  ///< play progress in milliseconds
    std::string imdb_id;
    std::string douban_id;
    std::string trim_id;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PlayListItem, guid, lan, title, tv_title,
    parent_guid, parent_title, type, poster, poster_width, poster_height,
    vote_average, runtime, overview, is_favorite, season_number, episode_number,
    duration, ts, imdb_id, douban_id, trim_id);

/// @brief Minimal item info embedded inside PlayInfo
struct PlayItem {
    std::string guid;
    std::string title;
    std::string tv_title;
    std::string parent_title;
    int         season_number  = 0;
    int         episode_number = 0;
    int64_t     duration      = 0;
    std::string poster;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PlayItem, guid, title, tv_title, parent_title,
    season_number, episode_number, duration, poster);

struct PlayConfig {
    int skip_opening = 0;
    int skip_ending  = 0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PlayConfig, skip_opening, skip_ending);

/// @brief Response from /v/api/v1/play/info
struct PlayInfo {
    std::string guid;       ///< media_guid (for stream URL)
    PlayItem    item;
    int64_t     ts          = 0;  ///< play progress in milliseconds
    PlayConfig  play_config;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PlayInfo, guid, item, ts, play_config);

struct StreamFile {
    std::string guid;
    std::string name;
    int64_t     size = 0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(StreamFile, guid, name, size);

struct StreamInfo {
    int         index      = 0;
    std::string codec;
    std::string title;
    std::string language;
    bool        is_default = false;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(StreamInfo, index, codec, title, language, is_default);

/// @brief Response from /v/api/v1/stream/list/{itemGuid}
struct StreamListResponse {
    std::vector<StreamFile> files;
    std::vector<StreamInfo> video_streams;
    std::vector<StreamInfo> audio_streams;
    std::vector<StreamInfo> subtitle_streams;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(StreamListResponse, files, video_streams,
    audio_streams, subtitle_streams);

/// @brief Threshold (in percent) above which an item is considered "watched"
static constexpr int WATCHED_THRESHOLD_PERCENT = 95;

/// @brief Convert a fnOS PlayListItem to a Jellyfin-compatible Episode for existing UI reuse.
/// Poster URLs are stored with a special marker prefix so video_source.cpp can detect them.
inline jellyfin::Episode toJellyfinEpisode(const PlayListItem& item) {
    jellyfin::Episode ep;
    ep.Id               = item.guid;
    ep.Name             = item.title.empty() ? item.tv_title : item.title;
    ep.IndexNumber      = item.episode_number;
    ep.ParentIndexNumber = item.season_number;
    ep.SeriesName       = item.tv_title;
    ep.Overview         = item.overview;
    // duration is in ms; 1 tick = 100 ns → 1 ms = 10000 ticks
    ep.RunTimeTicks     = static_cast<uint64_t>(item.duration) * 10000LL;
    ep.UserData.PlaybackPositionTicks = static_cast<int64_t>(item.ts) * 10000LL;
    ep.UserData.IsFavorite = item.is_favorite;
    ep.UserData.Played  = (item.ts > 0 && item.duration > 0 &&
                           item.ts >= item.duration * WATCHED_THRESHOLD_PERCENT / 100);

    // Map fnOS type to Jellyfin type string
    switch (item.type) {
    case ITEM_TYPE_MOVIE:   ep.Type = jellyfin::mediaTypeMovie;   break;
    case ITEM_TYPE_SERIES:  ep.Type = jellyfin::mediaTypeSeries;  break;
    case ITEM_TYPE_SEASON:  ep.Type = jellyfin::mediaTypeSeason;  break;
    case ITEM_TYPE_EPISODE: ep.Type = jellyfin::mediaTypeEpisode; break;
    case ITEM_TYPE_FOLDER:  ep.Type = jellyfin::mediaTypeFolder;  break;
    default:                ep.Type = jellyfin::mediaTypeFolder;  break;
    }

    // Store the fnOS poster URL path in ImageTags.
    // Use the path directly (starts with "/") so callers can distinguish it from
    // a Jellyfin image tag hash and use Image::with() instead of Image::load().
    if (!item.poster.empty()) {
        ep.ImageTags[jellyfin::imageTypePrimary] = item.poster;
        // Episodes may be displayed with thumb layout too
        if (item.type == ITEM_TYPE_EPISODE)
            ep.ImageTags[jellyfin::imageTypeThumb] = item.poster;
    }

    return ep;
}

}  // namespace fnos
