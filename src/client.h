#pragma once

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QMutex>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <QPixmap>

class ClientWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ClientWidget(const QString &hostMain, quint16 portMain,
                          const QString &hostInset, quint16 portInset,
                          QWidget *parent = nullptr);
    ~ClientWidget() override;

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void startPipeline(const QString &host, quint16 port, bool isMain);
    static GstFlowReturn onNewSample(GstAppSink *appsink, gpointer user_data);
    void layoutInset();
    void swapVideos();

    QLabel *mainLabel_;
    QLabel *insetLabel_;
    QLabel *mainTarget_;
    QLabel *insetTarget_;

    GstElement *mainPipeline_;
    GstElement *insetPipeline_;

    QMutex targetMutex_;
};
