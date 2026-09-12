#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QTimer>

#include "controller.h"
#include "controlmsg.h"
#include "inputconvertgame.h"
#include "receiver.h"
#include "videosocket.h"

namespace {
QString controllerLogTime()
{
    return QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
}
}

Controller::Controller(std::function<qint64(const QByteArray&)> sendData, QString gameScript, QObject *parent)
    : QObject(parent)
    , m_sendData(sendData)
{
    m_controlTraceTimer.start();
    m_receiver = new Receiver(this);
    Q_ASSERT(m_receiver);

    updateScript(gameScript);
}

Controller::~Controller() {}

QString Controller::controlTraceStageName(int stage) const
{
    switch (stage) {
    case CTS_POST:
        return QStringLiteral("post");
    case CTS_QUEUED:
        return QStringLiteral("queued");
    case CTS_DISPATCH:
        return QStringLiteral("dispatch");
    case CTS_SERIALIZED:
        return QStringLiteral("serialized");
    case CTS_SEND_BEGIN:
        return QStringLiteral("send-begin");
    case CTS_SEND_RESULT:
        return QStringLiteral("send-result");
    default:
        return QStringLiteral("stage-%1").arg(stage);
    }
}

void Controller::recordControlTrace(const ControlMsg *controlMsg, int stage,
                                    int bytes, qint64 written, bool success)
{
    if (!controlMsg || !controlMsg->hasDebugTrace()) {
        return;
    }

    ControlTraceEntry &entry = m_controlTrace[m_controlTraceNext];
    entry.elapsedMs = m_controlTraceTimer.elapsed();
    entry.sequence = controlMsg->debugSequence();
    entry.gestureSequence = controlMsg->debugGestureSequence();
    entry.stage = stage;
    entry.action = controlMsg->debugAction();
    entry.id = controlMsg->debugId();
    entry.bytes = bytes;
    entry.written = written;
    entry.success = success;
    m_controlTraceNext = (m_controlTraceNext + 1) % CONTROL_TRACE_CAPACITY;
    m_controlTraceSize = qMin(m_controlTraceSize + 1, CONTROL_TRACE_CAPACITY);
}

void Controller::dumpControlTrace(quint64 gestureSequence, const QString &reason)
{
    const qint64 nowElapsed = m_controlTraceTimer.elapsed();
    const QDateTime now = QDateTime::currentDateTime();
    const int first = (m_controlTraceNext - m_controlTraceSize + CONTROL_TRACE_CAPACITY)
        % CONTROL_TRACE_CAPACITY;
    int matchingEntries = 0;

    qWarning().noquote() << controllerLogTime()
                         << "[ControlTrace] begin"
                         << "reason:" << reason
                         << "gesture:" << gestureSequence
                         << "entries:" << m_controlTraceSize;

    for (int i = 0; i < m_controlTraceSize; ++i) {
        const ControlTraceEntry &entry = m_controlTrace[(first + i) % CONTROL_TRACE_CAPACITY];
        if (gestureSequence != 0 && entry.gestureSequence != gestureSequence) {
            continue;
        }
        ++matchingEntries;
        const QDateTime eventTime = now.addMSecs(entry.elapsedMs - nowElapsed);
        qWarning().noquote() << eventTime.toString(Qt::ISODateWithMs)
                             << "[ControlTrace]"
                             << "elapsedMs:" << entry.elapsedMs
                             << "stage:" << controlTraceStageName(entry.stage)
                             << "seq:" << entry.sequence
                             << "gesture:" << entry.gestureSequence
                             << "action:" << entry.action
                             << "id:" << entry.id
                             << "bytes:" << entry.bytes
                             << "written:" << entry.written
                             << "success:" << entry.success;
    }

    qWarning().noquote() << controllerLogTime()
                         << "[ControlTrace] end"
                         << "matchedEntries:" << matchingEntries;
}

void Controller::postControlMsg(ControlMsg *controlMsg)
{
    if (!controlMsg) {
        qWarning().noquote() << controllerLogTime()
                             << "[Controller] postControlMsg ignored: null message";
        return;
    }

    recordControlTrace(controlMsg, CTS_POST);

    if (m_cameraMode) {
        const auto type = controlMsg->type();
        const bool isCameraControl = type == ControlMsg::CMT_CAMERA_SET_TORCH
                || type == ControlMsg::CMT_CAMERA_ZOOM_IN
                || type == ControlMsg::CMT_CAMERA_ZOOM_OUT;
        if (!isCameraControl) {
            qWarning().noquote() << controllerLogTime()
                                 << "[Controller] postControlMsg dropped in camera mode"
                                 << "msg:" << static_cast<const void *>(controlMsg)
                                 << "type:" << type
                                 << "traceSeq:" << controlMsg->debugSequence();
            delete controlMsg;
            return;
        }
    }

    QCoreApplication::postEvent(this, controlMsg);
    recordControlTrace(controlMsg, CTS_QUEUED);
}

