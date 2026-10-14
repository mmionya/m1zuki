#include "openrouter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <cstring>

namespace {
QByteArray completion(const QJsonValue &content, const QString &reason = QStringLiteral("stop"))
{
    return QJsonDocument(QJsonObject{
        {QStringLiteral("choices"), QJsonArray{QJsonObject{
            {QStringLiteral("finish_reason"), reason},
            {QStringLiteral("message"), QJsonObject{{QStringLiteral("content"), content}}}
        }}}
    }).toJson(QJsonDocument::Compact);
}

class FakeReply final : public QNetworkReply {
public:
    FakeReply(const QNetworkRequest &request, const QByteArray &body, int status,
              NetworkError error, bool complete, QObject *parent)
        : QNetworkReply(parent), m_body(body)
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::PostOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        if (error != NoError)
            setError(error, QStringLiteral("secret server error: sk-test-never-echo"));
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        if (complete) {
            QTimer::singleShot(0, this, [this] {
                if (isFinished())
                    return;
                emit readyRead();
                if (isFinished())
                    return;
                setFinished(true);
                emit finished();
            });
        }
    }

    void abort() override
    {
        if (isFinished())
            return;
        setError(OperationCanceledError, QStringLiteral("cancelled"));
        setFinished(true);
        emit finished();
    }

    qint64 bytesAvailable() const override
    {
        return m_body.size() - m_offset + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const qint64 count = qMin(maxSize, static_cast<qint64>(m_body.size() - m_offset));
        if (count <= 0)
            return -1;
        std::memcpy(data, m_body.constData() + m_offset, static_cast<size_t>(count));
        m_offset += count;
        return count;
    }

private:
    QByteArray m_body;
    qint64 m_offset = 0;
};

class FakeNetwork final : public QNetworkAccessManager {
public:
    QNetworkRequest lastRequest;
    QByteArray lastBody;
    QByteArray responseBody = completion(QStringLiteral("3"));
    int status = 200;
    int requests = 0;
    QNetworkReply::NetworkError responseError = QNetworkReply::NoError;
    bool autoComplete = true;

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoingData) override
    {
        Q_ASSERT(operation == PostOperation);
        ++requests;
        lastRequest = request;
        lastBody = outgoingData ? outgoingData->readAll() : QByteArray{};
        return new FakeReply(request, responseBody, status, responseError, autoComplete, this);
    }
};
} // namespace

