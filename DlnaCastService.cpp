#include "DlnaCastService.h"
#include <QNetworkInterface>
#include <QXmlStreamReader>
#include <QRegularExpression>
#include <QTimer>
#include <QDebug>

static const char* SSDP_IP = "239.255.255.250";
static const quint16 SSDP_PORT = 1900;

DlnaCastService::DlnaCastService(QObject* parent) : QObject(parent) {
    m_netManager = new QNetworkAccessManager(this);

    m_httpServer = new QTcpServer(this);
    if (!m_httpServer->listen(QHostAddress::AnyIPv4, m_serverPort)) {
        m_httpServer->listen(QHostAddress::AnyIPv4, 0);
        m_serverPort = m_httpServer->serverPort();
    }
    connect(m_httpServer, &QTcpServer::newConnection, this, &DlnaCastService::onHttpConnection);

    m_ssdpSocket = new QUdpSocket(this);
    m_ssdpSocket->bind(QHostAddress::AnyIPv4, 0, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    connect(m_ssdpSocket, &QUdpSocket::readyRead, this, &DlnaCastService::onSsdpDatagram);

    discoverDevices();
}

DlnaCastService::~DlnaCastService() {
    if (m_httpServer) m_httpServer->close();
}

QString DlnaCastService::getLocalIpAddress() {
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const auto& iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp) || (iface.flags() & QNetworkInterface::IsLoopBack))
            continue;

        for (const auto& entry : iface.addressEntries()) {
            const auto ip = entry.ip();
            if (ip.protocol() == QAbstractSocket::IPv4Protocol && !ip.isLoopback()) {
                const QString s = ip.toString();
                if (s.startsWith("192.168.") || s.startsWith("10.") || s.startsWith("172.")) {
                    return s;
                }
            }
        }
    }
    return "127.0.0.1";
}

void DlnaCastService::discoverDevices() {
    const QByteArray ssdpQueryRenderer =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 3\r\n"
        "ST: urn:schemas-upnp-org:device:MediaRenderer:1\r\n\r\n";

    const QByteArray ssdpQueryRoot =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 3\r\n"
        "ST: upnp:rootdevice\r\n\r\n";

    m_ssdpSocket->writeDatagram(ssdpQueryRenderer, QHostAddress(SSDP_IP), SSDP_PORT);
    m_ssdpSocket->writeDatagram(ssdpQueryRoot, QHostAddress(SSDP_IP), SSDP_PORT);
}

void DlnaCastService::onSsdpDatagram() {
    while (m_ssdpSocket->hasPendingDatagrams()) {
        QByteArray buffer;
        buffer.resize(static_cast<qsizetype>(m_ssdpSocket->pendingDatagramSize()));
        m_ssdpSocket->readDatagram(buffer.data(), buffer.size());

        const QString response = QString::fromUtf8(buffer);
        if (response.contains("LOCATION:", Qt::CaseInsensitive)) {
            static const QRegularExpression locRegex("LOCATION:\\s*([^\r\n]+)", QRegularExpression::CaseInsensitiveOption);
            auto match = locRegex.match(response);
            if (match.hasMatch()) {
                parseDeviceDescription(match.captured(1).trimmed());
            }
        }
    }
}

void DlnaCastService::parseDeviceDescription(const QString& xmlUrl) {
    QUrl url(xmlUrl);
    auto* reply = m_netManager->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, url]() {
        if (reply->error() == QNetworkReply::NoError) {
            QXmlStreamReader xml(reply->readAll());
            QString friendlyName;
            QString controlUrlStr;
            bool insideService = false;
            bool isAVTransport = false;

            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isStartElement()) {
                    const QString tag = xml.name().toString();
                    if (tag == "friendlyName" && friendlyName.isEmpty()) {
                        friendlyName = xml.readElementText();
                    } else if (tag == "service") {
                        insideService = true;
                        isAVTransport = false;
                    } else if (insideService && tag == "serviceType") {
                        if (xml.readElementText().contains("AVTransport")) {
                            isAVTransport = true;
                        }
                    } else if (insideService && isAVTransport && tag == "controlURL") {
                        controlUrlStr = xml.readElementText();
                    }
                } else if (xml.isEndElement() && xml.name() == "service") {
                    insideService = false;
                }
            }

            if (!friendlyName.isEmpty() && !controlUrlStr.isEmpty()) {
                QUrl fullControlUrl = url.resolved(QUrl(controlUrlStr));
                if (!m_devices.contains(friendlyName)) {
                    DlnaDevice dev{friendlyName, fullControlUrl};
                    m_devices.insert(friendlyName, dev);
                    emit deviceFound(dev);
                }
            }
        }
        reply->deleteLater();
    });
}

