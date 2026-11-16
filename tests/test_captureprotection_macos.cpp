#include "captureprotection_macos.h"
#include <QSignalSpy>
#include <QtTest>

class MacCaptureCapabilityTest : public QObject {
    Q_OBJECT
private slots:
    void requiresExplicitOptIn()
    {
        CaptureProtection protection;
        QSignalSpy status(&protection, &CaptureProtection::statusChanged);
        QVERIFY(!protection.isReady());
        QVERIFY(!protection.hasFailed());
        protection.start();
        QVERIFY(!protection.isReady());
        QVERIFY(protection.hasFailed());
        QCOMPARE(status.count(), 1);
        QCOMPARE(status.last()[0].toBool(), false);
        protection.setAllowUnprotectedOverlay(true);
        QVERIFY(protection.isReady());
        QVERIFY(!protection.hasFailed());
        QCOMPARE(status.count(), 2);
        QCOMPARE(status.last()[0].toBool(), true);
        protection.setAllowUnprotectedOverlay(true);
        QCOMPARE(status.count(), 2);
        protection.setAllowUnprotectedOverlay(false);
        QVERIFY(!protection.isReady());
        QVERIFY(protection.hasFailed());
        QCOMPARE(status.count(), 3);
    }
    void persistedOptInAppliesAtStart()
    {
        CaptureProtection protection;
        QSignalSpy status(&protection, &CaptureProtection::statusChanged);
        protection.setAllowUnprotectedOverlay(true);
        QCOMPARE(status.count(), 0);
        QVERIFY(!protection.isReady());
        protection.start();
        QVERIFY(protection.isReady());
        QCOMPARE(status.count(), 1);
        protection.start();
        QCOMPARE(status.count(), 1);
    }
};
QTEST_MAIN(MacCaptureCapabilityTest)
#include "test_captureprotection_macos.moc"
