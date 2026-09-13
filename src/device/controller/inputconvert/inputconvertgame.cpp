#include <QDebug>
#include <QCursor>
#include <QDateTime>
#include <QGuiApplication>
#include <QTimer>
#include <QTime>
#include <QRandomGenerator>

#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#endif

#include "../controller.h"
#include "inputconvertgame.h"

#define CURSOR_POS_CHECK 50

InputConvertGame::InputConvertGame(Controller *controller) : InputConvertNormal(controller) {
    m_logTimer.start();
    m_dumpTraceOnGestureEnd = qEnvironmentVariableIntValue("QTSCRCPY_TOUCH_TRACE_DUMP") > 0;
    m_ctrlSteerWheel.delayData.timer = new QTimer(this);
    m_ctrlSteerWheel.delayData.timer->setSingleShot(true);
    connect(m_ctrlSteerWheel.delayData.timer, &QTimer::timeout, this, &InputConvertGame::onSteerWheelTimer);
}

InputConvertGame::~InputConvertGame() {}

QString InputConvertGame::touchActionName(AndroidMotioneventAction action) const
{
    switch (action) {
    case AMOTION_EVENT_ACTION_DOWN:
        return QStringLiteral("DOWN");
    case AMOTION_EVENT_ACTION_UP:
        return QStringLiteral("UP");
    case AMOTION_EVENT_ACTION_MOVE:
        return QStringLiteral("MOVE");
    default:
        return QStringLiteral("ACTION_%1").arg(static_cast<int>(action));
    }
}

QString InputConvertGame::logTime() const
{
    return QStringLiteral("%1 (+%2ms)")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
        .arg(m_logTimer.elapsed());
}

QString InputConvertGame::traceStageName(int stage) const
{
    switch (stage) {
    case ITS_INPUT_KEY:
        return QStringLiteral("input-key");
    case ITS_WHEEL_STATE:
        return QStringLiteral("wheel-state");
    case ITS_QUEUE_GENERATED:
        return QStringLiteral("queue-generated");
    case ITS_TIMER:
        return QStringLiteral("timer");
    case ITS_TOUCH_BEGIN:
        return QStringLiteral("touch-begin");
    case ITS_TOUCH_POST:
        return QStringLiteral("touch-post");
    case ITS_TOUCH_DROP:
        return QStringLiteral("touch-drop");
    case ITS_TOUCH_ID_ATTACH:
        return QStringLiteral("touch-id-attach");
    case ITS_TOUCH_ID_DETACH:
        return QStringLiteral("touch-id-detach");
    default:
        return QStringLiteral("stage-%1").arg(stage);
    }
}

void InputConvertGame::recordTrace(int stage, quint64 sequence, quint64 gestureSequence,
                                   int key, int id, int action,
                                   const QPointF &pos, const QPoint &absolutePos,
                                   int pressedNum, int queuePos, int queueTimer)
{
    TraceEntry &entry = m_trace[m_traceNext];
    entry.elapsedMs = m_logTimer.elapsed();
    entry.sequence = sequence;
    entry.gestureSequence = gestureSequence;
    entry.stage = stage;
    entry.key = key;
    entry.id = id;
    entry.action = action;
    entry.pos = pos;
    entry.absolutePos = absolutePos;
    entry.pressedNum = pressedNum;
    entry.queuePos = queuePos;
    entry.queueTimer = queueTimer;
    m_traceNext = (m_traceNext + 1) % TRACE_CAPACITY;
    m_traceSize = qMin(m_traceSize + 1, TRACE_CAPACITY);
}

void InputConvertGame::dumpTrace(const QString &reason)
{
    const qint64 nowElapsed = m_logTimer.elapsed();
    const QDateTime now = QDateTime::currentDateTime();
    const int first = (m_traceNext - m_traceSize + TRACE_CAPACITY) % TRACE_CAPACITY;

    qWarning().noquote() << logTime()
                         << "[TouchTrace] begin"
                         << "reason:" << reason
                         << "entries:" << m_traceSize
                         << "touchIDs:" << touchIDState();

    for (int i = 0; i < m_traceSize; ++i) {
        const TraceEntry &entry = m_trace[(first + i) % TRACE_CAPACITY];
        const QDateTime eventTime = now.addMSecs(entry.elapsedMs - nowElapsed);
        qWarning().noquote() << eventTime.toString(Qt::ISODateWithMs)
                             << "[TouchTrace]"
                             << "elapsedMs:" << entry.elapsedMs
                             << "stage:" << traceStageName(entry.stage)
                             << "seq:" << entry.sequence
                             << "gesture:" << entry.gestureSequence
                             << "key:" << entry.key
                             << "action:" << (entry.action >= 0 ? touchActionName(static_cast<AndroidMotioneventAction>(entry.action)) : QStringLiteral("-"))
                             << "id:" << entry.id
                             << "pos:" << entry.pos
                             << "absolutePos:" << entry.absolutePos
                             << "pressedNum:" << entry.pressedNum
                             << "queuePos:" << entry.queuePos
                             << "queueTimer:" << entry.queueTimer;
    }

    if (m_controller) {
        m_controller->dumpControlTrace(m_ctrlSteerWheel.delayData.traceSequence, reason);
    }

    qWarning().noquote() << logTime() << "[TouchTrace] end";
}

