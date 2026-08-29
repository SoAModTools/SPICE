#include "TextureViewport.h"

#include <QtCore/QEvent>
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

double fitScale(const QSize imageSize, const QSize availableSize) {
    if (imageSize.isEmpty() || availableSize.isEmpty()) return 1.0;
    const double fractional = std::min(
        static_cast<double>(availableSize.width()) / imageSize.width(),
        static_cast<double>(availableSize.height()) / imageSize.height());
    return fractional >= 1.0 ? std::max(1.0, std::floor(fractional)) : fractional;
}

double explicitScale(const TextureViewport::ZoomMode mode) {
    switch (mode) {
    case TextureViewport::ZoomMode::One: return 1.0;
    case TextureViewport::ZoomMode::Two: return 2.0;
    case TextureViewport::ZoomMode::Four: return 4.0;
    case TextureViewport::ZoomMode::Eight: return 8.0;
    case TextureViewport::ZoomMode::Sixteen: return 16.0;
    case TextureViewport::ZoomMode::ThirtyTwo: return 32.0;
    case TextureViewport::ZoomMode::IntegerFit: break;
    }
    return 1.0;
}

void paintBackground(QPainter& painter, const QRect& bounds,
    const TextureViewport::BackgroundMode mode) {
    if (mode == TextureViewport::BackgroundMode::Dark) {
        painter.fillRect(bounds, QColor(43, 43, 43));
        return;
    }
    if (mode == TextureViewport::BackgroundMode::Light) {
        painter.fillRect(bounds, QColor(222, 222, 222));
        return;
    }

    constexpr int square = 12;
    painter.fillRect(bounds, QColor(92, 92, 92));
    for (int y = 0; y < bounds.height(); y += square) {
        for (int x = 0; x < bounds.width(); x += square) {
            if (((x / square) + (y / square)) % 2 == 0) {
                painter.fillRect(x, y, square, square, QColor(132, 132, 132));
            }
        }
    }
}

QImage renderScaled(const QImage& source, const QSize targetSize,
    const TextureViewport::SamplingMode sampling) {
    QImage result(targetSize, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::SmoothPixmapTransform,
        sampling == TextureViewport::SamplingMode::Linear);
    painter.drawImage(QRect(QPoint(0, 0), targetSize), source);
    return result;
}

} // namespace

class TextureCanvas final : public QWidget {
public:
    explicit TextureCanvas(QWidget* parent = nullptr)
        : QWidget(parent) {
        setMinimumSize(1, 1);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    void setImage(QImage image, QString emptyMessage) {
        image_ = std::move(image);
        emptyMessage_ = std::move(emptyMessage);
        update();
    }

    [[nodiscard]] QSize imageSize() const noexcept { return image_.size(); }
    [[nodiscard]] bool hasImage() const noexcept { return !image_.isNull(); }
    [[nodiscard]] double scale() const noexcept { return scale_; }

    void setScale(const double scale) {
        scale_ = scale;
        update();
    }

    void setSampling(const TextureViewport::SamplingMode sampling) {
        sampling_ = sampling;
        update();
    }

    void setBackground(const TextureViewport::BackgroundMode background) {
        background_ = background;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        paintBackground(painter, rect(), background_);
        if (!hasImage()) {
            painter.setPen(background_ == TextureViewport::BackgroundMode::Light
                ? QColor(55, 55, 55) : QColor(220, 220, 220));
            painter.drawText(rect().adjusted(12, 12, -12, -12), Qt::AlignCenter | Qt::TextWordWrap,
                emptyMessage_);
            return;
        }

        const QSize targetSize(
            std::max(1, static_cast<int>(std::round(image_.width() * scale_))),
            std::max(1, static_cast<int>(std::round(image_.height() * scale_))));
        const QRect target(
            (width() - targetSize.width()) / 2,
            (height() - targetSize.height()) / 2,
            targetSize.width(), targetSize.height());
        painter.setRenderHint(QPainter::SmoothPixmapTransform,
            sampling_ == TextureViewport::SamplingMode::Linear);
        painter.drawImage(target, image_);
    }

private:
    QImage image_{};
    QString emptyMessage_ = "No decoded preview available";
    double scale_ = 1.0;
    TextureViewport::SamplingMode sampling_ = TextureViewport::SamplingMode::Nearest;
    TextureViewport::BackgroundMode background_ = TextureViewport::BackgroundMode::Checkerboard;
};

TextureViewport::TextureViewport(QWidget* parent)
    : QWidget(parent) {
    setObjectName("textureViewport");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);

    auto* toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->addWidget(new QLabel("Sampling", this));
    sampling_ = new QComboBox(this);
    sampling_->setObjectName("textureSamplingMode");
    sampling_->addItem("Nearest", static_cast<int>(SamplingMode::Nearest));
    sampling_->addItem("Linear (approx.)", static_cast<int>(SamplingMode::Linear));
    sampling_->setToolTip("Linear is an approximate filtered preview, not a game-accurate material rendering mode.");
    toolbar->addWidget(sampling_);

    toolbar->addWidget(new QLabel("Zoom", this));
    zoom_ = new QComboBox(this);
    zoom_->setObjectName("textureZoomMode");
    zoom_->addItem("Integer Fit", static_cast<int>(ZoomMode::IntegerFit));
    zoom_->addItem("1x", static_cast<int>(ZoomMode::One));
    zoom_->addItem("2x", static_cast<int>(ZoomMode::Two));
    zoom_->addItem("4x", static_cast<int>(ZoomMode::Four));
    zoom_->addItem("8x", static_cast<int>(ZoomMode::Eight));
    zoom_->addItem("16x", static_cast<int>(ZoomMode::Sixteen));
    zoom_->addItem("32x", static_cast<int>(ZoomMode::ThirtyTwo));
    toolbar->addWidget(zoom_);

