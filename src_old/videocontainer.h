// #ifndef WINDOW_H
// #define WINDOW_H
//
// #include <QWidget>
// #include <QImage>
// #include <QFrame>
//
// class VideoContainer : public QWidget
// {
//  public:
//   explicit VideoContainer(QWidget *parent = 0);
//  private:
// };
//
//
// class Image : public QImage
// {
//  public:
//   Image() {
//     widget.setAutoFillBackground(true);
//     widget.setPalette({QColor(0xff, 0x00, 0x00)});
//   }
//  private:
//   QFrame frame;
//   QWidget widget {&frame};
// };
//
//
// #endif // WINDOW_H

#ifndef WINDOW_H
#define WINDOW_H

#include <QWidget>

class QGroupBox;

class Window : public QWidget
{
    Q_OBJECT

public:
    Window(QWidget *parent = 0);

private:
    QGroupBox *createFirstExclusiveGroup();
    QGroupBox *createSecondExclusiveGroup();
    QGroupBox *createNonExclusiveGroup();
    QGroupBox *createPushButtonGroup();
};

#endif