void InputConvertGame::logSteerWheelSummary(const QString &reason, int id)
{
    const auto &delayData = m_ctrlSteerWheel.delayData;
    const qint64 duration = delayData.downTimeMs >= 0 && delayData.upTimeMs >= delayData.downTimeMs
        ? delayData.upTimeMs - delayData.downTimeMs : -1;
    const qint64 firstMoveDelay = delayData.downTimeMs >= 0 && delayData.firstMoveTimeMs >= delayData.downTimeMs
        ? delayData.firstMoveTimeMs - delayData.downTimeMs : -1;

    qInfo().noquote() << logTime()
                      << "[SteerWheelSummary]"
                      << "reason:" << reason
                      << "gesture:" << delayData.traceSequence
                      << "touchKey:" << m_ctrlSteerWheel.touchKey
                      << "id:" << id
                      << "durationMs:" << duration
                      << "firstMoveDelayMs:" << firstMoveDelay
                      << "moveCount:" << delayData.moveCount
                      << "downTimeMs:" << delayData.downTimeMs
                      << "firstMoveTimeMs:" << delayData.firstMoveTimeMs
                      << "upTimeMs:" << delayData.upTimeMs
                      << "currentPos:" << delayData.currentPos
                      << "queuePos:" << delayData.queuePos.size()
                      << "queueTimer:" << delayData.queueTimer.size()
                      << "touchIDs:" << touchIDState();

    if (m_dumpTraceOnGestureEnd || id < 0 || delayData.moveCount == 0 || firstMoveDelay < 0) {
        dumpTrace(reason);
    }
}

void InputConvertGame::mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize)
{
    // 处理开关按键
    if (m_keyMap.isSwitchOnKeyboard() == false && m_keyMap.getSwitchKey() == static_cast<int>(from->button())) {
        if (from->type() != QEvent::MouseButtonPress) {
            return;
        }
        if (!switchGameMap()) {
            m_needBackMouseMove = false;
        }
        return;
    }

    if (!m_needBackMouseMove && m_gameMap) {
        updateSize(frameSize, showSize);
        // mouse move
        if (m_keyMap.isValidMouseMoveMap()) {
            if (processMouseMove(from)) {
                return;
            }
        }
        // mouse click
        if (processMouseClick(from)) {
            return;
        }
    }
    InputConvertNormal::mouseEvent(from, frameSize, showSize);
}

void InputConvertGame::wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize)
{
    if (m_gameMap) {
        updateSize(frameSize, showSize);
    } else {
        InputConvertNormal::wheelEvent(from, frameSize, showSize);
    }
}

void InputConvertGame::keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize)
{
    if (!from) {
        qWarning().noquote() << logTime() << "[InputKeyEvent] received null event";
        return;
    }

    // 处理开关按键
    if (m_keyMap.isSwitchOnKeyboard() && m_keyMap.getSwitchKey() == from->key()) {
        if (QEvent::KeyPress != from->type()) {
            return;
        }
        if (!switchGameMap()) {
            m_needBackMouseMove = false;
        }
        return;
    }

    const KeyMap::KeyMapNode &node = m_keyMap.getKeyMapNodeKey(from->key());
    // 处理特殊按键：可以释放出鼠标的按键
    if (m_needBackMouseMove && KeyMap::KMT_CLICK == node.type && node.data.click.switchMap) {
        updateSize(frameSize, showSize);
        // Qt::Key_Tab Qt::Key_M for PUBG mobile
        processKeyClick(node.data.click.keyNode.pos, false, node.data.click.switchMap, from);
        return;
    }

    if (m_gameMap) {
        updateSize(frameSize, showSize);
        if (!from || from->isAutoRepeat()) {
            return;
        }

        // small eyes
        if (m_keyMap.isValidMouseMoveMap() && from->key() == m_keyMap.getMouseMoveMap().data.mouseMove.smallEyes.key) {
            m_ctrlMouseMove.smallEyes = (QEvent::KeyPress == from->type());

            if (QEvent::KeyPress == from->type()) {
                m_processMouseMove = false;
                int delay = 30;
                QTimer::singleShot(delay, this, [this]() { mouseMoveStopTouch(); });
                QTimer::singleShot(delay * 2, this, [this]() {
                    mouseMoveStartTouch(nullptr);
                    m_processMouseMove = true;
                });

                stopMouseMoveTimer();
            } else {
                mouseMoveStopTouch();
                mouseMoveStartTouch(nullptr);
            }
            return;
        }

        switch (node.type) {
        // 处理方向盘
        case KeyMap::KMT_STEER_WHEEL:
            processSteerWheel(node, from);
            return;
        // 处理普通按键
        case KeyMap::KMT_CLICK:
            processKeyClick(node.data.click.keyNode.pos, false, node.data.click.switchMap, from);
            processAndroidKey(node.data.click.keyNode.androidKey, from);
            return;
        case KeyMap::KMT_CLICK_TWICE:
            processKeyClick(node.data.clickTwice.keyNode.pos, true, false, from);
            processAndroidKey(node.data.clickTwice.keyNode.androidKey, from);
            return;
        case KeyMap::KMT_CLICK_MULTI:
            processKeyClickMulti(node.data.clickMulti.keyNode.delayClickNodes, node.data.clickMulti.keyNode.delayClickNodesCount, from);
            return;
        case KeyMap::KMT_DRAG:
            processKeyDrag(node.data.drag.keyNode.pos, node.data.drag.keyNode.extendPos,
                         node.data.drag.startDelay, node.data.drag.dragSpeed, from);
            return;
        case KeyMap::KMT_ANDROID_KEY:
            processAndroidKey(node.data.androidKey.keyNode.androidKey, from);
            break;
        default:
            break;
        }
    } else {
        InputConvertNormal::keyEvent(from, frameSize, showSize);
    }
}

bool InputConvertGame::isCurrentCustomKeymap()
{
    return m_gameMap;
}

void InputConvertGame::loadKeyMap(const QString &json)
{
    m_keyMap.loadKeyMap(json);
}

void InputConvertGame::updateSize(const QSize &frameSize, const QSize &showSize)
{
    if (showSize != m_showSize) {
        if (m_gameMap && m_keyMap.isValidMouseMoveMap()) {
#ifdef QT_NO_DEBUG
            // show size change, resize grab cursor
            emit grabCursor(true);
#endif
        }
    }
    m_frameSize = frameSize;
    m_showSize = showSize;
}

