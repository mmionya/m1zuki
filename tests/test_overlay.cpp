#include "overlay.h"

#include <QImage>
#include <QScreen>
#include <QtTest>

class OverlayTest : public QObject
{
    Q_OBJECT
private slots:
    void errorIndicatorIsSmallAndExpires()
    {
        Overlay overlay;
        overlay.showError(1);
        QVERIFY(overlay.isVisible());
        QCOMPARE(overlay.size(), QSize(24, 24));
        QVERIFY(overlay.windowFlags() & Qt::WindowTransparentForInput);
        QVERIFY(overlay.windowFlags() & Qt::WindowDoesNotAcceptFocus);
        QImage image(overlay.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        overlay.render(&image);
        QVERIFY(qAlpha(image.pixel(12, 12)) > 0);
        QCOMPARE(qAlpha(image.pixel(0, 0)), 0);
        if (qEnvironmentVariableIsSet("M1ZUKI_TEST_ERROR_PREVIEW"))
            QVERIFY(image.save(qEnvironmentVariable("M1ZUKI_TEST_ERROR_PREVIEW")));
        QTRY_VERIFY_WITH_TIMEOUT(!overlay.isVisible(), 2000);
        overlay.showError();
        overlay.clear();
        QVERIFY(!overlay.isVisible());
    }
    void multipleAnswersAreCanonicalAndResizeTheOverlay()
    {
        Overlay overlay;
        overlay.showNumber(3, 0, 32);
        const auto singleSize = overlay.size();
        overlay.showNumbers({4, 1, 3, 1}, 0, 32);
        QVERIFY(overlay.isVisible());
        QCOMPARE(overlay.accessibleName(), QStringLiteral("1, 3, 4"));
        QVERIFY(overlay.width() > singleSize.width());
        QCOMPARE(overlay.height(), singleSize.height());
        QCOMPARE(overlay.font().pixelSize(), 32);
        QVERIFY(overlay.width() >= QFontMetrics(overlay.font()).horizontalAdvance("1, 3, 4") + 8);
        overlay.showNumber(3, 0, 32);
        QCOMPARE(overlay.size(), singleSize);
        QCOMPARE(overlay.accessibleName(), QStringLiteral("3"));
    }

    void answersKeepQuestionOrderAndMarkUnknownQuestions()
    {
        Overlay overlay;
        overlay.showQuestionAnswers({{2}, {}, {3, 1, 3}}, 0, 32);
        QVERIFY(overlay.isVisible());
        QCOMPARE(overlay.accessibleName(), QStringLiteral("Q1: 2 | Q2: ? | Q3: 1, 3"));
        QVERIFY(overlay.width() >= QFontMetrics(overlay.font())
                    .horizontalAdvance(QStringLiteral("Q3: 1, 3")));
    }

    void invalidSetsHidePreviousAnswers()
    {
        Overlay overlay;
        for (const QList<int> invalid : {QList<int>{}, QList<int>{1, 0}, QList<int>{1, -3}, QList<int>{2, 100}}) {
            overlay.showNumbers({1, 3}, 0, 32);
            overlay.showNumbers(invalid, 0, 32);
            QVERIFY(!overlay.isVisible());
            QVERIFY(overlay.accessibleName().isEmpty());
        }
    }

    void longSetsFitTheScreenWithoutLosingOptions()
    {
        Overlay overlay;
        QList<int> numbers;
        QStringList labels;
        for (int number = 1; number <= 99; ++number) {
            numbers.append(number);
            labels.append(QString::number(number));
        }
        overlay.showNumbers(numbers, 0, 96);
        QVERIFY(overlay.isVisible());
        QCOMPARE(overlay.accessibleName(), labels.join(", "));
        const QSize screen = QGuiApplication::primaryScreen()->geometry().size();
        QVERIFY(overlay.width() <= screen.width() - 24);
        QVERIFY(overlay.height() <= screen.height() - 24);
        QVERIFY(overlay.font().pixelSize() >= 12);
        QVERIFY(overlay.height() > QFontMetrics(overlay.font()).height() + 8);
        QImage rendered(overlay.size(), QImage::Format_ARGB32_Premultiplied);
        rendered.fill(Qt::transparent);
        overlay.render(&rendered);
        for (int x = 0; x < rendered.width(); ++x) {
            QCOMPARE(rendered.pixelColor(x, 0).alpha(), 0);
            QCOMPARE(rendered.pixelColor(x, rendered.height() - 1).alpha(), 0);
        }
        for (int y = 0; y < rendered.height(); ++y) {
            QCOMPARE(rendered.pixelColor(0, y).alpha(), 0);
            QCOMPARE(rendered.pixelColor(rendered.width() - 1, y).alpha(), 0);
        }
    }

    void numberIsVisibleWithTransparentBackgroundAndNoInput()
    {
        Overlay overlay;
        QVERIFY(!overlay.isVisible());
        overlay.showNumber(3, 0, 32);
        QVERIFY(overlay.isVisible());
        QCOMPARE(overlay.font().pixelSize(), 32);
        QCOMPARE(overlay.pos(), QGuiApplication::primaryScreen()->geometry().topLeft() + QPoint(12, 12));
        QVERIFY(overlay.windowFlags().testFlag(Qt::WindowTransparentForInput));
        QVERIFY(overlay.windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus));
        QCOMPARE(overlay.focusPolicy(), Qt::NoFocus);
        QImage rendered(overlay.size(), QImage::Format_ARGB32_Premultiplied);
        rendered.fill(Qt::transparent);
        overlay.render(&rendered);
        QCOMPARE(rendered.pixelColor(0, 0).alpha(), 0);
        QCOMPARE(rendered.pixelColor(rendered.width() - 1, rendered.height() - 1).alpha(), 0);
        int visiblePixels = 0;
        for (int y = 0; y < rendered.height(); ++y)
            for (int x = 0; x < rendered.width(); ++x)
                if (rendered.pixelColor(x, y).alpha())
                    ++visiblePixels;
        QVERIFY(visiblePixels > 20);
        QVERIFY(visiblePixels < rendered.width() * rendered.height() / 2);
        overlay.clear();
        QVERIFY(!overlay.isVisible());
    }

