#include "steerwheeldetector.h"

#include <QDebug>
#include <QFile>
#include <QIODevice>
#include <QtGlobal>
#include <cmath>

#ifdef QSC_ENABLE_OPENCV
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#endif

bool SteerWheelDetector::init()
{
#ifdef QSC_ENABLE_OPENCV
    m_brightKnob = loadTemplate(QStringLiteral(":/steerwheel/knob-bright-idle.jpeg"));
    m_darkKnob = loadTemplate(QStringLiteral(":/steerwheel/knob-dark-idle.jpeg"));
    m_ready = !m_brightKnob.empty() && !m_darkKnob.empty();
    if (!m_ready) {
        qWarning() << "SteerWheelDetector: failed to load knob templates";
    } else {
        qInfo() << "SteerWheelDetector: knob templates loaded"
                << m_brightKnob.cols << "x" << m_brightKnob.rows;
    }
    return m_ready;
#else
    return false;
#endif
}

bool SteerWheelDetector::isReady() const
{
#ifdef QSC_ENABLE_OPENCV
    return m_ready;
#else
    return false;
#endif
}

bool SteerWheelDetector::isResponding(const uint8_t *rgb32, int width, int height,
                                      const QPointF &centerPos, const QPointF &targetPos) const
{
#ifdef QSC_ENABLE_OPENCV
    if (!m_ready || !rgb32 || width <= 0 || height <= 0) {
        return true; // cannot decide — do not force retry
    }

    cv::Mat gray = rgb32ToGray(rgb32, width, height);
    const MatchResult match = findKnob(gray, centerPos);
    if (!match.found) {
        qInfo() << "SteerWheelDetector: knob not found (score too low), treat as no response";
        return false;
    }

    const QPointF centerPx(centerPos.x() * width, centerPos.y() * height);
    const QPointF targetPx(targetPos.x() * width, targetPos.y() * height);
    const double dxCenter = match.centerPx.x() - centerPx.x();
    const double dyCenter = match.centerPx.y() - centerPx.y();
    const double distCenter = std::sqrt(dxCenter * dxCenter + dyCenter * dyCenter);
    const double dxTarget = match.centerPx.x() - targetPx.x();
    const double dyTarget = match.centerPx.y() - targetPx.y();
    const double distTarget = std::sqrt(dxTarget * dxTarget + dyTarget * dyTarget);

    // Travel length expected for current keys (pixel).
    const double travel = std::sqrt(
            (targetPx.x() - centerPx.x()) * (targetPx.x() - centerPx.x())
            + (targetPx.y() - centerPx.y()) * (targetPx.y() - centerPx.y()));

    // Still near center if within ~0.55 radius; near target if within max(0.7*radius, 0.35*travel).
    const double nearCenterMax = kKnobRadiusPx * 0.55;
    const double nearTargetMax = qMax(kKnobRadiusPx * 0.70, travel * 0.35);

    const bool nearCenter = distCenter <= nearCenterMax;
    const bool nearTarget = distTarget <= nearTargetMax;
    // Prefer "closer to target than to center" when travel is meaningful.
    const bool closerToTarget = travel > 1.0 && distTarget + kKnobRadiusPx * 0.25 < distCenter;
    const bool responded = !nearCenter && (nearTarget || closerToTarget);

    qInfo() << "SteerWheelDetector: knob"
            << "score" << match.score
            << "distCenter" << distCenter
            << "distTarget" << distTarget
            << "responded" << responded;
    return responded;
#else
    Q_UNUSED(rgb32);
    Q_UNUSED(width);
    Q_UNUSED(height);
    Q_UNUSED(centerPos);
    Q_UNUSED(targetPos);
    return true;
#endif
}

#ifdef QSC_ENABLE_OPENCV
cv::Mat SteerWheelDetector::loadTemplate(const QString &qrcPath) const
{
    QFile file(qrcPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "SteerWheelDetector: cannot open" << qrcPath;
        return cv::Mat();
    }
    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty()) {
        return cv::Mat();
    }
    std::vector<uchar> buf(bytes.begin(), bytes.end());
    cv::Mat bgr = cv::imdecode(buf, cv::IMREAD_COLOR);
    if (bgr.empty()) {
        return cv::Mat();
    }
    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    return gray;
}

cv::Mat SteerWheelDetector::rgb32ToGray(const uint8_t *rgb32, int width, int height) const
{
    cv::Mat bgra(height, width, CV_8UC4, const_cast<uint8_t *>(rgb32));
    cv::Mat bgr;
    cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    return gray;
}

SteerWheelDetector::MatchResult SteerWheelDetector::findKnob(const cv::Mat &gray,
                                                             const QPointF &centerPos) const
{
    MatchResult best;
    if (gray.empty() || m_brightKnob.empty()) {
        return best;
    }

    const int tw = m_brightKnob.cols;
    const int th = m_brightKnob.rows;
    // Search window: cover stick travel (~0.16 of frame) plus template size.
    const int searchHalfW = qMax(tw, static_cast<int>(gray.cols * 0.18));
    const int searchHalfH = qMax(th, static_cast<int>(gray.rows * 0.30));
    const int cx = static_cast<int>(centerPos.x() * gray.cols);
    const int cy = static_cast<int>(centerPos.y() * gray.rows);
    const int x0 = qBound(0, cx - searchHalfW, gray.cols - 1);
    const int y0 = qBound(0, cy - searchHalfH, gray.rows - 1);
    const int x1 = qBound(x0 + tw, cx + searchHalfW, gray.cols);
    const int y1 = qBound(y0 + th, cy + searchHalfH, gray.rows);
    if (x1 - x0 < tw || y1 - y0 < th) {
        return best;
    }

    cv::Mat roi = gray(cv::Rect(x0, y0, x1 - x0, y1 - y0));

    auto matchOne = [&](const cv::Mat &templ) {
        if (templ.empty() || roi.rows < templ.rows || roi.cols < templ.cols) {
            return;
        }
        cv::Mat result;
        cv::matchTemplate(roi, templ, result, cv::TM_CCOEFF_NORMED);
        double minVal = 0.0;
        double maxVal = 0.0;
        cv::Point maxLoc;
        cv::minMaxLoc(result, &minVal, &maxVal, nullptr, &maxLoc);
        if (maxVal > best.score) {
            best.score = maxVal;
            best.found = maxVal >= 0.45;
            best.centerPx = QPointF(x0 + maxLoc.x + templ.cols * 0.5,
                                    y0 + maxLoc.y + templ.rows * 0.5);
        }
    };

    matchOne(m_brightKnob);
    matchOne(m_darkKnob);
    return best;
}
#endif
