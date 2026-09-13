
#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <QObject>
#include <QPointer>
#include <QSize>
#include <QElapsedTimer>
#include <array>

#include "inputconvertbase.h"

class QTcpSocket;
class Receiver;
class InputConvertBase;
class DeviceMsg;
class Controller : public QObject
{
    Q_OBJECT
public:
    Controller(std::function<qint64(const QByteArray&)> sendData, QString gameScript = "", QObject *parent = Q_NULLPTR);
    virtual ~Controller();

    void postControlMsg(ControlMsg *controlMsg);
    void setCameraMode(bool cameraMode);
    void recvDeviceMsg(DeviceMsg *deviceMsg);
    void test(QRect rc);

    void updateScript(QString gameScript = "");
    bool isCurrentCustomKeymap();
    void setVideoWindowFocused(bool focused);

    void postGoBack();
    void postGoHome();
    void postGoMenu();
    void postAppSwitch();
    void postPower();
    void postVolumeUp();
    void postVolumeDown();
    void copy();
    void cut();
    void expandNotificationPanel();
    void expandSettingsPanel();
    void collapsePanel();
    void rotateDevice();
    void startApp(const QString &name);
    void scanFile(const QString &path);
    void resizeDisplay(const QSize &size);
    void setDisplayPower(bool on);
    void setCameraTorch(bool on);
    void cameraZoomIn();
    void cameraZoomOut();

    // for input convert
    void mouseEvent(const QMouseEvent *from, const QSize &frameSize, const QSize &showSize);
    void wheelEvent(const QWheelEvent *from, const QSize &frameSize, const QSize &showSize);
    void keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize);

    // turn the screen on if it was off, press BACK otherwise
    // If the screen is off, it is turned on only on down
    void postBackOrScreenOn(bool down);
    void requestDeviceClipboard();
    void getDeviceClipboard(bool cut = false);
    void setDeviceClipboard(bool pause = true);
    void clipboardPaste();
    void postTextInput(QString &text);
    void dumpControlTrace(quint64 gestureSequence, const QString &reason);

signals:
    void grabCursor(bool grab);

protected:
    bool event(QEvent *event);

private:
    bool sendControl(const QByteArray &buffer, ControlMsg *controlMsg = nullptr);
    void postKeyCodeClick(AndroidKeycode keycode);
    void sendPendingResize();
    void recordControlTrace(const ControlMsg *controlMsg, int stage, int bytes = -1, qint64 written = -1, bool success = false);
    QString controlTraceStageName(int stage) const;

    enum ControlTraceStage
    {
        CTS_POST = 0,
        CTS_QUEUED,
        CTS_DISPATCH,
        CTS_SERIALIZED,
        CTS_SEND_BEGIN,
        CTS_SEND_RESULT
    };

    struct ControlTraceEntry
    {
        qint64 elapsedMs = 0;
        quint64 sequence = 0;
        quint64 gestureSequence = 0;
        int stage = CTS_POST;
        int action = -1;
        int id = -1;
        int bytes = -1;
        qint64 written = -1;
        bool success = false;
    };

    static constexpr int CONTROL_TRACE_CAPACITY = 512;

private:
    QPointer<Receiver> m_receiver;
    QPointer<InputConvertBase> m_inputConvert;
    std::function<qint64(const QByteArray&)> m_sendData = Q_NULLPTR;
    QSize m_pendingResize;
    bool m_resizeQueued = false;
    bool m_cameraMode = false;
    QElapsedTimer m_controlTraceTimer;
    std::array<ControlTraceEntry, CONTROL_TRACE_CAPACITY> m_controlTrace {};
    int m_controlTraceNext = 0;
    int m_controlTraceSize = 0;
};

#endif // CONTROLLER_H