void InputConvertGame::sendTouchDownEvent(int id, QPointF pos)
{
    sendTouchEvent(id, pos, AMOTION_EVENT_ACTION_DOWN);
}

void InputConvertGame::sendTouchMoveEvent(int id, QPointF pos)
{
    sendTouchEvent(id, pos, AMOTION_EVENT_ACTION_MOVE);
}

void InputConvertGame::sendTouchUpEvent(int id, QPointF pos)
{
    sendTouchEvent(id, pos, AMOTION_EVENT_ACTION_UP);
}

void InputConvertGame::sendTouchEvent(int id, QPointF pos, AndroidMotioneventAction action)
{
    const quint64 sequence = ++m_logSequence;
    const quint64 gestureSequence = m_ctrlSteerWheel.delayData.traceSequence;
    recordTrace(ITS_TOUCH_BEGIN, sequence, gestureSequence, m_ctrlSteerWheel.touchKey,
                id, action, pos, QPoint(), m_ctrlSteerWheel.delayData.pressedNum,
                m_ctrlSteerWheel.delayData.queuePos.size(),
                m_ctrlSteerWheel.delayData.queueTimer.size());

    if (0 > id || MULTI_TOUCH_MAX_NUM - 1 < id) {
        recordTrace(ITS_TOUCH_DROP, sequence, gestureSequence, m_ctrlSteerWheel.touchKey,
                    id, action, pos, QPoint(), m_ctrlSteerWheel.delayData.pressedNum,
                    m_ctrlSteerWheel.delayData.queuePos.size(),
                    m_ctrlSteerWheel.delayData.queueTimer.size());
        dumpTrace(QStringLiteral("invalid-touch-id"));
        Q_ASSERT(0);
        return;
    }

    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_INJECT_TOUCH);
    if (!controlMsg) {
        qCritical().noquote() << logTime()
                              << "[TouchEvent] dropped: failed to allocate ControlMsg"
                              << "seq:" << sequence;
        return;
    }

    QPoint absolutePos = calcFrameAbsolutePos(pos).toPoint();
    static QPoint lastAbsolutePos = absolutePos;
    if (AMOTION_EVENT_ACTION_MOVE == action && lastAbsolutePos == absolutePos) {
        recordTrace(ITS_TOUCH_DROP, sequence, gestureSequence, m_ctrlSteerWheel.touchKey,
                    id, action, pos, absolutePos, m_ctrlSteerWheel.delayData.pressedNum,
                    m_ctrlSteerWheel.delayData.queuePos.size(),
                    m_ctrlSteerWheel.delayData.queueTimer.size());
        delete controlMsg;
        return;
    }
    lastAbsolutePos = absolutePos;

    if (action == AMOTION_EVENT_ACTION_MOVE && gestureSequence != 0
        && id == getTouchID(m_ctrlSteerWheel.touchKey)) {
        ++m_ctrlSteerWheel.delayData.moveCount;
        if (m_ctrlSteerWheel.delayData.firstMoveTimeMs < 0) {
            m_ctrlSteerWheel.delayData.firstMoveTimeMs = m_logTimer.elapsed();
        }
    }

    controlMsg->setInjectTouchMsgData(
        static_cast<quint64>(id),
        action,
        static_cast<AndroidMotioneventButtons>(0),
        static_cast<AndroidMotioneventButtons>(0),
        QRect(absolutePos, m_frameSize),
        AMOTION_EVENT_ACTION_DOWN == action ? 1.0f : 0.0f);
    controlMsg->setDebugTrace(sequence, gestureSequence, static_cast<int>(action), id);
    recordTrace(ITS_TOUCH_POST, sequence, gestureSequence, m_ctrlSteerWheel.touchKey,
                id, action, pos, absolutePos, m_ctrlSteerWheel.delayData.pressedNum,
                m_ctrlSteerWheel.delayData.queuePos.size(),
                m_ctrlSteerWheel.delayData.queueTimer.size());
    sendControlMsg(controlMsg);
}

void InputConvertGame::sendKeyEvent(AndroidKeyeventAction action, AndroidKeycode keyCode) {
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_INJECT_KEYCODE);
    if (!controlMsg) {
        return;
    }

    controlMsg->setInjectKeycodeMsgData(action, keyCode, 0, AMETA_NONE);
    sendControlMsg(controlMsg);
}

QPointF InputConvertGame::calcFrameAbsolutePos(QPointF relativePos)
{
    QPointF absolutePos;
    absolutePos.setX(m_frameSize.width() * relativePos.x());
    absolutePos.setY(m_frameSize.height() * relativePos.y());
    return absolutePos;
}

QPointF InputConvertGame::calcScreenAbsolutePos(QPointF relativePos)
{
    QPointF absolutePos;
    absolutePos.setX(m_showSize.width() * relativePos.x());
    absolutePos.setY(m_showSize.height() * relativePos.y());
    return absolutePos;
}

int InputConvertGame::attachTouchID(int key)
{
    const int existingId = getTouchID(key);
    recordTrace(ITS_TOUCH_ID_ATTACH, ++m_logSequence,
                m_ctrlSteerWheel.delayData.traceSequence, key, existingId);

    if (existingId != -1) {
        qWarning().noquote() << logTime()
                             << "[TouchID] duplicate attach request"
                             << "key:" << key
                             << "existingID:" << existingId
                             << "state:" << touchIDState();
        dumpTrace(QStringLiteral("duplicate-touch-id-attach"));
    }

    for (int i = 0; i < MULTI_TOUCH_MAX_NUM; i++) {
        if (0 == m_multiTouchID[i]) {
            m_multiTouchID[i] = key;
            recordTrace(ITS_TOUCH_ID_ATTACH, ++m_logSequence,
                        m_ctrlSteerWheel.delayData.traceSequence, key, i);
            return i;
        }
    }

    qWarning().noquote() << logTime()
                         << "[TouchID] attach failed: no free slot"
                         << "key:" << key
                         << "state:" << touchIDState();
    dumpTrace(QStringLiteral("no-free-touch-id"));
    return -1;
}

