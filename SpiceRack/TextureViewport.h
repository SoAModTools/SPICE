#pragma once

#include "../SpiceMix/Documents/DocumentTypes.h"

#include <QtWidgets/QWidget>

#include <functional>
#include <optional>

class QComboBox;
class QEvent;
class QScrollArea;
class TextureCanvas;

class TextureViewport final : public QWidget {
public:
    enum class SamplingMode {
        Nearest,
        Linear,
    };

    enum class ZoomMode {
        IntegerFit,
        One,
        Two,
        Four,
        Eight,
        Sixteen,
        ThirtyTwo,
    };

    enum class BackgroundMode {
        Checkerboard,
        Dark,
        Light,
    };

    explicit TextureViewport(QWidget* parent = nullptr);

    void setImage(const std::optional<spice::mix::RgbaImageSnapshot>& image,
        const QString& emptyMessage = "No decoded preview available");

    [[nodiscard]] SamplingMode samplingMode() const noexcept;
    [[nodiscard]] ZoomMode zoomMode() const noexcept;
    [[nodiscard]] BackgroundMode backgroundMode() const noexcept;
    [[nodiscard]] double effectiveScale() const noexcept;

    void setSamplingMode(SamplingMode mode);
    void setZoomMode(ZoomMode mode);
    void setBackgroundMode(BackgroundMode mode);

    [[nodiscard]] bool verifyViewControlsDoNotInvoke(const std::function<bool()>& stateProbe);
    [[nodiscard]] static bool runRenderingSmokeChecks();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateCanvasGeometry();

    QScrollArea* scrollArea_ = nullptr;
    TextureCanvas* canvas_ = nullptr;
    QComboBox* sampling_ = nullptr;
    QComboBox* zoom_ = nullptr;
    QComboBox* background_ = nullptr;
};
