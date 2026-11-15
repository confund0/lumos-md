#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QTemporaryDir>

#include "open_tabs.hpp"

TEST_CASE("open tabs: write then read gives the same paths and active") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString file = dir.filePath("open-tabs");
  const QStringList paths = {"/a/x.md", "/a/y.md", "/a/z.md"};
  REQUIRE(write_open_tabs(file, paths, "/a/y.md"));
  const OpenTabsRead read = read_open_tabs(file);
  CHECK(read.paths == paths);
  CHECK(read.active == "/a/y.md");
}

TEST_CASE("open tabs: a missing file gives nothing") {
  const OpenTabsRead read = read_open_tabs("/does/not/exist/open-tabs");
  CHECK(read.paths.isEmpty());
  CHECK(read.active.isEmpty());
}

TEST_CASE("open tabs: an empty, relative, control or repeated line is left out") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString file = dir.filePath("open-tabs");
  QFile f(file);
  REQUIRE(f.open(QIODevice::WriteOnly));
  f.write(QByteArray("/a/x.md\n\nrel.md\n/a/\x01y.md\n/a/x.md\n/a/z.md\n"));
  f.close();
  const OpenTabsRead read = read_open_tabs(file);
  CHECK(read.paths == QStringList{"/a/x.md", "/a/z.md"});
  CHECK(read.active.isEmpty());
}

TEST_CASE("open tabs: the first of two lines marked active wins") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString file = dir.filePath("open-tabs");
  QFile f(file);
  REQUIRE(f.open(QIODevice::WriteOnly));
  f.write(QByteArray("*/a/x.md\n*/a/y.md\n"));
  f.close();
  const OpenTabsRead read = read_open_tabs(file);
  CHECK(read.paths == QStringList{"/a/x.md", "/a/y.md"});
  CHECK(read.active == "/a/x.md");
}
