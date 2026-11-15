#include "link_open.hpp"

#include <QDir>
#include <QFileInfo>

QUrl link_url(const lumos::LinkTarget& target, const QString& note_path) {
  const QString url = QString::fromStdString(target.url);
  switch (target.form) {
    case lumos::UrlForm::relative_path: {
      const QDir dir = note_path.isEmpty() ? QDir::current() : QFileInfo(note_path).absoluteDir();
      return QUrl::fromLocalFile(dir.absolutePath() + QLatin1Char('/')).resolved(QUrl(url));
    }
    case lumos::UrlForm::absolute_path:
      return QUrl::fromLocalFile(QUrl::fromPercentEncoding(url.toUtf8()));
    case lumos::UrlForm::file_url:
    case lumos::UrlForm::scheme_url: {
      const QUrl u(url);
      return u.isValid() && !u.scheme().isEmpty() ? u : QUrl();
    }
    case lumos::UrlForm::other:
      break;
  }
  return {};
}
