#pragma once
#include <QPoint>
#include <QRectF>
#include <QSizeF>
#include <algorithm>
#include <cmath>

namespace DockPlacement {
inline constexpr int panelX = 32, panelY = 52, panelWidth = 456, panelHeight = 64;
inline constexpr int windowWidth = 520, windowHeight = 168;
inline constexpr int labelHeight = 27, labelGap = 16, edgeMargin = 2;
inline QRectF panel(qreal scale = 1, qreal width = panelWidth) {
    return QRectF(panelX * scale, panelY * scale, width * scale, panelHeight * scale);
}
inline QRectF usable(const QRect& area) {
    return QRectF(area).adjusted(edgeMargin, edgeMargin, -edgeMargin, -edgeMargin);
}
inline qreal scaleForArea(const QRect& area, qreal width = panelWidth) {
    const auto bounds = usable(area);

    return std::max(qreal(.001), std::min({qreal(1), (bounds.width() - 1) / width,
                                           (bounds.height() - 1) /
                                               (panelHeight + 2 * (labelGap + labelHeight))}));
}
inline QPoint clampOrigin(QPoint wanted, const QRect& area, qreal scale = 1,
                          qreal width = panelWidth) {
    const QRectF bounds = usable(area), bar = panel(scale, width);
    const int left = int(std::ceil(bounds.left() - bar.left()));
    const int top = int(std::ceil(bounds.top() - bar.top()));
    const int right = std::max(left, int(std::floor(bounds.right() - bar.right())));
    const int bottom = std::max(top, int(std::floor(bounds.bottom() - bar.bottom())));
    return {std::clamp(wanted.x(), left, right), std::clamp(wanted.y(), top, bottom)};
}
inline QRectF labelBounds(QPoint windowOrigin, const QRect& area, qreal scale = 1,
                          qreal width = panelWidth) {
    QRectF visible = usable(area).translated(-windowOrigin);
    visible = QRectF(visible.x() / scale, visible.y() / scale, visible.width() / scale,
                     visible.height() / scale);
    return visible.intersected(QRectF(0, 0, width + 2 * panelX, windowHeight));
}
inline QRectF placeLabel(qreal requestedWidth, qreal centreX, QRectF bounds) {
    if (bounds.isEmpty())
        return {};
    const qreal w = std::min(requestedWidth, bounds.width());
    const qreal h = std::min(qreal(labelHeight), bounds.height());
    const qreal above = panelY - labelGap - h, below = panelY + panelHeight + labelGap;
    qreal y = above;

    if (above < bounds.top()) {
        const qreal aboveRoom = panelY - bounds.top();
        const qreal belowRoom = bounds.bottom() - (panelY + panelHeight);
        if (below + h <= bounds.bottom() || belowRoom >= aboveRoom)
            y = below;
    }
    y = std::clamp(y, bounds.top(), bounds.bottom() - h);
    const qreal x = std::clamp(centreX - w / 2, bounds.left(), bounds.right() - w);
    return {x, y, w, h};
}
inline QRectF scaled(QRectF rect, qreal factor) {
    return {rect.x() * factor, rect.y() * factor, rect.width() * factor, rect.height() * factor};
}
}
