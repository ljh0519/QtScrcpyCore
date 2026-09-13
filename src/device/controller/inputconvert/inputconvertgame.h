#ifndef INPUTCONVERTGAME_H
#define INPUTCONVERTGAME_H

#include <QPointF>
#include <QQueue>
#include <QString>
#include <QElapsedTimer>
#include <QPoint>
#include <array>

#include "inputconvertnormal.h"
#include "keymap.h"

#define MULTI_TOUCH_MAX_NUM 10
class InputConvertGame : public InputConvertNormal
{
    Q_OBJECT
public:
    InputConvertGame(Controller *controller);
    virtual ~InputConvertGame();

    virtual void mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize);
    virtual void wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize);
    virtual void keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize);
    virtual bool isCurrentCustomKeymap();
    virtual void setVideoWindowFocused(bool focused);

    void loadKeyMap(const QString &json);

protected:
    void updateSize(const QSize &frameSize, const QSize &showSize);
    void sendTouchDownEvent(int id, QPointF pos);
    void sendTouchMoveEvent(int id, QPointF pos);
    void sendTouchUpEvent(int id, QPointF pos);
    void sendTouchEvent(int id, QPointF pos, AndroidMotioneventAction action);
    void sendKeyEvent(AndroidKeyeventAction action, AndroidKeycode keyCode);
    QPointF calcFrameAbsolutePos(QPointF relativePos);
    QPointF calcScreenAbsolutePos(QPointF relativePos);

    // multi touch id
    int attachTouchID(int key);
    void detachTouchID(int key);
    int getTouchID(int key);
    QString touchIDState() const;
    QString touchActionName(AndroidMotioneventAction action) const;
    QString logTime() const;
    void recordTrace(int stage, quint64 sequence = 0, quint64 gestureSequence = 0,
                     int key = 0, int id = -1, int action = -1,
                     const QPointF &pos = QPointF(), const QPoint &absolutePos = QPoint(),
                     int pressedNum = -1, int queuePos = -1, int queueTimer = -1);
    void dumpTrace(const QString &reason);
    QString traceStageName(int stage) const;
    void logSteerWheelSummary(const QString &reason, int id);

    // steer wheel
    void processSteerWheel(const KeyMap::KeyMapNode &node, const QKeyEvent *from);

    // click
    void processKeyClick(const QPointF &clickPos, bool clickTwice, bool switchMap, const QKeyEvent *from);

    // click mutil
    void processKeyClickMulti(const KeyMap::DelayClickNode *nodes, const int count, const QKeyEvent *from);

    // drag
    void processKeyDrag(const QPointF &startPos, QPointF endPos, quint32 startDelay, float dragSpeed, const QKeyEvent *from);

    // android key
    void processAndroidKey(AndroidKeycode androidKey, const QKeyEvent *from);

    // mouse
    bool processMouseClick(const QMouseEvent *from);
    bool processMouseMove(const QMouseEvent *from);
    void moveCursorTo(const QMouseEvent *from, const QPoint &localPosPixel);
    void mouseMoveStartTouch(const QMouseEvent *from);
    void mouseMoveStopTouch();
    void startMouseMoveTimer();
    void stopMouseMoveTimer();

    bool switchGameMap();
    bool checkCursorPos(const QMouseEvent *from);
    void hideMouseCursor(bool hide);
    void resetStuckTouches();

    void getDelayQueue(const QPointF& start, const QPointF& end,
                       const double& distanceStep, const double& posStepconst,
                       quint32 lowestTimer, quint32 highestTimer,
                       QQueue<QPointF>& queuePos, QQueue<quint32>& queueTimer);

protected:
    void timerEvent(QTimerEvent *event);

private slots:
    void onSteerWheelTimer();
    void onDragTimer();

private:
    QSize m_frameSize;
    QSize m_showSize;
    bool m_gameMap = false;
    bool m_needBackMouseMove = false;
    bool m_cursorHiddenByKeymap = false;
    int m_multiTouchID[MULTI_TOUCH_MAX_NUM] = { 0 };
    KeyMap m_keyMap;
    QElapsedTimer m_logTimer;
    quint64 m_logSequence = 0;
    bool m_dumpTraceOnGestureEnd = false;

    enum TraceStage
    {
        ITS_INPUT_KEY = 0,
        ITS_WHEEL_STATE,
        ITS_QUEUE_GENERATED,
        ITS_TIMER,
        ITS_TOUCH_BEGIN,
        ITS_TOUCH_POST,
        ITS_TOUCH_DROP,
        ITS_TOUCH_ID_ATTACH,
        ITS_TOUCH_ID_DETACH
    };

    struct TraceEntry
    {
        qint64 elapsedMs = 0;
        quint64 sequence = 0;
        quint64 gestureSequence = 0;
        int stage = ITS_INPUT_KEY;
        int key = 0;
        int id = -1;
        int action = -1;
        QPointF pos;
        QPoint absolutePos;
        int pressedNum = -1;
        int queuePos = -1;
        int queueTimer = -1;
    };

    static constexpr int TRACE_CAPACITY = 512;
    std::array<TraceEntry, TRACE_CAPACITY> m_trace {};
    int m_traceNext = 0;
    int m_traceSize = 0;

    bool m_processMouseMove = true;

    // steer wheel
    struct
    {
        // the first key pressed
        int touchKey = Qt::Key_unknown;
        bool pressedUp = false;
        bool pressedDown = false;
        bool pressedLeft = false;
        bool pressedRight = false;
        bool pressedSprint = false;

        // for delay
        struct {
            QPointF currentPos;
            QTimer* timer = nullptr;
            QQueue<QPointF> queuePos;
            QQueue<quint32> queueTimer;
            int pressedNum = 0;
            quint64 traceSequence = 0;
            qint64 downTimeMs = -1;
            qint64 firstMoveTimeMs = -1;
            qint64 upTimeMs = -1;
            int moveCount = 0;
        } delayData;
    } m_ctrlSteerWheel;

    // mouse move
    struct
    {
        QPointF lastConverPos;
        QPointF lastPos = { 0.0, 0.0 };
        bool touching = false;
        int timer = 0;
        bool smallEyes = false;
        int ignoreCount = 0;
    } m_ctrlMouseMove;

    // for drag delay
    struct {
        QPointF currentPos;
        QTimer* timer = nullptr;
        QQueue<QPointF> queuePos;
        QQueue<quint32> queueTimer;
        int pressKey = 0;
    } m_dragDelayData;
};

#endif // INPUTCONVERTGAME_H
