/*
    Copyright 2024 WsureDev
    fnOS (飞牛影视) media API routes and data structures
*/

#pragma once

#include <nlohmann/json.hpp>
#include <api/jellyfin/media.hpp>

namespace fnos {

// Media API routes
const std::string apiItemList    = "/v/api/v1/item/list";
const std::string apiEpisodeList = "/v/api/v1/episode/list/{}";
const std::string apiPlayInfo    = "/v/api/v1/play/info";
const std::string apiPlayQuality = "/v/api/v1/play/quality";
const std::string apiStreamList  = "/v/api/v1/stream/list/{}";
const std::string apiMediaRange  = "/v/api/v1/media/range/{}";
const std::string apiItemWatched = "/v/api/v1/item/watched";
const std::string apiPlayRecord  = "/v/api/v1/play/record";
const std::string apiGetStream   = "/v/api/v1/stream";
const std::string apiSubtitleDl  = "/v/api/v1/subtitle/dl/{}";

/// @brief A media item entry returned by item/list and episode/list.
/// Field types and units match the reference TypeScript types in fntv-electron:
///   type       — string ("Movie", "Series", "Season", "Episode", etc.)
///   is_favorite — integer 1/0
///   ts         — play progress in **seconds**
///   duration   — total length in **seconds**
struct PlayListItem {
    std::string guid;
    std::string lan;
    std::string title;
    std::string tv_title;
    std::string parent_guid;
    std::string parent_title;
    std::string type;         ///< "Movie", "Series", "Season", "Episode", "Folder", …
    std::string poster;       ///< URL path relative to server base
    int         poster_width  = 0;
    int         poster_height = 0;
    float       vote_average  = 0.0f;
    int         runtime       = 0;    ///< runtime in minutes
    std::string overview;
    int         is_favorite   = 0;   ///< 1 = favorited, 0 = not
    int         season_number  = 0;
    int         episode_number = 0;
    double      duration      = 0.0;  ///< total length in **seconds**
    double      ts            = 0.0;  ///< play progress in **seconds**
    std::string imdb_id;
    std::string douban_id;
    std::string trim_id;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PlayListItem, guid, lan, title, tv_title,
    parent_guid, parent_title, type, poster, poster_width, poster_height,
    vote_average, runtime, overview, is_favorite, season_number, episode_number,
    duration, ts, imdb_id, douban_id, trim_id);

/// @brief Wrapper for the item/list response.
/// The API returns {"code":0,"data":{"list":[...],"total":N,...}} so the
/// list is nested inside `data`, not `data` itself.
struct ItemListData {
    int                      total = 0;
    std::vector<PlayListItem> list;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ItemListData, total, list);

/// @brief Minimal item info embedded inside PlayInfo
struct PlayItem {
    std::string guid;
    std::string title;
    std::string tv_title;
    std::string parent_title;
    int         season_number  = 0;
    int         episode_number = 0;
    double      duration      = 0.0;  ///< total length in **seconds**
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
/// Note: `guid` is the ITEM guid; `media_guid` is the MEDIA FILE guid needed for streaming.
/// `ts` is the play progress in **seconds**.
struct PlayInfo {
    std::string guid;        ///< item guid
    std::string media_guid;  ///< media file guid — use THIS for /media/range/{media_guid}
    PlayItem    item;
    double      ts = 0.0;    ///< play progress in **seconds**
    PlayConfig  play_config;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PlayInfo, guid, media_guid, item, ts, play_config);

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

    // duration and ts are in seconds; 1 second = 10,000,000 ticks (100 ns each)
    ep.RunTimeTicks     = static_cast<uint64_t>(item.duration * 10000000.0);
    ep.UserData.PlaybackPositionTicks = static_cast<int64_t>(item.ts * 10000000.0);
    ep.UserData.IsFavorite = (item.is_favorite != 0);
    ep.UserData.Played  = (item.ts > 0 && item.duration > 0 &&
                           item.ts >= item.duration * WATCHED_THRESHOLD_PERCENT / 100.0);
    if (ep.RunTimeTicks > 0 && ep.UserData.PlaybackPositionTicks > 0) {
        ep.UserData.PlayedPercentage =
            100.0 * ep.UserData.PlaybackPositionTicks / static_cast<double>(ep.RunTimeTicks);
    }

    // fnOS type strings match Jellyfin type strings — pass through directly
    ep.Type = item.type;

    // Store the fnOS poster URL path in ImageTags.
    // Paths start with "/" so callers can distinguish from Jellyfin image tag hashes.
    if (!item.poster.empty()) {
        ep.ImageTags[jellyfin::imageTypePrimary] = item.poster;
        if (item.type == jellyfin::mediaTypeEpisode)
            ep.ImageTags[jellyfin::imageTypeThumb] = item.poster;
    }

    return ep;
}

}  // namespace fnos