void Controller::setCameraMode(bool cameraMode)
{
    m_cameraMode = cameraMode;
}

void Controller::recvDeviceMsg(DeviceMsg *deviceMsg)
{
    if (!m_receiver) {
        return;
    }

    m_receiver->recvDeviceMsg(deviceMsg);
}

void Controller::test(QRect rc)
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_INJECT_TOUCH);
    controlMsg->setInjectTouchMsgData(
        static_cast<quint64>(POINTER_ID_MOUSE), AMOTION_EVENT_ACTION_DOWN, AMOTION_EVENT_BUTTON_PRIMARY, AMOTION_EVENT_BUTTON_PRIMARY, rc, 1.0f);
    postControlMsg(controlMsg);
}

void Controller::updateScript(QString gameScript)
{
    if (m_inputConvert) {
        delete m_inputConvert;
    }
    if (!gameScript.isEmpty()) {
        InputConvertGame *convertgame = new InputConvertGame(this);
        convertgame->loadKeyMap(gameScript);
        m_inputConvert = convertgame;
    } else {
        m_inputConvert = new InputConvertNormal(this);
    }
    Q_ASSERT(m_inputConvert);
    connect(m_inputConvert, &InputConvertBase::grabCursor, this, &Controller::grabCursor);
}

bool Controller::isCurrentCustomKeymap()
{
    if (!m_inputConvert) {
        return false;
    }

    return m_inputConvert->isCurrentCustomKeymap();
}

void Controller::postBackOrScreenOn(bool down)
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_BACK_OR_SCREEN_ON);
    controlMsg->setBackOrScreenOnData(down);
    if (!controlMsg) {
        return;
    }
    postControlMsg(controlMsg);
}

void Controller::postGoHome()
{
    postKeyCodeClick(AKEYCODE_HOME);
}

void Controller::postGoMenu()
{
    postKeyCodeClick(AKEYCODE_MENU);
}

void Controller::postGoBack()
{
    postKeyCodeClick(AKEYCODE_BACK);
}

void Controller::postAppSwitch()
{
    postKeyCodeClick(AKEYCODE_APP_SWITCH);
}

void Controller::postPower()
{
    postKeyCodeClick(AKEYCODE_POWER);
}

void Controller::postVolumeUp()
{
    postKeyCodeClick(AKEYCODE_VOLUME_UP);
}

void Controller::postVolumeDown()
{
    postKeyCodeClick(AKEYCODE_VOLUME_DOWN);
}

void Controller::copy()
{
    postKeyCodeClick(AKEYCODE_COPY);
}

void Controller::cut()
{
    postKeyCodeClick(AKEYCODE_CUT);
}

void Controller::expandNotificationPanel()
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_EXPAND_NOTIFICATION_PANEL);
    if (!controlMsg) {
        return;
    }
    postControlMsg(controlMsg);
}

void Controller::expandSettingsPanel()
{
    postControlMsg(new ControlMsg(ControlMsg::CMT_EXPAND_SETTINGS_PANEL));
}

void Controller::collapsePanel()
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_COLLAPSE_PANELS);
    if (!controlMsg) {
        return;
    }
    postControlMsg(controlMsg);
}

void Controller::rotateDevice()
{
    postControlMsg(new ControlMsg(ControlMsg::CMT_ROTATE_DEVICE));
}

void Controller::startApp(const QString &name)
{
    if (name.isEmpty()) {
        return;
    }
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_START_APP);
    controlMsg->setStartAppData(name);
    postControlMsg(controlMsg);
}

void Controller::scanFile(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_SCAN_FILE);
    controlMsg->setScanFileData(path);
    postControlMsg(controlMsg);
}

void Controller::resizeDisplay(const QSize &size)
{
    if (size.width() <= 0 || size.height() <= 0) {
        return;
    }
    m_pendingResize = size;
    if (m_resizeQueued) {
        return;
    }
    m_resizeQueued = true;
    QTimer::singleShot(0, this, &Controller::sendPendingResize);
}

void Controller::sendPendingResize()
{
    m_resizeQueued = false;
    if (m_pendingResize.isEmpty()) {
        return;
    }
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_RESIZE_DISPLAY);
    controlMsg->setResizeDisplayData(m_pendingResize);
    m_pendingResize = QSize();
    postControlMsg(controlMsg);
}

