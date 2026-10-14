#pragma once

#include <QTimer>
#include <QWidget>
#include <QList>
#include <QStringList>

class Overlay : public QWidget
{
    Q_OBJECT
public:
    explicit Overlay(QWidget *parent = nullptr);
    void showNumber(int number, int seconds, int fontPixels);
    void showNumbers(const QList<int> &numbers, int seconds, int fontPixels);
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void positionOverlay();
    bool layoutNumbers();

    QTimer m_hideTimer;
    QList<int> m_numbers;
    QStringList m_lines;
    int m_fontPixels = 32;
};
