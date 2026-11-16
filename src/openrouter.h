#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

class QNetworkAccessManager;
class QNetworkReply;

struct AnswerResult {
    QList<int> numbers;
    QList<QList<int>> questionAnswers;
    QString error;
};

// Each question's choices are kept separate and in screenshot reading order.
// numbers contains the merged choices for callers that only need the old format.
AnswerResult parseAnswer(const QString &text);

class OpenRouterClient final : public QObject {
    Q_OBJECT

public:
    explicit OpenRouterClient(QObject *parent = nullptr);
    // The supplied manager must outlive this client. Useful for offline tests.
    OpenRouterClient(QNetworkAccessManager *network, QObject *parent);
    ~OpenRouterClient() override;

    void solve(const QByteArray &png, const QString &key, const QString &model);
    void cancel();
    bool isBusy() const;

signals:
    void answered(const QList<QList<int>> &questionAnswers);
    void failed(const QString &message);

private:
    void readResponse();
    void finishResponse();

    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timeout;
    QByteArray m_body;
};
