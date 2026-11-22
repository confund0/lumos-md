#pragma once

#include <QAbstractButton>
#include <QApplication>
#include <QFileDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QObject>
#include <QString>
#include <QTest>
#include <QTimer>

#include <memory>

namespace {

inline void click_dialogs(QList<QMessageBox::StandardButton> buttons) {
  auto queue = std::make_shared<QList<QMessageBox::StandardButton>>(std::move(buttons));
  auto* timer = new QTimer();
  timer->setInterval(10);
  QObject::connect(timer, &QTimer::timeout, [timer, queue] {
    auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
    if (box == nullptr || queue->isEmpty()) return;
    QAbstractButton* button = box->button(queue->takeFirst());
    if (button != nullptr) button->click();
    if (queue->isEmpty()) {
      timer->stop();
      timer->deleteLater();
    }
  });
  timer->start();
}

inline void click_dialog(QMessageBox::StandardButton button) { click_dialogs({button}); }

inline void click_dialog_text(const QString& button_text, QString* text = nullptr, QString* informative = nullptr) {
  auto* timer = new QTimer();
  timer->setInterval(10);
  QObject::connect(timer, &QTimer::timeout, [timer, button_text, text, informative] {
    auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
    if (box == nullptr) return;
    if (text != nullptr) *text = box->text();
    if (informative != nullptr) *informative = box->informativeText();
    for (QAbstractButton* button : box->buttons()) {
      if (button->text() == button_text) {
        button->click();
        break;
      }
    }
    timer->stop();
    timer->deleteLater();
  });
  timer->start();
}

inline QTimer* guard_no_dialog(std::shared_ptr<int> count) {
  auto* timer = new QTimer();
  timer->setInterval(10);
  QObject::connect(timer, &QTimer::timeout, [count] {
    auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
    if (box == nullptr) return;
    ++*count;
    QTest::keyClick(box, Qt::Key_Escape);
  });
  timer->start();
  return timer;
}

inline void answer_file_dialog(const QString& path) {
  auto* timer = new QTimer();
  timer->setInterval(10);
  QObject::connect(timer, &QTimer::timeout, [timer, path] {
    auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
    if (dialog == nullptr) return;
    if (path.isEmpty()) {
      dialog->reject();
    } else {
      auto* name = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
      if (name == nullptr) return;
      name->setText(path);
      QMetaObject::invokeMethod(dialog, "accept");
    }
    if (dialog->isVisible()) return;
    timer->stop();
    timer->deleteLater();
  });
  timer->start();
}

inline void escape_dialog() {
  auto* timer = new QTimer();
  timer->setInterval(10);
  QObject::connect(timer, &QTimer::timeout, [timer] {
    auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
    if (box == nullptr) return;
    QTest::keyClick(box, Qt::Key_Escape);
    timer->stop();
    timer->deleteLater();
  });
  timer->start();
}

}  
