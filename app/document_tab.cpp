
#include "document_tab.hpp"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QMetaObject>
#include <QPointer>
#include <QSaveFile>
#include <QTextCharFormat>
#include <QThreadPool>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>

#include "find_bar.hpp"
#include "link_open.hpp"
#include "lumos/log.hpp"
#include "lumos/qt/source_editor.hpp"
#include "lumos/tools.hpp"

namespace {

constexpr std::string_view kComp = "app";
constexpr int kImageMaxHeight = 320;

QString image_dir(const QString& note_path) {
  return note_path.isEmpty() ? QDir::current().absolutePath() : QFileInfo(note_path).absolutePath();
}

QString absolute_path(const QString& path) { return QDir::cleanPath(QFileInfo(path).absoluteFilePath()); }

}  

DocumentTab::DocumentTab(QWidget* parent) : QWidget(parent) {
  editor_ = new lumos::qt::SourceEditor(this);
  find_bar_ = new FindBar(editor_, this);
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(editor_);
  layout->addWidget(find_bar_);

  editor_->set_image_max_height(kImageMaxHeight);
  connect(editor_, &lumos::qt::SourceEditor::imageRequested, this,
          [this](const std::string& url) { load_image(url); });
  connect(editor_, &lumos::qt::SourceEditor::textChanged, this, [this] { update_marks(); });
  connect(editor_, &lumos::qt::SourceEditor::linkActivated, this, [this](const lumos::LinkTarget& target) {
    const QUrl url = link_url(target, path_);
    const bool ok = !url.isEmpty() && (!url.isLocalFile() || QFileInfo::exists(url.toLocalFile())) &&
                    QDesktopServices::openUrl(url);
    lumos::log(lumos::LogLevel::debug, kComp, "link_open",
               {{"url", url.toString().toStdString()}, {"ok", ok ? "1" : "0"}});
    if (!ok && link_open_failed_) link_open_failed_(QString::fromStdString(target.url));
  });
}

QString DocumentTab::name() const { return path_.isEmpty() ? tr("Untitled") : QFileInfo(path_).fileName(); }

bool DocumentTab::is_blank() const {
  return path_.isEmpty() && editor_->length() == 0 && !editor_->is_modified();
}

bool DocumentTab::load(const QString& path, bool keep_position, QString* error) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    lumos::log(lumos::LogLevel::debug, kComp, "open_failed",
               {{"path", path.toStdString()}, {"reason", f.errorString().toStdString()}});
    if (error != nullptr) *error = f.errorString();
    return false;
  }
  const QByteArray bytes = f.readAll();
  path_ = absolute_path(path);
  editor_->set_text(std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())), keep_position);
  update_marks();
  lumos::log(lumos::LogLevel::debug, kComp, "open",
             {{"path", path.toStdString()}, {"bytes", std::to_string(bytes.size())},
              {"reload", keep_position ? "1" : "0"}});
  return true;
}

bool DocumentTab::write(const QString& path, QString* error) {
  const std::string text = editor_->text();  
  QSaveFile f(path);
  const bool ok = f.open(QIODevice::WriteOnly) &&
                  f.write(text.data(), static_cast<qint64>(text.size())) == static_cast<qint64>(text.size()) &&
                  f.commit();
  if (!ok) {
    lumos::log(lumos::LogLevel::debug, kComp, "save_failed",
               {{"path", path.toStdString()}, {"reason", f.errorString().toStdString()}});
    if (error != nullptr) *error = f.errorString();
    return false;
  }
  editor_->mark_saved();
  lumos::log(lumos::LogLevel::debug, kComp, "save",
             {{"path", path.toStdString()}, {"bytes", std::to_string(text.size())}});
  return true;
}

void DocumentTab::set_path(const QString& path) {
  const bool moved = image_dir(path) != image_dir(path_);
  path_ = absolute_path(path);
  if (moved) editor_->reload_images();
}

void DocumentTab::set_link_open_failed(std::function<void(const QString&)> callback) {
  link_open_failed_ = std::move(callback);
}

void DocumentTab::update_marks() {
  QTextCharFormat f;
  f.setUnderlineStyle(QTextCharFormat::WaveUnderline);
  f.setUnderlineColor(Qt::red);
  editor_->set_marks(1, lumos::out_of_sequence(editor_->document()), f);
}

void DocumentTab::load_image(const std::string& url) {
  lumos::LinkTarget target;
  target.kind = lumos::md::UrlKind::image;
  target.url = url;
  target.form = lumos::url_form(url);
  const QUrl file_url = link_url(target, path_);
  if (!file_url.isLocalFile()) {
    lumos::log(lumos::LogLevel::debug, kComp, "image_load", {{"ok", "0"}, {"reason", "not_local"}});
    editor_->set_image_failed(url);
    return;
  }
  const QString file = file_url.toLocalFile();
  const qreal dpr = devicePixelRatioF();
  const int max_height = static_cast<int>(std::ceil(kImageMaxHeight * dpr));
  QPointer<DocumentTab> self(this);
  const QString note_dir = image_dir(path_);
  QThreadPool::globalInstance()->start([self, note_dir, url, file, dpr, max_height] {
    QElapsedTimer timer;
    timer.start();
    QImageReader reader(file);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (size.isValid() && size.height() > max_height)
      reader.setScaledSize(QSize(std::max(1, size.width() * max_height / size.height()), max_height));
    const bool scaled = reader.scaledSize().isValid();
    QImage image = reader.read();
    if (scaled) image.setDevicePixelRatio(dpr);
    const auto ms = static_cast<std::size_t>(timer.elapsed());
    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        [self, note_dir, url, image, ms] {
          DocumentTab* const tab = self.data();
          if (tab == nullptr || image_dir(tab->path_) != note_dir) return;
          lumos::log(lumos::LogLevel::debug, kComp, "image_load",
                     {{"ok", image.isNull() ? "0" : "1"}, {"ms", std::to_string(ms)}});
          if (image.isNull()) tab->editor_->set_image_failed(url);
          else tab->editor_->set_image(url, image);
        },
        Qt::QueuedConnection);
  });
}