void InputConvertGame::detachTouchID(int key)
{
    recordTrace(ITS_TOUCH_ID_DETACH, ++m_logSequence,
                m_ctrlSteerWheel.delayData.traceSequence, key, getTouchID(key));

    for (int i = 0; i < MULTI_TOUCH_MAX_NUM; i++) {
        if (key == m_multiTouchID[i]) {
            m_multiTouchID[i] = 0;
            return;
        }
    }

    qWarning().noquote() << logTime()
                         << "[TouchID] detach requested for unknown key"
                         << "key:" << key
                         << "state:" << touchIDState();
    dumpTrace(QStringLiteral("unknown-touch-id-detach"));
}

int InputConvertGame::getTouchID(int key)
{
    for (int i = 0; i < MULTI_TOUCH_MAX_NUM; i++) {
        if (key == m_multiTouchID[i]) {
            return i;
        }
    }
    return -1;
}

QString InputConvertGame::touchIDState() const
{
    QString state;
    for (int i = 0; i < MULTI_TOUCH_MAX_NUM; i++) {
        if (!state.isEmpty()) {
            state += QLatin1Char(',');
        }
        state += QStringLiteral("%1:%2").arg(i).arg(m_multiTouchID[i]);
    }
    return state;
}

// -------- steer wheel event --------

void InputConvertGame::getDelayQueue(const QPointF& start, const QPointF& end,
                                     const double& distanceStep, const double& posStepconst,
                                     quint32 lowestTimer, quint32 highestTimer,
                                     QQueue<QPointF>& queuePos, QQueue<quint32>& queueTimer) {
    double x1 = start.x();
    double y1 = start.y();
    double x2 = end.x();
    double y2 = end.y();

    double dx=x2-x1;
    double dy=y2-y1;
    double e=(fabs(dx)>fabs(dy))?fabs(dx):fabs(dy);
    e /= distanceStep;
    dx/=e;
    dy/=e;

    QQueue<QPointF> queue;
    QQueue<quint32> queue2;
    for(int i=1;i<=e;i++) {
        QPointF pos(x1+(QRandomGenerator::global()->bounded(posStepconst*2)-posStepconst), y1+(QRandomGenerator::global()->bounded(posStepconst*2)-posStepconst));
        queue.enqueue(pos);
        queue2.enqueue(QRandomGenerator::global()->bounded(lowestTimer, highestTimer));
        x1+=dx;
        y1+=dy;
    }

    queuePos = queue;
    queueTimer = queue2;
    recordTrace(ITS_QUEUE_GENERATED, ++m_logSequence,
                m_ctrlSteerWheel.delayData.traceSequence,
                m_ctrlSteerWheel.touchKey,
                getTouchID(m_ctrlSteerWheel.touchKey),
                -1, end, QPoint(), m_ctrlSteerWheel.delayData.pressedNum,
                queuePos.size(), queueTimer.size());
}

void InputConvertGame::onSteerWheelTimer() {
    if(m_ctrlSteerWheel.delayData.queuePos.empty()) {
        recordTrace(ITS_TIMER, ++m_logSequence,
                    m_ctrlSteerWheel.delayData.traceSequence,
                    m_ctrlSteerWheel.touchKey,
                    getTouchID(m_ctrlSteerWheel.touchKey),
                    -1, m_ctrlSteerWheel.delayData.currentPos, QPoint(),
                    m_ctrlSteerWheel.delayData.pressedNum,
                    m_ctrlSteerWheel.delayData.queuePos.size(),
                    m_ctrlSteerWheel.delayData.queueTimer.size());
        return;
    }

    int id = getTouchID(m_ctrlSteerWheel.touchKey);
    recordTrace(ITS_TIMER, ++m_logSequence,
                m_ctrlSteerWheel.delayData.traceSequence,
                m_ctrlSteerWheel.touchKey, id, AMOTION_EVENT_ACTION_MOVE,
                m_ctrlSteerWheel.delayData.currentPos, QPoint(),
                m_ctrlSteerWheel.delayData.pressedNum,
                m_ctrlSteerWheel.delayData.queuePos.size(),
                m_ctrlSteerWheel.delayData.queueTimer.size());

    m_ctrlSteerWheel.delayData.currentPos = m_ctrlSteerWheel.delayData.queuePos.dequeue();
    sendTouchMoveEvent(id, m_ctrlSteerWheel.delayData.currentPos);

    if(m_ctrlSteerWheel.delayData.queuePos.empty() && m_ctrlSteerWheel.delayData.pressedNum == 0) {
        sendTouchUpEvent(id, m_ctrlSteerWheel.delayData.currentPos);
        detachTouchID(m_ctrlSteerWheel.touchKey);
        return;
    }

    if(!m_ctrlSteerWheel.delayData.queuePos.empty()) {
        const quint32 delay = m_ctrlSteerWheel.delayData.queueTimer.dequeue();
        recordTrace(ITS_TIMER, ++m_logSequence,
                    m_ctrlSteerWheel.delayData.traceSequence,
                    m_ctrlSteerWheel.touchKey, id, AMOTION_EVENT_ACTION_MOVE,
                    m_ctrlSteerWheel.delayData.currentPos, QPoint(),
                    m_ctrlSteerWheel.delayData.pressedNum,
                    m_ctrlSteerWheel.delayData.queuePos.size(),
                    static_cast<int>(delay));
        m_ctrlSteerWheel.delayData.timer->start(delay);
    }
}

