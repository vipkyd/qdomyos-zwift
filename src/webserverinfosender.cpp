#include "webserverinfosender.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QtWebSockets/QWebSocket>

WebServerInfoSender::WebServerInfoSender(const QString &id, QObject *parent) : TemplateInfoSender(id, parent) {
    fetcher = new QNetworkAccessManager(this);
    fetcher->setCookieJar(new QNoCookieJar());
    connect(fetcher, SIGNAL(finished(QNetworkReply *)), this, SLOT(handleFetcherRequest(QNetworkReply *)));
    connect(fetcher, SIGNAL(sslErrors(QNetworkReply *, const QList<QSslError> &)), this,
            SLOT(ignoreSSLErrors(QNetworkReply *, const QList<QSslError> &)));
}
WebServerInfoSender::~WebServerInfoSender() { innerStop(); }

void WebServerInfoSender::ignoreSSLErrors(QNetworkReply *repl, const QList<QSslError> &) { repl->ignoreSslErrors(); }

bool WebServerInfoSender::listen() {
    if (!innerTcpServer)
        innerTcpServer = new QTcpServer(this);
    if (!innerTcpServer->isListening()) {
        if (innerTcpServer->listen(QHostAddress::Any, port)) {
            if (!port) {
                settings.setValue(QStringLiteral("template_") + templateId + QStringLiteral("_port"),
                                  port = innerTcpServer->serverPort());
            }
            httpServer->bind(innerTcpServer);
            return true;
        } else {
            delete innerTcpServer;
            innerTcpServer = 0;
        }
    }
    return false;
}

bool WebServerInfoSender::isRunning() const { return innerTcpServer && innerTcpServer->isListening(); }
bool WebServerInfoSender::send(const QString &data) {
    if (isRunning() && !data.isEmpty()) {
        bool rv = true, oldrv = false;
        for (QWebSocket *client : sendToClients) {
            rv = client->sendTextMessage(data) > 0;
            if (!oldrv)
                oldrv = rv;
        }
        return rv;
    } else
        return false;
}

void WebServerInfoSender::innerStop() {
    if (innerTcpServer) {
        if (isRunning())
            innerTcpServer->close();
        httpServer->deleteLater();
        clients.clear();
        sendToClients.clear();
        reply2Req.clear();
        innerTcpServer = 0;
        httpServer = 0;
    }
}

bool WebServerInfoSender::init() {
    bool ok;
    folders = settings.value(QStringLiteral("template_") + templateId + QStringLiteral("_folders")).toStringList();
    if (!folders.isEmpty()) {
        QString relative;
        int idx;
        port = settings.value(QStringLiteral("template_") + templateId + QStringLiteral("_port"), 6666).toInt(&ok);
        if (!ok)
            port = 6666;
        if (!httpServer)
            httpServer = new QHttpServer(this);
        relative2Absolute.clear();
        for (auto fld : folders) {
            idx = fld.lastIndexOf('/');
            qDebug() << QStringLiteral("Folder") << fld;
            if (idx > 0) {
                relative = fld.mid(idx + 1);
                qDebug() << QStringLiteral("Relative") << relative;
                relative2Absolute.insert(relative, fld);
                httpServer->route(QStringLiteral("/") + relative + QStringLiteral("/<arg>"),
                                  [this](const QUrl &url, const QHttpServerRequest &request) {
                                      QUrl urlreq = request.url();
                                      QString path = urlreq.path().mid(1);
                                      int idxreq = path.indexOf('/');
                                      QString reqId = idxreq < 0 ? path : path.mid(0, idxreq);
                                      qDebug() << QStringLiteral("Path") << path << QStringLiteral("req") << reqId;
                                      path = relative2Absolute.value(reqId);
                                      if (path.isEmpty())
                                          return QHttpServerResponse("text/plain", "Unautorized",
                                                                     QHttpServerResponder::StatusCode::Forbidden);
                                      else {
                                          path += QStringLiteral("/%1").arg(url.path());
                                          qDebug() << "File to look at:" << path;
                                          return QHttpServerResponse::fromFile(path);
                                      }
                                  });
            }
        }
        if (listen()) {
            qDebug() << QStringLiteral("WebServer listening on port") << port << QStringLiteral(" ")
                     << relative2Absolute;
            connect(httpServer, SIGNAL(newWebSocketConnection()), this, SLOT(onNewConnection()));
            return true;
        } else {
            reinit();
        }
    }
    return false;
}

