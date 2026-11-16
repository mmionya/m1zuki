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
    void showQuestionAnswers(const QList<QList<int>> &answers, int seconds, int fontPixels);
    void clear();
    void showError(int seconds = 5);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void positionOverlay();
    bool layoutTextLines();

    QTimer m_hideTimer;
    QList<int> m_numbers;
    QStringList m_sourceLines;
    QStringList m_lines;
    int m_fontPixels = 32;
    bool m_error = false;
};