void InputConvertGame::processSteerWheel(const KeyMap::KeyMapNode &node, const QKeyEvent *from)
{
    int key = from->key();
    bool flag = from->type() == QEvent::KeyPress;
    const quint64 sequence = ++m_logSequence;
    recordTrace(ITS_INPUT_KEY, sequence, m_ctrlSteerWheel.delayData.traceSequence,
                key, getTouchID(m_ctrlSteerWheel.touchKey));

    // identify keys
    if (key == node.data.steerWheel.up.key) {
        m_ctrlSteerWheel.pressedUp = flag;
    } else if (key == node.data.steerWheel.right.key) {
        m_ctrlSteerWheel.pressedRight = flag;
    } else if (key == node.data.steerWheel.down.key) {
        m_ctrlSteerWheel.pressedDown = flag;
    } else if (node.data.steerWheel.sprint.type != KeyMap::AT_INVALID
               && key == node.data.steerWheel.sprint.key) {
        m_ctrlSteerWheel.pressedSprint = flag;
        qInfo().noquote() << logTime()
                          << "[SteerWheel] sprint key"
                          << "key:" << key
                          << "isPress:" << flag
                          << "pressedUp:" << m_ctrlSteerWheel.pressedUp
                          << "pressedSprint:" << m_ctrlSteerWheel.pressedSprint;
    } else { // left
        m_ctrlSteerWheel.pressedLeft = flag;
    }

    // calc offset and pressed number
    QPointF offset(0.0, 0.0);
    int pressedNum = 0;
    if (m_ctrlSteerWheel.pressedUp) {
        ++pressedNum;
        offset.ry() -= node.data.steerWheel.up.extendOffset;
    }
    if (m_ctrlSteerWheel.pressedSprint) {
        ++pressedNum;
        offset.ry() -= node.data.steerWheel.up.extendOffset;
    }
    if (m_ctrlSteerWheel.pressedRight) {
        ++pressedNum;
        offset.rx() += node.data.steerWheel.right.extendOffset;
    }
    if (m_ctrlSteerWheel.pressedDown) {
        ++pressedNum;
        offset.ry() += node.data.steerWheel.down.extendOffset;
    }
    if (m_ctrlSteerWheel.pressedLeft) {
        ++pressedNum;
        offset.rx() -= node.data.steerWheel.left.extendOffset;
    }
    m_ctrlSteerWheel.delayData.pressedNum = pressedNum;
    recordTrace(ITS_WHEEL_STATE, sequence, m_ctrlSteerWheel.delayData.traceSequence,
                key, getTouchID(m_ctrlSteerWheel.touchKey), -1,
                node.data.steerWheel.centerPos + offset, QPoint(), pressedNum,
                m_ctrlSteerWheel.delayData.queuePos.size(),
                m_ctrlSteerWheel.delayData.queueTimer.size());

    // last key release and timer no active, active timer to detouch
    if (pressedNum == 0) {
        if (m_ctrlSteerWheel.delayData.timer->isActive()) {
            m_ctrlSteerWheel.delayData.timer->stop();
            m_ctrlSteerWheel.delayData.queueTimer.clear();
            m_ctrlSteerWheel.delayData.queuePos.clear();
        }

        int id = getTouchID(m_ctrlSteerWheel.touchKey);
        const quint64 gestureSequence = m_ctrlSteerWheel.delayData.traceSequence;
        m_ctrlSteerWheel.delayData.upTimeMs = m_logTimer.elapsed();
        sendTouchUpEvent(id, m_ctrlSteerWheel.delayData.currentPos);
        detachTouchID(m_ctrlSteerWheel.touchKey);
        if (gestureSequence != 0) {
            logSteerWheelSummary(QStringLiteral("all-direction-keys-released"), id);
            if (m_ctrlSteerWheel.delayData.traceSequence == gestureSequence) {
                m_ctrlSteerWheel.delayData.traceSequence = 0;
                m_ctrlSteerWheel.delayData.downTimeMs = -1;
                m_ctrlSteerWheel.delayData.firstMoveTimeMs = -1;
                m_ctrlSteerWheel.delayData.upTimeMs = -1;
                m_ctrlSteerWheel.delayData.moveCount = 0;
            }
        }
        return;
    }

    // process steer wheel key event
    m_ctrlSteerWheel.delayData.timer->stop();
    m_ctrlSteerWheel.delayData.queueTimer.clear();
    m_ctrlSteerWheel.delayData.queuePos.clear();

    // first press, get key and touch down
    if (pressedNum == 1 && flag) {
        m_ctrlSteerWheel.touchKey = from->key();
        m_ctrlSteerWheel.delayData.traceSequence = sequence;
        m_ctrlSteerWheel.delayData.downTimeMs = m_logTimer.elapsed();
        m_ctrlSteerWheel.delayData.firstMoveTimeMs = -1;
        m_ctrlSteerWheel.delayData.upTimeMs = -1;
        m_ctrlSteerWheel.delayData.moveCount = 0;
        int id = attachTouchID(m_ctrlSteerWheel.touchKey);
        sendTouchDownEvent(id, node.data.steerWheel.centerPos);

        getDelayQueue(node.data.steerWheel.centerPos, node.data.steerWheel.centerPos+offset,
                      0.01f, 0.002f, 2, 8,
                      m_ctrlSteerWheel.delayData.queuePos,
                      m_ctrlSteerWheel.delayData.queueTimer);
    } else {
        getDelayQueue(m_ctrlSteerWheel.delayData.currentPos, node.data.steerWheel.centerPos+offset,
                      0.01f, 0.002f, 2, 8,
                      m_ctrlSteerWheel.delayData.queuePos,
                      m_ctrlSteerWheel.delayData.queueTimer);
    }
    recordTrace(ITS_WHEEL_STATE, sequence, m_ctrlSteerWheel.delayData.traceSequence,
                key, getTouchID(m_ctrlSteerWheel.touchKey), -1,
                node.data.steerWheel.centerPos + offset, QPoint(),
                m_ctrlSteerWheel.delayData.pressedNum,
                m_ctrlSteerWheel.delayData.queuePos.size(),
                m_ctrlSteerWheel.delayData.queueTimer.size());
    m_ctrlSteerWheel.delayData.timer->start();
    return;
}