void WebServerInfoSender::handleFetcherRequest(QNetworkReply *reply) {
    QPair<QJsonObject, QWebSocket *> reqIdRequester = reply2Req.value(reply);
    QString req = reqIdRequester.first.operator[](QStringLiteral("req")).toString();
    QWebSocket *requester = reqIdRequester.second;
    if (!req.isEmpty() && requester) {
        QNetworkReply::NetworkError error = reply->error();
        QString statusText = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray body = reply->readAll();
        QJsonObject out, init;
        QList<QNetworkReply::RawHeaderPair> rHeaders = reply->rawHeaderPairs();
        QJsonArray headers;
        for (auto p : rHeaders) {
            for (auto line : p.second.split('\n')) {
                QJsonArray arrv;
                arrv.append(p.first.constData());
                arrv.append(line.constData());
                headers.append(arrv);
            }
        }
        QString respType = reqIdRequester.first.operator[](QStringLiteral("responseType")).toString();
        init[QStringLiteral("headers")] = headers;
        init[QStringLiteral("status")] = statusCode;
        init[QStringLiteral("statusText")] = statusText;
        init[QStringLiteral("responseURL")] = reply->url().toString();
        if (respType == QStringLiteral("arraybuffer") || respType == QStringLiteral("blob"))
            out[QStringLiteral("body")] = QJsonValue(body.toBase64().constData());
        else
            out[QStringLiteral("body")] = QJsonValue(body.constData());
        out[QStringLiteral("init")] = init;
        out[QStringLiteral("req")] = req;
        out[QStringLiteral("DBG")] = error;
        QJsonDocument toSend(out);
        requester->sendTextMessage(toSend.toJson());
        reply2Req.remove(reply);
    }
    reply->deleteLater();
}

void WebServerInfoSender::processTextMessage(QString message) {
    /*QWebSocket *pClient = qobject_cast<QWebSocket *>(sender());
    if (pClient) {
        pClient->sendTextMessage(message);
    }*/
    qDebug() << QStringLiteral("Message received:") << message;
    emit onDataReceived(message.toUtf8());
}

void WebServerInfoSender::processFetcherRequest(QString data) {
    processFetcher(qobject_cast<QWebSocket *>(sender()), data.toUtf8());
}

void WebServerInfoSender::processFetcherRawRequest(QByteArray data) {
    processFetcher(qobject_cast<QWebSocket *>(sender()), data);
}

void WebServerInfoSender::processFetcher(QWebSocket *sender, const QByteArray &data) {
    qDebug() << QStringLiteral("Fetch Request Received") << data;
    QJsonDocument jsonResponse = QJsonDocument::fromJson(data);
    if (jsonResponse.isObject()) {
        QJsonObject jsonObject = jsonResponse.object();
        if (jsonObject.contains(QStringLiteral("req")) && jsonObject.contains(QStringLiteral("url"))) {
            QString req = jsonObject[QStringLiteral("req")].toString();
            QString url = jsonObject[QStringLiteral("url")].toString();
            QNetworkRequest request(url);
            QString method = QStringLiteral("GET");
            QJsonValue tmpv;
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
            if ((tmpv = jsonObject.value(QStringLiteral("method"))).isString())
                method = tmpv.toString();
            if ((tmpv = jsonObject.value(QStringLiteral("headers"))).isObject()) {
                QVariantHash headers = tmpv.toObject().toVariantHash();
                QVariantHash::const_iterator i = headers.constBegin();
                while (i != headers.constEnd()) {
                    request.setRawHeader(i.key().toUtf8(), i.value().toString().toUtf8());
                    ++i;
                }
            }
            QNetworkReply *repl;
            if (method.toLower() == QStringLiteral("post")) {
                QByteArray body;
                if ((tmpv = jsonObject.value(QStringLiteral("body"))).isString())
                    body = tmpv.toString().toUtf8();
                repl = fetcher->post(request, body);
            } else {
                repl = fetcher->get(request);
            }
            reply2Req[repl] = QPair<QJsonObject, QWebSocket *>(jsonObject, sender);
        }
    }
}

void WebServerInfoSender::onNewConnection() {
    QWebSocket *pSocket = httpServer->nextPendingWebSocketConnection();
    QUrl requestUrl = pSocket->requestUrl();
    qDebug() << QStringLiteral("WebSocket connection") << requestUrl;
    if (requestUrl.path() == QStringLiteral("/fetcher")) {
        connect(pSocket, SIGNAL(textMessageReceived(QString)), this, SLOT(processFetcherRequest(QString)));
        connect(pSocket, SIGNAL(binaryMessageReceived(QByteArray)), this, SLOT(processFetcherRawRequest(QByteArray)));
    } else {
        connect(pSocket, SIGNAL(textMessageReceived(QString)), this, SLOT(processTextMessage(QString)));
        connect(pSocket, SIGNAL(binaryMessageReceived(QByteArray)), this, SLOT(processBinaryMessage(QByteArray)));
        sendToClients << pSocket;
    }
    connect(pSocket, SIGNAL(disconnected()), this, SLOT(socketDisconnected()));

    clients << pSocket;
}

void WebServerInfoSender::socketDisconnected() {
    QWebSocket *pClient = qobject_cast<QWebSocket *>(sender());
    qDebug() << QStringLiteral("socketDisconnected:") << pClient;
    if (pClient) {
        clients.removeAll(pClient);
        if (!sendToClients.removeAll(pClient)) {
            QMutableHashIterator<QNetworkReply *, QPair<QJsonObject, QWebSocket *>> i(reply2Req);
            while (i.hasNext()) {
                i.next();
                if (i.value().second == pClient) {
                    i.remove();
                    break;
                }
            }
        }
        pClient->deleteLater();
    }
}

void WebServerInfoSender::processBinaryMessage(QByteArray message) {
    /*QWebSocket *pClient = qobject_cast<QWebSocket *>(sender());
    if (pClient) {
        pClient->sendBinaryMessage(message);
    }*/
    qDebug() << QStringLiteral("Binary Message received:") << message.toHex();
    emit onDataReceived(message);
}
