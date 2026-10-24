#include <catch2/catch_test_macros.hpp>

#include <QDir>

#include "recent_files.hpp"

TEST_CASE("recent files: a path goes to the front once") {
  QStringList list;
  list = add_recent_file(list, "/a/x.md");
  list = add_recent_file(list, "/a/y.md");
  CHECK(list == QStringList{"/a/y.md", "/a/x.md"});
  list = add_recent_file(list, "/a/./b/../x.md");
  CHECK(list == QStringList{"/a/x.md", "/a/y.md"});
}

TEST_CASE("recent files: a relative path is made absolute") {
  const QStringList list = add_recent_file({}, "x.md");
  CHECK(list == QStringList{QDir::cleanPath(QDir::current().absoluteFilePath("x.md"))});
}

TEST_CASE("recent files: the list keeps the newest entries") {
  QStringList list;
  for (int i = 0; i < 12; ++i) list = add_recent_file(list, QStringLiteral("/f%1").arg(i));
  REQUIRE(list.size() == kRecentFilesMax);
  CHECK(list.first() == "/f11");
  CHECK(list.last() == "/f2");
  CHECK(add_recent_file({"/a", "/b", "/c"}, "/d", 2) == QStringList{"/d", "/a"});
}

TEST_CASE("recent files: remove drops the path") {
  CHECK(remove_recent_file({"/a", "/b"}, "/a") == QStringList{"/b"});
  CHECK(remove_recent_file({"/a", "/b"}, "/c") == QStringList{"/a", "/b"});
}

TEST_CASE("recent files: label") {
  CHECK(recent_file_label(1, "/home/u/n.md", "/home/u") == "&1 ~/n.md");
  CHECK(recent_file_label(2, "/home/user/n.md", "/home/u") == "&2 /home/user/n.md");
  CHECK(recent_file_label(3, "/a/R&D.md", "/home/u") == "&3 /a/R&&D.md");
  CHECK(recent_file_label(10, "/a.md", "") == "10 /a.md");
}