    toolbar->addWidget(new QLabel("Background", this));
    background_ = new QComboBox(this);
    background_->setObjectName("textureBackgroundMode");
    background_->addItem("Checkerboard", static_cast<int>(BackgroundMode::Checkerboard));
    background_->addItem("Dark", static_cast<int>(BackgroundMode::Dark));
    background_->addItem("Light", static_cast<int>(BackgroundMode::Light));
    toolbar->addWidget(background_);
    toolbar->addStretch();
    root->addLayout(toolbar);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setObjectName("textureScrollArea");
    scrollArea_->setWidgetResizable(false);
    scrollArea_->setFrameShape(QFrame::StyledPanel);
    canvas_ = new TextureCanvas(scrollArea_);
    scrollArea_->setWidget(canvas_);
    scrollArea_->viewport()->installEventFilter(this);
    root->addWidget(scrollArea_, 1);

    connect(sampling_, &QComboBox::currentIndexChanged, this, [this](int) {
        canvas_->setSampling(samplingMode());
    });
    connect(zoom_, &QComboBox::currentIndexChanged, this, [this](int) {
        updateCanvasGeometry();
    });
    connect(background_, &QComboBox::currentIndexChanged, this, [this](int) {
        canvas_->setBackground(backgroundMode());
    });
    updateCanvasGeometry();
}

void TextureViewport::setImage(const std::optional<spice::mix::RgbaImageSnapshot>& image,
    const QString& emptyMessage) {
    QImage copied{};
    if (image.has_value() && !image->empty()) {
        QImage wrapped(image->rgba8.data(), static_cast<int>(image->width),
            static_cast<int>(image->height), QImage::Format_RGBA8888);
        copied = wrapped.copy();
    }
    canvas_->setImage(std::move(copied), emptyMessage);
    updateCanvasGeometry();
}

TextureViewport::SamplingMode TextureViewport::samplingMode() const noexcept {
    return static_cast<SamplingMode>(sampling_->currentData().toInt());
}

TextureViewport::ZoomMode TextureViewport::zoomMode() const noexcept {
    return static_cast<ZoomMode>(zoom_->currentData().toInt());
}

TextureViewport::BackgroundMode TextureViewport::backgroundMode() const noexcept {
    return static_cast<BackgroundMode>(background_->currentData().toInt());
}

double TextureViewport::effectiveScale() const noexcept { return canvas_->scale(); }

void TextureViewport::setSamplingMode(const SamplingMode mode) {
    sampling_->setCurrentIndex(sampling_->findData(static_cast<int>(mode)));
}

void TextureViewport::setZoomMode(const ZoomMode mode) {
    zoom_->setCurrentIndex(zoom_->findData(static_cast<int>(mode)));
}

void TextureViewport::setBackgroundMode(const BackgroundMode mode) {
    background_->setCurrentIndex(background_->findData(static_cast<int>(mode)));
}

bool TextureViewport::verifyViewControlsDoNotInvoke(const std::function<bool()>& stateProbe) {
    const bool initialState = stateProbe();
    const auto initialSampling = samplingMode();
    const auto initialZoom = zoomMode();
    const auto initialBackground = backgroundMode();
    setSamplingMode(SamplingMode::Linear);
    setZoomMode(ZoomMode::One);
    setBackgroundMode(BackgroundMode::Dark);
    const bool unchanged = stateProbe() == initialState;
    setSamplingMode(initialSampling);
    setZoomMode(initialZoom);
    setBackgroundMode(initialBackground);
    return unchanged && stateProbe() == initialState;
}

bool TextureViewport::runRenderingSmokeChecks() {
    if (fitScale(QSize(16, 16), QSize(500, 500)) != 31.0) return false;
    const double downscale = fitScale(QSize(1024, 512), QSize(500, 500));
    if (!(downscale > 0.0 && downscale < 1.0)) return false;

    QImage source(2, 1, QImage::Format_ARGB32);
    source.setPixelColor(0, 0, Qt::black);
    source.setPixelColor(1, 0, Qt::white);
    const auto nearest = renderScaled(source, QSize(16, 8), SamplingMode::Nearest);
    const auto linear = renderScaled(source, QSize(16, 8), SamplingMode::Linear);

    bool nearestHasIntermediate = false;
    bool linearHasIntermediate = false;
    for (int y = 0; y < nearest.height(); ++y) {
        for (int x = 0; x < nearest.width(); ++x) {
            const int nearestRed = nearest.pixelColor(x, y).red();
            const int linearRed = linear.pixelColor(x, y).red();
            nearestHasIntermediate = nearestHasIntermediate || (nearestRed > 0 && nearestRed < 255);
            linearHasIntermediate = linearHasIntermediate || (linearRed > 0 && linearRed < 255);
        }
    }
    return !nearestHasIntermediate && linearHasIntermediate;
}

bool TextureViewport::eventFilter(QObject* watched, QEvent* event) {
    if (watched == scrollArea_->viewport() && event->type() == QEvent::Resize) updateCanvasGeometry();
    return QWidget::eventFilter(watched, event);
}

void TextureViewport::updateCanvasGeometry() {
    const QSize available = scrollArea_->viewport()->size().expandedTo(QSize(1, 1));
    const double scale = zoomMode() == ZoomMode::IntegerFit
        ? fitScale(canvas_->imageSize(), available)
        : explicitScale(zoomMode());
    canvas_->setScale(scale);
    QSize target{};
    if (canvas_->hasImage()) {
        target = QSize(
            std::max(1, static_cast<int>(std::round(canvas_->imageSize().width() * scale))),
            std::max(1, static_cast<int>(std::round(canvas_->imageSize().height() * scale))));
    }
    canvas_->resize(target.expandedTo(available));
}