void Controller::requestDeviceClipboard()
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_GET_CLIPBOARD);
    if (!controlMsg) {
        return;
    }
    postControlMsg(controlMsg);
}

void Controller::getDeviceClipboard(bool cut)
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_GET_CLIPBOARD);
    if (!controlMsg) {
        return;
    }
    ControlMsg::GetClipboardCopyKey copyKey = cut ? ControlMsg::GCCK_CUT : ControlMsg::GCCK_COPY;
    controlMsg->setGetClipboardMsgData(copyKey);
    postControlMsg(controlMsg);
}

void Controller::setDeviceClipboard(bool pause)
{
    QClipboard *board = QApplication::clipboard();
    QString text = board->text();
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_SET_CLIPBOARD);
    if (!controlMsg) {
        return;
    }
    controlMsg->setSetClipboardMsgData(text, pause);
    postControlMsg(controlMsg);
}

void Controller::clipboardPaste()
{
    QClipboard *board = QApplication::clipboard();
    QString text = board->text();
    postTextInput(text);
}

void Controller::postTextInput(QString &text)
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_INJECT_TEXT);
    if (!controlMsg) {
        return;
    }
    controlMsg->setInjectTextMsgData(text);
    postControlMsg(controlMsg);
}

void Controller::setDisplayPower(bool on)
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_SET_DISPLAY_POWER);
    if (!controlMsg) {
        return;
    }
    controlMsg->setDisplayPowerData(on);
    postControlMsg(controlMsg);
}

void Controller::setCameraTorch(bool on)
{
    ControlMsg *controlMsg = new ControlMsg(ControlMsg::CMT_CAMERA_SET_TORCH);
    controlMsg->setCameraTorchData(on);
    postControlMsg(controlMsg);
}

void Controller::cameraZoomIn()
{
    postControlMsg(new ControlMsg(ControlMsg::CMT_CAMERA_ZOOM_IN));
}

void Controller::cameraZoomOut()
{
    postControlMsg(new ControlMsg(ControlMsg::CMT_CAMERA_ZOOM_OUT));
}

void Controller::mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize)
{
    if (m_inputConvert) {
        m_inputConvert->mouseEvent(from, frameSize, showSize);
    }
}

void Controller::wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize)
{
    if (m_inputConvert) {
        m_inputConvert->wheelEvent(from, frameSize, showSize);
    }
}

void Controller::keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize)
{
    if (m_inputConvert) {
        m_inputConvert->keyEvent(from, frameSize, showSize);
    }
}

bool Controller::event(QEvent *event)
{
    if (event && static_cast<ControlMsg::Type>(event->type()) == ControlMsg::Control) {
        ControlMsg *controlMsg = dynamic_cast<ControlMsg *>(event);
        if (controlMsg) {
            recordControlTrace(controlMsg, CTS_DISPATCH);
            const QByteArray buffer = controlMsg->serializeData();
            recordControlTrace(controlMsg, CTS_SERIALIZED, buffer.size());
            sendControl(buffer, controlMsg);
        }
        return true;
    }
    return QObject::event(event);
}

bool Controller::sendControl(const QByteArray &buffer, ControlMsg *controlMsg)
{
    if (buffer.isEmpty()) {
        qWarning().noquote() << controllerLogTime()
                             << "[Controller] sendControl failed: empty buffer";
        return false;
    }
    qint32 len = 0;
    if (m_sendData) {
        recordControlTrace(controlMsg, CTS_SEND_BEGIN, buffer.size());
        len = static_cast<qint32>(m_sendData(buffer));
        recordControlTrace(controlMsg, CTS_SEND_RESULT, buffer.size(), len, len == buffer.length());
    } else {
        qWarning().noquote() << controllerLogTime()
                             << "[Controller] sendControl failed: no sender";
    }
    return len == buffer.length() ? true : false;
}

void Controller::postKeyCodeClick(AndroidKeycode keycode)
{
    ControlMsg *controlEventDown = new ControlMsg(ControlMsg::CMT_INJECT_KEYCODE);
    if (!controlEventDown) {
        return;
    }
    controlEventDown->setInjectKeycodeMsgData(AKEY_EVENT_ACTION_DOWN, keycode, 0, AMETA_NONE);
    postControlMsg(controlEventDown);

    ControlMsg *controlEventUp = new ControlMsg(ControlMsg::CMT_INJECT_KEYCODE);
    if (!controlEventUp) {
        return;
    }
    controlEventUp->setInjectKeycodeMsgData(AKEY_EVENT_ACTION_UP, keycode, 0, AMETA_NONE);
    postControlMsg(controlEventUp);
}
