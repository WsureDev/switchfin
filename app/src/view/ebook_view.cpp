
#include "view/ebook_view.hpp"

#ifdef USE_MUPDF

#include "api/http.hpp"
#include <mupdf/fitz.h>

class fz_error : public std::exception {
public:
    explicit fz_error(fz_context *ctx) : ctx(ctx) {}
    const char *what() const noexcept override { return ctx->error.message; }

private:
    fz_context *ctx;
};

using namespace brls::literals;

EBookView::EBookView() {
    this->container = new brls::Image();
    this->container->setFreeTexture(false);
    this->container->setFocusable(true);
    this->container->setHideHighlight(true);
    this->container->setHideHighlightBackground(true);
    this->addView(this->container);
    this->setJustifyContent(brls::JustifyContent::SPACE_AROUND);
    this->setPaddingLeft(brls::getStyle().getMetric("main/content_padding_sides"));
    this->setPaddingRight(brls::getStyle().getMetric("main/content_padding_sides"));
    this->setPaddingTop(10.f);
    this->setPaddingBottom(10.f);

    this->ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    fz_register_document_handlers(ctx);

    this->registerAction("main/player/prev"_i18n, brls::BUTTON_LB, [this](brls::View *view) {
        if (this->page <= 0) return false;
        this->render(--this->page);
        return true;
    });

    this->registerAction("main/player/next"_i18n, brls::BUTTON_RB, [this](brls::View *view) {
        if (this->page >= this->count - 1) return false;
        this->render(++this->page);
        return true;
    });

    this->addGestureRecognizer(
        new brls::TapGestureRecognizer([this](brls::TapGestureStatus status, brls::Sound *soundToPlay) {
            if (status.state == brls::GestureState::END) {
                auto frame = this->getFrame();
                if (status.position.x < frame.getMidX()) {
                    if (this->page > 0) this->render(--this->page);
                } else {
                    if (this->page < this->count - 1) this->render(++this->page);
                }
            }
        }));
}

EBookView::~EBookView() {
    if (this->doc) fz_drop_document(this->ctx, this->doc);
    if (this->ctx) fz_drop_context(this->ctx);
}

void EBookView::open(const std::string &url, float percent) {
    this->page = std::floor(percent);

    ASYNC_RETAIN
    brls::async([ASYNC_TOKEN, url]() {
        try {
            std::string content = HTTP::get(url, HTTP::Timeout{});
            fz_stream *stream = fz_open_memory(ctx, (const uint8_t *)content.data(), content.size());
            fz_try(ctx) this->doc = fz_open_document_with_stream(ctx, url.c_str(), stream);
            fz_always(ctx) fz_drop_stream(ctx, stream);
            fz_catch(ctx) throw fz_error(ctx);

            fz_try(ctx) this->count = fz_count_pages(ctx, this->doc);
            fz_catch(ctx) throw fz_error(ctx);

            brls::sync([ASYNC_TOKEN]() {
                ASYNC_RELEASE
                this->container->setBackgroundColor(nvgRGB(245, 246, 247));
                this->render(this->page);
            });
        } catch (const std::exception &ex) {
            std::string msg = ex.what();
            brls::sync([ASYNC_TOKEN, msg]() {
                ASYNC_RELEASE
                this->dismiss([msg]() { brls::Application::notify(msg); });
            });
        }
    });
}

void EBookView::render(int n) {
    fz_pixmap *pix = nullptr;
    fz_matrix ctm = fz_scale(1.5f, 1.5f);
    fz_try(ctx) pix = fz_new_pixmap_from_page_number(ctx, doc, n, ctm, fz_device_rgb(ctx), 1);
    fz_catch(ctx) return;
    auto vg = brls::Application::getNVGContext();
    int tex = nvgCreateImageRGBA(vg, pix->w, pix->h, 0, pix->samples);
    fz_drop_pixmap(ctx, pix);
    this->container->innerSetImage(tex);
}

#endif