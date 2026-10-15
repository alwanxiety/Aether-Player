#pragma once

#include <QObject>
#include <QUdpSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFile>
#include <QMap>
#include <QByteArray>

struct DlnaDevice {
    QString name;
    QUrl controlUrl;
};

class DlnaCastService : public QObject {
    Q_OBJECT
public:
    explicit DlnaCastService(QObject* parent = nullptr);
    ~DlnaCastService() override;

    void discoverDevices();
    void castTrack(const DlnaDevice& device, const QString& localAudioPath, const QString& title, const QString& artist, const QByteArray& coverBytes);
    void stopCasting(const DlnaDevice& device);
    void seekTrack(const DlnaDevice& device, qint64 positionMs);
    void getPositionInfo(const DlnaDevice& device);

    const QMap<QString, DlnaDevice>& foundDevices() const { return m_devices; }

    signals:
        void deviceFound(const DlnaDevice& dev);
    void castStatusChanged(const QString& msg);
    void positionInfoReceived(qint64 absTimeMs, qint64 trackDurationMs);

private slots:
    void onSsdpDatagram();
    void onHttpConnection();
    void parseDeviceDescription(const QString& xmlUrl);

private:
    QString getLocalIpAddress();
    void sendSoapCommand(const QUrl& controlUrl, const QString& action, const QString& body);
    QString buildDidlMetaData(const QString& streamUrl, const QString& coverUrl, const QString& title, const QString& artist);

    QUdpSocket* m_ssdpSocket = nullptr;
    QTcpServer* m_httpServer = nullptr;
    QNetworkAccessManager* m_netManager = nullptr;

    QMap<QString, DlnaDevice> m_devices;
    QString m_currentServePath;
    QByteArray m_currentCoverBytes;
    quint16 m_serverPort = 8877;
};