void DlnaCastService::onHttpConnection() {
    QTcpSocket* client = m_httpServer->nextPendingConnection();

    connect(client, &QTcpSocket::readyRead, this, [this, client]() {
        const QByteArray req = client->readAll();
        const QString reqStr = QString::fromUtf8(req);

        if (reqStr.contains("GET /cover.jpg") || reqStr.contains("HEAD /cover.jpg")) {
            if (m_currentCoverBytes.isEmpty()) {
                client->write("HTTP/1.1 404 Not Found\r\n\r\n");
                client->disconnectFromHost();
                return;
            }

            const QString header = QString(
                "HTTP/1.1 200 OK\r\n"
                "Server: AetherDlna/1.0\r\n"
                "Content-Type: image/jpeg\r\n"
                "Content-Length: %1\r\n"
                "Connection: close\r\n\r\n"
            ).arg(m_currentCoverBytes.size());

            client->write(header.toUtf8());
            if (reqStr.startsWith("GET")) {
                client->write(m_currentCoverBytes);
            }
            client->disconnectFromHost();
            return;
        }

        if (!reqStr.startsWith("GET") && !reqStr.startsWith("HEAD")) return;

        auto* file = new QFile(m_currentServePath, client);
        if (!file->open(QIODevice::ReadOnly)) {
            client->write("HTTP/1.1 404 Not Found\r\n\r\n");
            client->disconnectFromHost();
            return;
        }

        const qint64 fileSize = file->size();
        qint64 startByte = 0;
        qint64 endByte = fileSize - 1;
        bool isPartial = false;

        static const QRegularExpression rangeRegex(R"(Range:\s*bytes=(\d+)-(\d*))", QRegularExpression::CaseInsensitiveOption);
        auto match = rangeRegex.match(reqStr);
        if (match.hasMatch()) {
            isPartial = true;
            startByte = match.captured(1).toLongLong();
            if (!match.captured(2).isEmpty()) {
                endByte = match.captured(2).toLongLong();
            }
            if (endByte >= fileSize) endByte = fileSize - 1;
        }

        const qint64 contentLength = endByte - startByte + 1;
        file->seek(startByte);

        QString header;
        if (isPartial) {
            header = QString(
                "HTTP/1.1 206 Partial Content\r\n"
                "Server: AetherDlna/1.0\r\n"
                "Content-Type: audio/mpeg\r\n"
                "Content-Range: bytes %1-%2/%3\r\n"
                "Content-Length: %4\r\n"
                "Accept-Ranges: bytes\r\n"
                "Connection: keep-alive\r\n\r\n"
            ).arg(startByte).arg(endByte).arg(fileSize).arg(contentLength);
        } else {
            header = QString(
                "HTTP/1.1 200 OK\r\n"
                "Server: AetherDlna/1.0\r\n"
                "Content-Type: audio/mpeg\r\n"
                "Content-Length: %1\r\n"
                "Accept-Ranges: bytes\r\n"
                "Connection: keep-alive\r\n\r\n"
            ).arg(fileSize);
        }

        client->write(header.toUtf8());

        if (reqStr.startsWith("HEAD")) {
            client->disconnectFromHost();
            return;
        }

        auto sendNextChunk = [client, file, endByte]() {
            if (!client || !file->isOpen()) return;
            while (client->bytesToWrite() < 128 * 1024 && file->pos() <= endByte && !file->atEnd()) {
                const qint64 remaining = endByte - file->pos() + 1;
                const qint64 chunkSize = std::min<qint64>(64 * 1024, remaining);
                const QByteArray chunk = file->read(chunkSize);
                if (chunk.isEmpty()) break;
                client->write(chunk);
            }
            if (file->pos() > endByte || file->atEnd()) {
                file->close();
            }
        };

        connect(client, &QTcpSocket::bytesWritten, client, [sendNextChunk](qint64) {
            sendNextChunk();
        });

        sendNextChunk();
    });
}

QString DlnaCastService::buildDidlMetaData(const QString& streamUrl, const QString& coverUrl, const QString& title, const QString& artist) {
    QString escapedTitle = title.toHtmlEscaped();
    QString escapedArtist = artist.toHtmlEscaped();

    QString didl = QString(
        "&lt;DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" "
        "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
        "xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\"&gt;"
        "&lt;item id=\"0\" parentID=\"0\" restricted=\"1\"&gt;"
        "&lt;dc:title&gt;%1&lt;/dc:title&gt;"
        "&lt;upnp:artist&gt;%2&lt;/upnp:artist&gt;"
        "&lt;upnp:albumArtURI&gt;%3&lt;/upnp:albumArtURI&gt;"
        "&lt;upnp:class&gt;object.item.audioItem.musicTrack&lt;/upnp:class&gt;"
        "&lt;res protocolInfo=\"http-get:*:audio/mpeg:DLNA.ORG_PN=MP3;DLNA.ORG_OP=01\"&gt;%4&lt;/res&gt;"
        "&lt;/item&gt;&lt;/DIDL-Lite&gt;"
    ).arg(escapedTitle, escapedArtist, coverUrl, streamUrl);

    return didl;
}

