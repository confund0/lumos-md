#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "app_log.hpp"
#include "lumos/log.hpp"

namespace {

QByteArray read_all(const QString& path) {
  QFile f(path);
  return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void write_bytes(const QString& path, qint64 n) {
  QFile f(path);
  REQUIRE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
  f.write(QByteArray(n, 'x'));
}

}  

TEST_CASE("app log: rotate_log_if_large moves only a file over the size") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString path = dir.filePath("a.log");
  CHECK_FALSE(rotate_log_if_large(path, 10));  
  write_bytes(path, 10);
  CHECK_FALSE(rotate_log_if_large(path, 10));
  write_bytes(path, 11);
  write_bytes(path + ".1", 3);
  CHECK(rotate_log_if_large(path, 10));
  CHECK_FALSE(QFile::exists(path));
  CHECK(read_all(path + ".1").size() == 11);  
}

TEST_CASE("app log: install_log creates the dir and appends lines") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString path = dir.filePath("state/lumos-md/lumos-md.log");
  REQUIRE(install_log({.path = path}));
  lumos::log(lumos::LogLevel::info, "test", "one");
  lumos::log(lumos::LogLevel::debug, "test", "hidden");  
  uninstall_log();
  REQUIRE(install_log({.path = path}));
  lumos::log(lumos::LogLevel::warn, "test", "two", {{"n", "2"}});
  uninstall_log();
  const QList<QByteArray> lines = read_all(path).split('\n');
  REQUIRE(lines.size() == 3);
  CHECK(lines[0].contains("level=info comp=test event=one"));
  CHECK(lines[1].contains("level=warn comp=test event=two n=2"));
  CHECK(lines[2].isEmpty());
}

TEST_CASE("app log: a file over the size moves to .1 at start and while writing") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString path = dir.filePath("lumos-md.log");
  write_bytes(path, 200);
  REQUIRE(install_log({.path = path, .max_bytes = 100}));
  CHECK(read_all(path + ".1").size() == 200);
  CHECK(read_all(path).isEmpty());
  lumos::log(lumos::LogLevel::info, "test", "one");
  lumos::log(lumos::LogLevel::info, "test", "two");
  lumos::log(lumos::LogLevel::info, "test", "three");
  uninstall_log();
  const QByteArray old = read_all(path + ".1");
  CHECK(old.contains("event=one"));
  CHECK(old.contains("event=two"));
  CHECK(read_all(path).split('\n').size() == 2);
  CHECK(read_all(path).contains("event=three"));
}

TEST_CASE("app log: a rotation by another process opens the new file") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString path = dir.filePath("lumos-md.log");
  REQUIRE(install_log({.path = path}));
  lumos::log(lumos::LogLevel::info, "test", "one");
  REQUIRE(QFile::rename(path, path + ".1"));
  write_bytes(path, 0);
  lumos::log(lumos::LogLevel::info, "test", "two");  
  lumos::log(lumos::LogLevel::info, "test", "three");
  uninstall_log();
  CHECK(read_all(path + ".1").contains("event=two"));
  CHECK(read_all(path).contains("event=three"));
  CHECK_FALSE(read_all(path).contains("event=two"));
}

TEST_CASE("app log: a file that cannot be opened gives false") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  REQUIRE(QDir(dir.path()).mkdir("x.log"));
  CHECK_FALSE(install_log({.path = dir.filePath("x.log")}));
  uninstall_log();
}
