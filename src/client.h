#pragma once

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <QPixmap>
#include <functional>

class ClientWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ClientWidget(const QString &hostMain, quint16 portMain,
                          const QString &hostInset, quint16 portInset,
                          QWidget *parent = nullptr);
    ~ClientWidget();

private:
    void startPipeline(const QString &host, quint16 port, bool isMain);
    static GstFlowReturn onNewSample(GstAppSink *appsink, gpointer user_data);
    void layoutInset();
    void swapVideos();

    QLabel *mainLabel_;
    QLabel *insetLabel_;

    QPixmap mainPixmap_;
    QPixmap insetPixmap_;

    GstElement *mainPipeline_;
    GstElement *insetPipeline_;

    std::function<void()> insetClickHandler_;

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
};