// -------- key event --------

void InputConvertGame::processKeyClick(const QPointF &clickPos, bool clickTwice, bool switchMap, const QKeyEvent *from)
{
    if (switchMap && QEvent::KeyRelease == from->type()) {
        m_needBackMouseMove = !m_needBackMouseMove;
        hideMouseCursor(!m_needBackMouseMove);
    }

    if (QEvent::KeyPress == from->type()) {
        int id = attachTouchID(from->key());
        sendTouchDownEvent(id, clickPos);
        if (clickTwice) {
            sendTouchUpEvent(getTouchID(from->key()), clickPos);
            detachTouchID(from->key());
        }
    } else if (QEvent::KeyRelease == from->type()) {
        if (clickTwice) {
            int id = attachTouchID(from->key());
            sendTouchDownEvent(id, clickPos);
        }
        sendTouchUpEvent(getTouchID(from->key()), clickPos);
        detachTouchID(from->key());
    }
}

void InputConvertGame::processKeyClickMulti(const KeyMap::DelayClickNode *nodes, const int count, const QKeyEvent *from)
{
    if (QEvent::KeyPress != from->type()) {
        return;
    }

    int key = from->key();
    int delay = 0;
    QPointF clickPos;

    for (int i = 0; i < count; i++) {
        delay += nodes[i].delay;
        clickPos = nodes[i].pos;
        QTimer::singleShot(delay, this, [this, key, clickPos]() {
            int id = attachTouchID(key);
            sendTouchDownEvent(id, clickPos);
        });

        // Don't up it too fast
        delay += 20;
        QTimer::singleShot(delay, this, [this, key, clickPos]() {
            int id = getTouchID(key);
            sendTouchUpEvent(id, clickPos);
            detachTouchID(key);
        });
    }
}

void InputConvertGame::onDragTimer() {
    if(m_dragDelayData.queuePos.empty()) {
        return;
    }
    int id = getTouchID(m_dragDelayData.pressKey);
    m_dragDelayData.currentPos = m_dragDelayData.queuePos.dequeue();
    sendTouchMoveEvent(id, m_dragDelayData.currentPos);

    if(m_dragDelayData.queuePos.empty()) {
        delete m_dragDelayData.timer;
        m_dragDelayData.timer = nullptr;

        sendTouchUpEvent(id, m_dragDelayData.currentPos);
        detachTouchID(m_dragDelayData.pressKey);

        m_dragDelayData.currentPos = QPointF();
        m_dragDelayData.pressKey = 0;
        return;
    }

    if(!m_dragDelayData.queuePos.empty()) {
        m_dragDelayData.timer->start(m_dragDelayData.queueTimer.dequeue());
    }
}

void InputConvertGame::processKeyDrag(const QPointF &startPos, QPointF endPos, quint32 startDelay, float dragSpeed, const QKeyEvent *from)
{
    if (QEvent::KeyPress == from->type()) {
        // stop last
        if (m_dragDelayData.timer && m_dragDelayData.timer->isActive()) {
            m_dragDelayData.timer->stop();
            delete m_dragDelayData.timer;
            m_dragDelayData.timer = nullptr;
            m_dragDelayData.queuePos.clear();
            m_dragDelayData.queueTimer.clear();

            sendTouchUpEvent(getTouchID(m_dragDelayData.pressKey), m_dragDelayData.currentPos);
            detachTouchID(m_dragDelayData.pressKey);

            m_dragDelayData.currentPos = QPointF();
            m_dragDelayData.pressKey = 0;
        }

        // start this
        int id = attachTouchID(from->key());
        sendTouchDownEvent(id, startPos);

        m_dragDelayData.timer = new QTimer(this);
        m_dragDelayData.timer->setSingleShot(true);
        connect(m_dragDelayData.timer, &QTimer::timeout, this, &InputConvertGame::onDragTimer);
        m_dragDelayData.pressKey = from->key();
        m_dragDelayData.currentPos = startPos;
        m_dragDelayData.queuePos.clear();
        m_dragDelayData.queueTimer.clear();

        // Clamp dragSpeed to 0-1 range
        const float speed = qBound(0.0f, static_cast<float>(dragSpeed), 1.0f);
        
        // Calculate delays based on dragSpeed
        // dragSpeed = 1 -> minDelay = 1, maxDelay = 2 (fastest)
        // dragSpeed = 0 -> minDelay = 30, maxDelay = 40 (slowest)
        const quint32 minDelay = static_cast<quint32>(1 + (1.0f - speed) * 29);  // 1 to 30
        const quint32 maxDelay = minDelay + static_cast<quint32>((1.0f - speed) * 9) + 1;  // // min + (0 to 9) + 1

        getDelayQueue(startPos, endPos,
                      0.01f, 0.0005f,
                      minDelay,
                      maxDelay,
                      m_dragDelayData.queuePos,
                      m_dragDelayData.queueTimer);

        m_dragDelayData.timer->start(startDelay);
    }
}

void InputConvertGame::processAndroidKey(AndroidKeycode androidKey, const QKeyEvent *from)
{
    if (AKEYCODE_UNKNOWN == androidKey) {
        return;
    }

    AndroidKeyeventAction action;
    switch (from->type()) {
    case QEvent::KeyPress:
        action = AKEY_EVENT_ACTION_DOWN;
        break;
    case QEvent::KeyRelease:
        action = AKEY_EVENT_ACTION_UP;
        break;
    default:
        return;
    }

    sendKeyEvent(action, androidKey);
}

// -------- mouse event --------