void DlnaCastService::sendSoapCommand(const QUrl& controlUrl, const QString& action, const QString& body) {
    QNetworkRequest req(controlUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "text/xml; charset=\"utf-8\"");
    req.setRawHeader("SOAPAction", QString("\"urn:schemas-upnp-org:service:AVTransport:1#%1\"").arg(action).toUtf8());

    const QString soapXml = QString(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
        "<s:Body>\r\n"
        "<u:%1 xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">\r\n"
        "<InstanceID>0</InstanceID>\r\n"
        "%2"
        "</u:%1>\r\n"
        "</s:Body>\r\n"
        "</s:Envelope>\r\n"
    ).arg(action, body);

    m_netManager->post(req, soapXml.toUtf8());
}

void DlnaCastService::castTrack(const DlnaDevice& device, const QString& localAudioPath, const QString& title, const QString& artist, const QByteArray& coverBytes) {
    m_currentServePath = localAudioPath;
    m_currentCoverBytes = coverBytes;

    const QString hostIp = getLocalIpAddress();
    const QString streamUrl = QString("http://%1:%2/stream.mp3").arg(hostIp).arg(m_serverPort);
    const QString coverUrl = QString("http://%1:%2/cover.jpg").arg(hostIp).arg(m_serverPort);

    const QString metaData = buildDidlMetaData(streamUrl, coverUrl, title, artist);

    const QString setUriBody = QString(
        "<CurrentURI>%1</CurrentURI>\r\n"
        "<CurrentURIMetaData>%2</CurrentURIMetaData>\r\n"
    ).arg(streamUrl, metaData);

    sendSoapCommand(device.controlUrl, "SetAVTransportURI", setUriBody);

    QTimer::singleShot(350, this, [this, device]() {
        const QString playBody = "<Speed>1</Speed>\r\n";
        sendSoapCommand(device.controlUrl, "Play", playBody);
        emit castStatusChanged("Трансляция на: " + device.name);
    });
}

void DlnaCastService::stopCasting(const DlnaDevice& device) {
    sendSoapCommand(device.controlUrl, "Stop", "");
}

void DlnaCastService::seekTrack(const DlnaDevice& device, qint64 positionMs) {
    qint64 sec = positionMs / 1000;
    qint64 h = sec / 3600;
    qint64 m = (sec % 3600) / 60;
    qint64 s = sec % 60;
    QString targetTime = QString("%1:%2:%3").arg(h, 2, 10, QChar('0'))
                                           .arg(m, 2, 10, QChar('0'))
                                           .arg(s, 2, 10, QChar('0'));

    const QString seekBody = QString(
        "<Unit>REL_TIME</Unit>\r\n"
        "<Target>%1</Target>\r\n"
    ).arg(targetTime);

    sendSoapCommand(device.controlUrl, "Seek", seekBody);
}

void DlnaCastService::getPositionInfo(const DlnaDevice& device) {
    QNetworkRequest req(device.controlUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "text/xml; charset=\"utf-8\"");
    req.setRawHeader("SOAPAction", "\"urn:schemas-upnp-org:service:AVTransport:1#GetPositionInfo\"");

    const QString soapXml =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
        "<s:Body>\r\n"
        "<u:GetPositionInfo xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">\r\n"
        "<InstanceID>0</InstanceID>\r\n"
        "</u:GetPositionInfo>\r\n"
        "</s:Body>\r\n"
        "</s:Envelope>\r\n";

    auto* reply = m_netManager->post(req, soapXml.toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray data = reply->readAll();
            static const QRegularExpression relRx("<RelTime>([^<]+)</RelTime>");
            static const QRegularExpression durRx("<TrackDuration>([^<]+)</TrackDuration>");

            auto relMatch = relRx.match(QString::fromUtf8(data));
            auto durMatch = durRx.match(QString::fromUtf8(data));

            if (relMatch.hasMatch() && durMatch.hasMatch()) {
                auto parseTimeToMs = [](const QString& tStr) -> qint64 {
                    QStringList parts = tStr.split(':');
                    if (parts.size() == 3) {
                        return parts[0].toLongLong() * 3600000 +
                               parts[1].toLongLong() * 60000 +
                               parts[2].toDouble() * 1000;
                    }
                    return 0;
                };

                emit positionInfoReceived(parseTimeToMs(relMatch.captured(1)), parseTimeToMs(durMatch.captured(1)));
            }
        }
        reply->deleteLater();
    });
}