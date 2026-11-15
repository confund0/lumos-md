#include <catch2/catch_test_macros.hpp>

#include <QDir>

#include "link_open.hpp"

namespace {

lumos::LinkTarget target(const char* url) {
  lumos::LinkTarget t;
  t.url = url;
  t.form = lumos::url_form(t.url);
  return t;
}

}  

TEST_CASE("link_url: a relative path is relative to the note", "[link_open]") {
  const QUrl u = link_url(target("20261012-a3f9/photo%20one.jpg"), QStringLiteral("/n/notes/20261012-a3f9.md"));
  CHECK(u.toLocalFile() == QStringLiteral("/n/notes/20261012-a3f9/photo one.jpg"));
  CHECK(link_url(target("../b.md#part"), QStringLiteral("/n/notes/a.md")).toLocalFile() == QStringLiteral("/n/b.md"));
}

TEST_CASE("link_url: an unsaved note uses the current directory", "[link_open]") {
  CHECK(link_url(target("x.png"), QString()).toLocalFile() == QDir::current().absoluteFilePath(QStringLiteral("x.png")));
}

TEST_CASE("link_url: absolute paths, file and scheme URLs", "[link_open]") {
  CHECK(link_url(target("/srv/a%20b.pdf"), QString()).toLocalFile() == QStringLiteral("/srv/a b.pdf"));
  CHECK(link_url(target("file:///srv/a.pdf"), QString()).toLocalFile() == QStringLiteral("/srv/a.pdf"));
  CHECK(link_url(target("https://example.org/x?y=1"), QString()) == QUrl(QStringLiteral("https://example.org/x?y=1")));
  CHECK(link_url(target("mailto:a@b.org"), QString()).scheme() == QStringLiteral("mailto"));
}

TEST_CASE("link_url: nothing to open", "[link_open]") {
  CHECK(link_url(target("#part"), QStringLiteral("/n/a.md")).isEmpty());
  CHECK(link_url(target(""), QStringLiteral("/n/a.md")).isEmpty());
  CHECK(link_url(target("//host/x"), QStringLiteral("/n/a.md")).isEmpty());
}