bool InputConvertGame::processMouseClick(const QMouseEvent *from)
{
    const KeyMap::KeyMapNode &node = m_keyMap.getKeyMapNodeMouse(from->button());
    if (KeyMap::KMT_INVALID == node.type) {
        return false;
    }

    if (QEvent::MouseButtonPress == from->type() || QEvent::MouseButtonDblClick == from->type()) {
        int id = attachTouchID(from->button());
        sendTouchDownEvent(id, node.data.click.keyNode.pos);
        return true;
    }
    if (QEvent::MouseButtonRelease == from->type()) {
        int id = getTouchID(from->button());
        sendTouchUpEvent(id, node.data.click.keyNode.pos);
        detachTouchID(from->button());
        return true;
    }
    return false;
}

bool InputConvertGame::processMouseMove(const QMouseEvent *from)
{
    if (QEvent::MouseMove != from->type()) {
        return false;
    }

    if (checkCursorPos(from)) {
        m_ctrlMouseMove.lastPos = QPointF(0.0, 0.0);
        return true;
    }

    auto lastPos = m_ctrlMouseMove.lastPos;
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    m_ctrlMouseMove.lastPos = from->localPos();
#else
    m_ctrlMouseMove.lastPos = from->position();
#endif

    if (m_ctrlMouseMove.ignoreCount > 0) {
        --m_ctrlMouseMove.ignoreCount;
        return true;
    }

    if (!lastPos.isNull() && m_processMouseMove) {
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        QPointF distance_raw{from->localPos() - lastPos};
#else
        QPointF distance_raw{from->position() - lastPos};
#endif
        QPointF speedRatio  {m_keyMap.getMouseMoveMap().data.mouseMove.speedRatio};
        QPointF distance    {distance_raw.x() / speedRatio.x(), distance_raw.y() / speedRatio.y()};

        mouseMoveStartTouch(from);
        startMouseMoveTimer();

        m_ctrlMouseMove.lastConverPos.setX(m_ctrlMouseMove.lastConverPos.x() + distance.x() / m_showSize.width());
        m_ctrlMouseMove.lastConverPos.setY(m_ctrlMouseMove.lastConverPos.y() + distance.y() / m_showSize.height());

        if (m_ctrlMouseMove.lastConverPos.x() < 0.05 || m_ctrlMouseMove.lastConverPos.x() > 0.95 || m_ctrlMouseMove.lastConverPos.y() < 0.05
            || m_ctrlMouseMove.lastConverPos.y() > 0.95) {
            if (m_ctrlMouseMove.smallEyes) {
                m_processMouseMove = false;
                int delay = 30;
                QTimer::singleShot(delay, this, [this]() { mouseMoveStopTouch(); });
                QTimer::singleShot(delay * 2, this, [this]() {
                    mouseMoveStartTouch(nullptr);
                    m_processMouseMove = true;
                });
            } else {
                mouseMoveStopTouch();
                m_ctrlMouseMove.ignoreCount = 5;
                return true;
            }
        }

        sendTouchMoveEvent(getTouchID(Qt::ExtraButton24), m_ctrlMouseMove.lastConverPos);
    }

    return true;
}

bool InputConvertGame::checkCursorPos(const QMouseEvent *from)
{
    bool moveCursor = false;
    QPoint pos = from->pos();
    if (pos.x() < CURSOR_POS_CHECK) {
        pos.setX(m_showSize.width() - CURSOR_POS_CHECK);
        moveCursor = true;
    } else if (pos.x() > m_showSize.width() - CURSOR_POS_CHECK) {
        pos.setX(CURSOR_POS_CHECK);
        moveCursor = true;
    } else if (pos.y() < CURSOR_POS_CHECK) {
        pos.setY(m_showSize.height() - CURSOR_POS_CHECK);
        moveCursor = true;
    } else if (pos.y() > m_showSize.height() - CURSOR_POS_CHECK) {
        pos.setY(CURSOR_POS_CHECK);
        moveCursor = true;
    }

    if (moveCursor) {
        moveCursorTo(from, pos);
    }

    return moveCursor;
}

void InputConvertGame::moveCursorTo(const QMouseEvent *from, const QPoint &localPosPixel)
{
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QPoint posOffset = from->pos() - localPosPixel;
    QPoint globalPos = from->globalPos();
#else
    QPoint posOffset = from->position().toPoint() - localPosPixel;
    QPoint globalPos = from->globalPosition().toPoint();
#endif
    globalPos -= posOffset;
    //qDebug()<<"move cursor to "<<globalPos<<" offset "<<posOffset;
#ifdef Q_OS_MACOS
    // On macOS, QCursor::setPos() posts a synthetic mouse-moved event (CGEventPost)
    // and does NOT re-associate the hardware mouse with the on-screen cursor, so the
    // warp does not "stick": the next hardware delta snaps the cursor back. That makes
    // mouseMoveMap re-centering a no-op, and the camera can no longer pan past the edge.
    // CGWarpMouseCursorPosition performs a real warp, and
    // CGAssociateMouseAndMouseCursorPosition(true) immediately restores the
    // mouse<->cursor association so subsequent hardware deltas keep flowing. This
    // mirrors the approach already used in util/mousetap/cocoamousetap.mm.
    CGWarpMouseCursorPosition(CGPointMake(globalPos.x(), globalPos.y()));
    CGAssociateMouseAndMouseCursorPosition(true);
#else
    QCursor::setPos(globalPos);
#endif
}

void InputConvertGame::mouseMoveStartTouch(const QMouseEvent *from)
{
    Q_UNUSED(from)
    if (!m_ctrlMouseMove.touching) {
        QPointF mouseMoveStartPos
            = m_ctrlMouseMove.smallEyes ? m_keyMap.getMouseMoveMap().data.mouseMove.smallEyes.pos : m_keyMap.getMouseMoveMap().data.mouseMove.startPos;
        int id = attachTouchID(Qt::ExtraButton24);
        sendTouchDownEvent(id, mouseMoveStartPos);
        m_ctrlMouseMove.lastConverPos = mouseMoveStartPos;
        m_ctrlMouseMove.touching = true;
    }
}

