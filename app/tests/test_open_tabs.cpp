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

TEST_CASE("open tabs: draft entries round trip, keep order, the active one marked") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString file = dir.filePath("open-tabs");
  const QStringList entries = {"/a/x.md", "draft:u-aaaaaaaaaaaaaaaa", "/a/y.md", "draft:p-bbbbbbbbbbbbbbbb"};
  REQUIRE(write_open_tabs(file, entries, "draft:u-aaaaaaaaaaaaaaaa"));
  const OpenTabsRead read = read_open_tabs(file);
  CHECK(read.paths == entries);
  CHECK(read.active == "draft:u-aaaaaaaaaaaaaaaa");
}

TEST_CASE("open tabs: a bad or repeated draft id is dropped") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString file = dir.filePath("open-tabs");
  QFile f(file);
  REQUIRE(f.open(QIODevice::WriteOnly));
  f.write(QByteArray("/a/x.md\ndraft:\ndraft:BAD\ndraft:u-aaaaaaaaaaaaaaaa\ndraft:u-aaaaaaaaaaaaaaaa\n"));
  f.close();
  const OpenTabsRead read = read_open_tabs(file);
  CHECK(read.paths == QStringList{"/a/x.md", "draft:u-aaaaaaaaaaaaaaaa"});
}

TEST_CASE("open tabs: draft_entry_id") {
  CHECK(draft_entry_id("draft:u-aaaaaaaaaaaaaaaa") == "u-aaaaaaaaaaaaaaaa");
  CHECK(draft_entry_id("draft:BAD").isEmpty());
  CHECK(draft_entry_id("draft:").isEmpty());
  CHECK(draft_entry_id("/a/x.md").isEmpty());
}