class OpenRouterTest final : public QObject {
    Q_OBJECT

private slots:
    void answer_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QList<int>>("expected");
        QTest::newRow("simple") << QStringLiteral("2") << QList<int>{2};
        QTest::newRow("whitespace") << QStringLiteral(" \n 99\t") << QList<int>{99};
        QTest::newRow("json-legacy") << QStringLiteral("{\"answer\": 4}") << QList<int>{4};
        QTest::newRow("multiline-json") << QStringLiteral("{\n \"answer\" : 8\n}") << QList<int>{8};
        QTest::newRow("fence") << QStringLiteral("```\n3\n```") << QList<int>{3};
        QTest::newRow("fence-crlf") << QStringLiteral("```text\r\n5\r\n```") << QList<int>{5};
        QTest::newRow("fenced-json-legacy") << QStringLiteral("```json\n{\"answer\": 6}\n```") << QList<int>{6};
        QTest::newRow("multiple") << QStringLiteral("1, 3, 4") << QList<int>{1, 3, 4};
        QTest::newRow("multiple-no-spaces") << QStringLiteral("2,3") << QList<int>{2, 3};
        QTest::newRow("sort-deduplicate") << QStringLiteral("4,1,3,1,4") << QList<int>{1, 3, 4};
        QTest::newRow("array") << QStringLiteral("[1, 3, 4]") << QList<int>{1, 3, 4};
        QTest::newRow("array-single") << QStringLiteral("[2]") << QList<int>{2};
        QTest::newRow("array-whitespace") << QStringLiteral("[ \n 99, 1,\n 8 ]") << QList<int>{1, 8, 99};
        QTest::newRow("json-multiple") << QStringLiteral("{\"answers\":[1,3,4]}") << QList<int>{1, 3, 4};
        QTest::newRow("json-deduplicate") << QStringLiteral("{\"answers\":[3,1,3]}") << QList<int>{1, 3};
        QTest::newRow("fenced-multiple") << QStringLiteral("```text\n3, 1\n```") << QList<int>{1, 3};
        QTest::newRow("fenced-array") << QStringLiteral("```json\n[1, 4]\n```") << QList<int>{1, 4};
        QTest::newRow("fenced-json") << QStringLiteral("```json\n{\"answers\":[4,2]}\n```") << QList<int>{2, 4};
        const QList<int> failure;
        QTest::newRow("zero") << QStringLiteral("0") << failure;
        QTest::newRow("json-zero") << QStringLiteral("{\"answer\":0}") << failure;
        QTest::newRow("empty-array") << QStringLiteral("[]") << failure;
        QTest::newRow("empty-json-array") << QStringLiteral("{\"answers\": [ ]}") << failure;
        QTest::newRow("zero-array") << QStringLiteral("[0]") << failure;
        QTest::newRow("zero-mixed") << QStringLiteral("0, 3") << failure;
        QTest::newRow("zero-mixed-array") << QStringLiteral("[1,0]") << failure;
        QTest::newRow("zero-mixed-json") << QStringLiteral("{\"answers\":[0,2]}") << failure;
        QTest::newRow("empty") << QString{} << failure;
        QTest::newRow("prose") << QStringLiteral("Ответ: 2") << failure;
        QTest::newRow("explanation") << QStringLiteral("2 because it is correct") << failure;
        QTest::newRow("prose-multiple") << QStringLiteral("Ответы: 1, 3") << failure;
        QTest::newRow("lines") << QStringLiteral("2\n3") << failure;
        QTest::newRow("negative") << QStringLiteral("-1") << failure;
        QTest::newRow("large") << QStringLiteral("100") << failure;
        QTest::newRow("decimal") << QStringLiteral("2.0") << failure;
        QTest::newRow("leading-zero") << QStringLiteral("02") << failure;
        QTest::newRow("label") << QStringLiteral("B") << failure;
        QTest::newRow("labels-list") << QStringLiteral("А, В") << failure;
        QTest::newRow("range") << QStringLiteral("1-3") << failure;
        QTest::newRow("range-list") << QStringLiteral("1, 3-4") << failure;
        QTest::newRow("alternative") << QStringLiteral("1 or 3") << failure;
        QTest::newRow("alternative-russian") << QStringLiteral("1 или 3") << failure;
        QTest::newRow("semicolon") << QStringLiteral("1;3") << failure;
        QTest::newRow("trailing-comma") << QStringLiteral("1,3,") << failure;
        QTest::newRow("double-comma") << QStringLiteral("1,,3") << failure;
        QTest::newRow("extra-key") << QStringLiteral("{\"answer\":2,\"reason\":\"x\"}") << failure;
        QTest::newRow("duplicate-key") << QStringLiteral("{\"answer\":2,\"answer\":3}") << failure;
        QTest::newRow("duplicate-list-key") << QStringLiteral("{\"answers\":[2],\"answers\":[3]}") << failure;
        QTest::newRow("both-json-forms") << QStringLiteral("{\"answers\":[2],\"answer\":3}") << failure;
        QTest::newRow("string-value") << QStringLiteral("{\"answer\":\"2\"}") << failure;
        QTest::newRow("float-value") << QStringLiteral("{\"answer\":2.0}") << failure;
        QTest::newRow("string-array") << QStringLiteral("[1,\"3\"]") << failure;
        QTest::newRow("float-array") << QStringLiteral("[1,3.0]") << failure;
        QTest::newRow("exponent-array") << QStringLiteral("[1,3e0]") << failure;
        QTest::newRow("bool-array") << QStringLiteral("[1,true]") << failure;
        QTest::newRow("null-array") << QStringLiteral("[1,null]") << failure;
        QTest::newRow("nested-array") << QStringLiteral("[1,[3]]") << failure;
        QTest::newRow("negative-array") << QStringLiteral("[1,-3]") << failure;
        QTest::newRow("large-array") << QStringLiteral("[1,100]") << failure;
        QTest::newRow("trailing-array-comma") << QStringLiteral("[1,3,]") << failure;
        QTest::newRow("json-wrong-type") << QStringLiteral("{\"answers\":3}") << failure;
        QTest::newRow("json-legacy-wrong-type") << QStringLiteral("{\"answer\":[3]}") << failure;
        QTest::newRow("trailing-json") << QStringLiteral("{\"answer\":2} {}") << failure;
        QTest::newRow("trailing-array") << QStringLiteral("[1,3] []") << failure;
        QTest::newRow("fenced-prose") << QStringLiteral("```\nAnswer 2\n```") << failure;
        QTest::newRow("trailing-prose") << QStringLiteral("```\n2\n```\nExplanation") << failure;
    }

    void answer()
    {
        QFETCH(QString, input);
        QFETCH(QList<int>, expected);
        const auto result = parseAnswer(input);
        QCOMPARE(result.numbers, expected);
        QCOMPARE(result.error.isEmpty(), !expected.isEmpty());
    }

    void requestAndAnswer()
    {
        FakeNetwork network;
        OpenRouterClient client(&network, nullptr);
        QSignalSpy answered(&client, &OpenRouterClient::answered);
        QSignalSpy failed(&client, &OpenRouterClient::failed);
        const QByteArray image("private-screen-bytes");
        client.solve(image, QStringLiteral("sk-test-never-echo"), QStringLiteral("test/vision"));
        QVERIFY(client.isBusy());
        QTRY_COMPARE(answered.count(), 1);
        QCOMPARE(qvariant_cast<QList<int>>(answered.first().first()), QList<int>{3});
        QCOMPARE(failed.count(), 0);
        QVERIFY(!client.isBusy());
        QCOMPARE(network.requests, 1);
        QCOMPARE(network.lastRequest.url(), QUrl(QStringLiteral("https://openrouter.ai/api/v1/chat/completions")));
        QCOMPARE(network.lastRequest.rawHeader("Authorization"), QByteArray("Bearer sk-test-never-echo"));
        QCOMPARE(network.lastRequest.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
                 static_cast<int>(QNetworkRequest::ManualRedirectPolicy));
        QCOMPARE(network.lastRequest.transferTimeout(), 60000);
        const auto payload = QJsonDocument::fromJson(network.lastBody).object();
        QCOMPARE(payload.value(QStringLiteral("model")).toString(), QStringLiteral("test/vision"));
        QVERIFY(payload.value(QStringLiteral("max_tokens")).toInt() >= 256);
        QVERIFY(!payload.value(QStringLiteral("stream")).toBool());
        QVERIFY(!payload.contains(QStringLiteral("response_format")));
        const auto messages = payload.value(QStringLiteral("messages")).toArray();
        QCOMPARE(messages.size(), 2);
        const auto parts = messages.at(1).toObject().value(QStringLiteral("content")).toArray();
        QCOMPARE(parts.at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("text"));
        const auto imageUrl = parts.at(1).toObject().value(QStringLiteral("image_url")).toObject();
        QCOMPARE(imageUrl.value(QStringLiteral("url")).toString(),
                 QStringLiteral("data:image/png;base64,") + QString::fromLatin1(image.toBase64()));
    }

    void rejectedResponses_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<QString>("expectedFragment");
        const QByteArray secret("sk-test-never-echo private-screen-bytes");
        QTest::newRow("unauthorized") << 401 << secret << QStringLiteral("API-ключ");
        QTest::newRow("balance") << 402 << secret << QStringLiteral("средств");
        QTest::newRow("rate-limit") << 429 << secret << QStringLiteral("много запросов");
        QTest::newRow("redirect") << 307 << secret << QStringLiteral("перенаправление");
        QTest::newRow("provider-error") << 200
            << QByteArray("{\"error\":{\"code\":429,\"message\":\"sk-test-never-echo private-screen-bytes\"}}")
            << QStringLiteral("много запросов");
        QTest::newRow("string-error-code") << 200
            << QByteArray("{\"error\":{\"code\":\"402\"}}") << QStringLiteral("средств");
        QTest::newRow("truncated") << 200 << completion(QStringLiteral("2"), QStringLiteral("length"))
            << QStringLiteral("оборвался");
        QTest::newRow("tool-call") << 200 << completion(QStringLiteral("2"), QStringLiteral("tool_calls"))
            << QStringLiteral("не завершила");
        QTest::newRow("unknown-finish") << 200 << completion(QStringLiteral("2"), QString{})
            << QStringLiteral("не завершила");
        QTest::newRow("malformed-json") << 200 << secret << QStringLiteral("некорректный");
        QTest::newRow("missing-choice") << 200 << QByteArray("{\"choices\":[]}")
            << QStringLiteral("единственный");
        QTest::newRow("missing-content") << 200 << completion(QJsonValue())
            << QStringLiteral("не вернула номер");
        QTest::newRow("zero-answer") << 200 << completion(QStringLiteral("0"))
            << QStringLiteral("Не удалось определить");
        QTest::newRow("empty-answer-set") << 200 << completion(QStringLiteral("[]"))
            << QStringLiteral("Не удалось определить");
        QTest::newRow("mixed-zero-answer-set") << 200 << completion(QStringLiteral("[1,0,3]"))
            << QStringLiteral("однозначный");
        QTest::newRow("alternative-answers") << 200 << completion(QStringLiteral("1 or 3"))
            << QStringLiteral("однозначный");
        QTest::newRow("prose-answer") << 200 << completion(QStringLiteral("sk-test-never-echo private-screen-bytes: 2"))
            << QStringLiteral("однозначный");
        QTest::newRow("too-large") << 200 << QByteArray(1024 * 1024 + 1, 'x')
            << QStringLiteral("слишком большой");
    }

    void rejectedResponses()
    {
        QFETCH(int, status);
        QFETCH(QByteArray, body);
        QFETCH(QString, expectedFragment);
        FakeNetwork network;
        network.status = status;
        network.responseBody = body;
        OpenRouterClient client(&network, nullptr);
        QSignalSpy answered(&client, &OpenRouterClient::answered);
        QSignalSpy failed(&client, &OpenRouterClient::failed);
        client.solve("private-screen-bytes", QStringLiteral("sk-test-never-echo"), QStringLiteral("test/vision"));
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(answered.count(), 0);
        QVERIFY(!client.isBusy());
        const auto error = failed.first().first().toString();
        QVERIFY2(error.contains(expectedFragment), qPrintable(error));
        QVERIFY(!error.contains(QStringLiteral("sk-test-never-echo")));
        QVERIFY(!error.contains(QStringLiteral("private-screen-bytes")));
    }

    void typedTextContent()
    {
        FakeNetwork network;
        network.responseBody = completion(QJsonArray{QJsonObject{
            {QStringLiteral("type"), QStringLiteral("text")},
            {QStringLiteral("text"), QStringLiteral("4")}
        }});
        OpenRouterClient client(&network, nullptr);
        QSignalSpy answered(&client, &OpenRouterClient::answered);
        client.solve("image", QStringLiteral("key"), QStringLiteral("model"));
        QTRY_COMPARE(answered.count(), 1);
        QCOMPARE(qvariant_cast<QList<int>>(answered.first().first()), QList<int>{4});
    }

    void multipleAnswersThroughClient_data()
    {
        QTest::addColumn<QString>("response");
        QTest::newRow("csv") << QStringLiteral("4, 1, 3, 1");
        QTest::newRow("array") << QStringLiteral("[4,1,3,1]");
        QTest::newRow("json") << QStringLiteral("{\"answers\":[4,1,3,1]}");
        QTest::newRow("fenced-json") << QStringLiteral("```json\n{\"answers\":[4,1,3,1]}\n```");
    }

    void multipleAnswersThroughClient()
    {
        QFETCH(QString, response);
        FakeNetwork network;
        network.responseBody = completion(response);
        OpenRouterClient client(&network, nullptr);
        QSignalSpy answered(&client, &OpenRouterClient::answered);
        QSignalSpy failed(&client, &OpenRouterClient::failed);
        client.solve("image", QStringLiteral("key"), QStringLiteral("model"));
        QTRY_COMPARE(answered.count(), 1);
        QCOMPARE(qvariant_cast<QList<int>>(answered.first().first()), (QList<int>{1, 3, 4}));
        QCOMPARE(failed.count(), 0);
        QVERIFY(!client.isBusy());
    }

    void networkErrorIsRedacted()
    {
        FakeNetwork network;
        network.status = 0;
        network.responseError = QNetworkReply::HostNotFoundError;
        OpenRouterClient client(&network, nullptr);
        QSignalSpy failed(&client, &OpenRouterClient::failed);
        client.solve("image", QStringLiteral("key"), QStringLiteral("model"));
        QTRY_COMPARE(failed.count(), 1);
        const auto error = failed.first().first().toString();
        QVERIFY(error.contains(QStringLiteral("интернету")));
        QVERIFY(!error.contains(QStringLiteral("sk-test-never-echo")));
    }

    void invalidSettingsNeverSend()
    {
        FakeNetwork network;
        OpenRouterClient client(&network, nullptr);
        QSignalSpy failed(&client, &OpenRouterClient::failed);
        client.solve("image", QString{}, QStringLiteral("model"));
        client.solve("image", QStringLiteral("key\nInjected: header"), QStringLiteral("model"));
        client.solve("image", QStringLiteral("key"), QString{});
        client.solve({}, QStringLiteral("key"), QStringLiteral("model"));
        QCOMPARE(failed.count(), 4);
        QCOMPARE(network.requests, 0);
        QVERIFY(!client.isBusy());
    }

    void cancelAndRestart()
    {
        FakeNetwork network;
        network.autoComplete = false;
        OpenRouterClient client(&network, nullptr);
        QSignalSpy answered(&client, &OpenRouterClient::answered);
        QSignalSpy failed(&client, &OpenRouterClient::failed);
        client.solve("image", QStringLiteral("key"), QStringLiteral("model"));
        QVERIFY(client.isBusy());
        client.solve("image", QStringLiteral("key"), QStringLiteral("model"));
        QCOMPARE(network.requests, 1);
        QCOMPARE(failed.count(), 1);
        failed.clear();
        client.cancel();
        QVERIFY(!client.isBusy());
        QCOMPARE(failed.count(), 0);
        QCOMPARE(answered.count(), 0);
        network.autoComplete = true;
        network.responseBody = completion(QStringLiteral("1, 3, 4"));
        client.solve("image", QStringLiteral("key"), QStringLiteral("model"));
        QTRY_COMPARE(answered.count(), 1);
        QCOMPARE(qvariant_cast<QList<int>>(answered.first().first()), (QList<int>{1, 3, 4}));
        QCOMPARE(network.requests, 2);
        QCOMPARE(failed.count(), 0);
    }
};

QTEST_GUILESS_MAIN(OpenRouterTest)
#include "test_openrouter.moc"