void InputConvertGame::mouseMoveStopTouch()
{
    if (m_ctrlMouseMove.touching) {
        int id = getTouchID(Qt::ExtraButton24);
        sendTouchUpEvent(id, m_ctrlMouseMove.lastConverPos);
        detachTouchID(Qt::ExtraButton24);
        m_ctrlMouseMove.touching = false;
    }
}

void InputConvertGame::startMouseMoveTimer()
{
    stopMouseMoveTimer();
    m_ctrlMouseMove.timer = startTimer(500);
}

void InputConvertGame::stopMouseMoveTimer()
{
    if (0 != m_ctrlMouseMove.timer) {
        killTimer(m_ctrlMouseMove.timer);
        m_ctrlMouseMove.timer = 0;
    }
}

bool InputConvertGame::switchGameMap()
{
    m_gameMap = !m_gameMap;
    qInfo() << QString("current keymap mode: %1").arg(m_gameMap ? "custom" : "normal");

    if (!m_keyMap.isValidMouseMoveMap()) {
        return m_gameMap;
    }
#ifdef QT_NO_DEBUG
    // grab cursor and set cursor only mouse move map
    emit grabCursor(m_gameMap);
#endif
    hideMouseCursor(m_gameMap);

    if (!m_gameMap) {
        stopMouseMoveTimer();
        mouseMoveStopTouch();
    }

    return m_gameMap;
}

void InputConvertGame::hideMouseCursor(bool hide)
{
    if (hide) {
        if (m_cursorHiddenByKeymap) {
            return;
        }
#ifdef QT_NO_DEBUG
        QGuiApplication::setOverrideCursor(QCursor(Qt::BlankCursor));
#else
        QGuiApplication::setOverrideCursor(QCursor(Qt::CrossCursor));
#endif
        m_cursorHiddenByKeymap = true;
    } else {
        if (!m_cursorHiddenByKeymap) {
            return;
        }
        QGuiApplication::restoreOverrideCursor();
        m_cursorHiddenByKeymap = false;
    }
}

void InputConvertGame::resetStuckTouches()
{
    stopMouseMoveTimer();
    mouseMoveStopTouch();

    if (m_ctrlSteerWheel.delayData.timer && m_ctrlSteerWheel.delayData.timer->isActive()) {
        m_ctrlSteerWheel.delayData.timer->stop();
    }
    m_ctrlSteerWheel.delayData.queuePos.clear();
    m_ctrlSteerWheel.delayData.queueTimer.clear();

    if (m_ctrlSteerWheel.touchKey != Qt::Key_unknown) {
        int id = getTouchID(m_ctrlSteerWheel.touchKey);
        if (id >= 0) {
            QPointF upPos = m_ctrlSteerWheel.delayData.currentPos;
            if (upPos.isNull()) {
                upPos = QPointF(0.5, 0.5);
            }
            sendTouchUpEvent(id, upPos);
            detachTouchID(m_ctrlSteerWheel.touchKey);
        }
        m_ctrlSteerWheel.touchKey = Qt::Key_unknown;
    }
    m_ctrlSteerWheel.pressedUp = false;
    m_ctrlSteerWheel.pressedDown = false;
    m_ctrlSteerWheel.pressedLeft = false;
    m_ctrlSteerWheel.pressedRight = false;
    m_ctrlSteerWheel.pressedSprint = false;
    m_ctrlSteerWheel.delayData.pressedNum = 0;
    m_ctrlSteerWheel.delayData.currentPos = QPointF();

    if (m_dragDelayData.timer) {
        m_dragDelayData.timer->stop();
        delete m_dragDelayData.timer;
        m_dragDelayData.timer = nullptr;
    }
    m_dragDelayData.queuePos.clear();
    m_dragDelayData.queueTimer.clear();
    if (m_dragDelayData.pressKey) {
        int id = getTouchID(m_dragDelayData.pressKey);
        if (id >= 0) {
            QPointF upPos = m_dragDelayData.currentPos;
            if (upPos.isNull()) {
                upPos = QPointF(0.5, 0.5);
            }
            sendTouchUpEvent(id, upPos);
            detachTouchID(m_dragDelayData.pressKey);
        }
        m_dragDelayData.pressKey = 0;
        m_dragDelayData.currentPos = QPointF();
    }

    for (int i = 0; i < MULTI_TOUCH_MAX_NUM; ++i) {
        if (0 == m_multiTouchID[i]) {
            continue;
        }
        sendTouchUpEvent(i, QPointF(0.5, 0.5));
        m_multiTouchID[i] = 0;
    }

    m_ctrlMouseMove.ignoreCount = 0;
    m_ctrlMouseMove.lastPos = QPointF(0.0, 0.0);
    m_ctrlMouseMove.smallEyes = false;
    m_processMouseMove = true;
}

void InputConvertGame::setVideoWindowFocused(bool focused)
{
    if (!m_gameMap) {
        return;
    }

    if (!focused) {
        // Focus loss often drops KeyRelease events and leaves virtual fingers held down.
        // Also ClipCursor is typically cleared by the OS; release our grab state explicitly.
        if (m_keyMap.isValidMouseMoveMap()) {
#ifdef QT_NO_DEBUG
            emit grabCursor(false);
#endif
            if (!m_needBackMouseMove) {
                hideMouseCursor(false);
            }
        }
        resetStuckTouches();
        return;
    }

    // Returning to the window: restore cursor capture so mouse-look works again.
    if (m_keyMap.isValidMouseMoveMap() && !m_needBackMouseMove) {
#ifdef QT_NO_DEBUG
        emit grabCursor(true);
#endif
        hideMouseCursor(true);
    }
}

void InputConvertGame::timerEvent(QTimerEvent *event)
{
    if (m_ctrlMouseMove.timer == event->timerId()) {
        stopMouseMoveTimer();
        // Don't auto-reset view when smallEyes mode is active
        if (!m_ctrlMouseMove.smallEyes) {
            mouseMoveStopTouch();
        }
    }
}
