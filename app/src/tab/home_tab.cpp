/*
    Copyright 2024 WsureDev
    fnOS home tab — shows continue-watching and all items from fnOS item/list
*/

#include "tab/home_tab.hpp"
#include "view/recyling_video.hpp"
#include "view/recycling_grid.hpp"
#include "view/video_card.hpp"
#include "view/video_source.hpp"
#include "api/fnos.hpp"
#include "utils/keybind.hpp"

using namespace brls::literals;  // for _i18n

HomeTab::HomeTab() {
    brls::Logger::debug("Tab HomeTab: create");
    this->inflateFromXMLRes("xml/tabs/home.xml");

    // The XML-bound RecylingVideo widgets use the Jellyfin API path; hide them.
    this->userResume->setVisibility(brls::Visibility::GONE);
    this->showNextup->setVisibility(brls::Visibility::GONE);

    // Create a single RecyclingGrid for all fnOS items and attach to boxHome.
    this->fnOSGrid = new RecyclingGrid();
    this->fnOSGrid->setGrow(1.0f);
    this->fnOSGrid->estimatedRowHeight = 300;
    this->fnOSGrid->estimatedRowWidth  = 175;
    this->fnOSGrid->spanCount          = brls::getStyle().getMetric("app/grid/5");
    this->fnOSGrid->registerCell("Cell", VideoCardCell::create);
    this->boxHome->addView(this->fnOSGrid);
}

HomeTab::~HomeTab() { brls::Logger::debug("View HomeTab: delete"); }

brls::View* HomeTab::create() { return new HomeTab(); }

void HomeTab::doRequest() { this->onCreate(); }

void HomeTab::onCreate() {
    auto actionRefresh = [this]([[maybe_unused]] brls::View* view) {
        this->fnOSGrid->showSkeleton();
        this->doRequest();
        return true;
    };
    this->registerAction("hints/refresh"_i18n, brls::BUTTON_BACK, actionRefresh);
    this->registerAction(KeyBind::getRefresh(), actionRefresh);

    this->fnOSGrid->showSkeleton();

    ASYNC_RETAIN
    fnos::postJSON<std::vector<fnos::PlayListItem>>(
        {{"parent_guid", ""}, {"sort_column", "ts"}, {"sort_type", "desc"}},
        [ASYNC_TOKEN](const std::vector<fnos::PlayListItem>& items) {
            ASYNC_RELEASE
            if (items.empty()) {
                this->fnOSGrid->setEmpty();
                return;
            }
            std::vector<jellyfin::Episode> eps;
            eps.reserve(items.size());
            for (auto& it : items)
                eps.push_back(fnos::toJellyfinEpisode(it));

            this->fnOSGrid->setDataSource(new VideoDataSource(eps));
        },
        [ASYNC_TOKEN](const std::string& ex) {
            ASYNC_RELEASE
            this->fnOSGrid->setError(ex);
            auto dialog = new brls::Dialog(ex);
            dialog->addButton("hints/retry"_i18n, [this]() {
                brls::sync([this]() { this->doRequest(); });
            });
            dialog->addButton("hints/cancel"_i18n, []() {});
            dialog->open();
        },
        fnos::apiItemList);
}
