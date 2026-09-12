#ifndef STEERWHEELDETECTOR_H
#define STEERWHEELDETECTOR_H

#include <QPointF>
#include <QString>

#ifdef QSC_ENABLE_OPENCV
#include <opencv2/core.hpp>
#endif

// Locate the opaque stick knob (with white rim) via small 38x38 templates.
// Responded = knob is near targetPos rather than still at centerPos.
class SteerWheelDetector
{
public:
    bool init();
    bool isReady() const;

    // Returns true when the detected knob is close enough to targetPos.
    bool isResponding(const uint8_t *rgb32, int width, int height,
                      const QPointF &centerPos, const QPointF &targetPos) const;

private:
#ifdef QSC_ENABLE_OPENCV
    struct MatchResult {
        bool found = false;
        double score = -1.0;
        QPointF centerPx; // pixel coords of knob center
    };

    cv::Mat loadTemplate(const QString &qrcPath) const;
    cv::Mat rgb32ToGray(const uint8_t *rgb32, int width, int height) const;
    MatchResult findKnob(const cv::Mat &gray, const QPointF &centerPos) const;

    cv::Mat m_brightKnob;
    cv::Mat m_darkKnob;
    bool m_ready = false;

    // Template is 38x38, radius ~19px at capture resolution.
    static constexpr int kKnobRadiusPx = 19;
#endif
};

#endif // STEERWHEELDETECTOR_H