    void changingNumberAndFontResizesWithoutClipping()
    {
        Overlay overlay;
        overlay.showNumber(1, 0, 12);
        QCOMPARE(overlay.font().pixelSize(), 12);
        const auto smallSize = overlay.size();
        overlay.showNumber(99, 0, 96);
        QCOMPARE(overlay.font().pixelSize(), 96);
        QVERIFY(overlay.width() > smallSize.width());
        QVERIFY(overlay.height() > smallSize.height());
        const QFontMetrics metrics(overlay.font());
        QVERIFY(overlay.width() >= metrics.horizontalAdvance(QStringLiteral("99")));
        QVERIFY(overlay.height() >= metrics.height());
        overlay.showNumber(1, 0, -1);
        QCOMPARE(overlay.font().pixelSize(), 12);
        overlay.showNumber(1, 0, 500);
        QCOMPARE(overlay.font().pixelSize(), 96);
    }

    void invalidNumberHidesPreviousAnswer()
    {
        Overlay overlay;
        overlay.showNumber(7, 0, 32);
        QVERIFY(overlay.isVisible());
        overlay.showNumber(0, 0, 32);
        QVERIFY(!overlay.isVisible());
    }

    void hidesAfterRequestedTime()
    {
        Overlay overlay;
        overlay.showNumbers({1, 3, 4}, 1, 32);
        QVERIFY(overlay.isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(!overlay.isVisible(), 1800);
    }

    void replacingAnswerRestartsExpiry()
    {
        Overlay overlay;
        overlay.showNumber(1, 1, 32);
        QTest::qWait(600);
        overlay.showNumber(2, 1, 32);
        QTest::qWait(500);
        QVERIFY(overlay.isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(!overlay.isVisible(), 1200);
    }
};

QTEST_MAIN(OverlayTest)
#include "test_overlay.moc"
