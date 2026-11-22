#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "drafts.hpp"
#include "file_watch.hpp"

namespace {

QString write_file(const QDir& dir, const QString& name, const QByteArray& bytes) {
  const QString path = dir.filePath(name);
  QFile f(path);
  REQUIRE(f.open(QIODevice::WriteOnly));
  f.write(bytes);
  f.close();
  return path;
}

}  

TEST_CASE("check_disk: unchanged bytes give same") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QByteArray bytes = "# A\n";
  const QString path = write_file(QDir(dir.path()), QStringLiteral("a.md"), bytes);
  const DiskCheck result = check_disk(path, sha256_hex(bytes), QByteArray());
  CHECK(result.state == DiskState::same);
  CHECK(result.sha256 == sha256_hex(bytes));
}

TEST_CASE("check_disk: a rewrite of the same bytes (touch) gives same") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QByteArray bytes = "# A\n";
  const QString path = write_file(QDir(dir.path()), QStringLiteral("a.md"), bytes);
  write_file(QDir(dir.path()), QStringLiteral("a.md"), bytes);  
  const DiskCheck result = check_disk(path, sha256_hex(bytes), QByteArray());
  CHECK(result.state == DiskState::same);
}

TEST_CASE("check_disk: other bytes give changed") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString path = write_file(QDir(dir.path()), QStringLiteral("a.md"), "# A\n");
  write_file(QDir(dir.path()), QStringLiteral("a.md"), "# B\n");
  const DiskCheck result = check_disk(path, sha256_hex(QByteArray("# A\n")), QByteArray());
  CHECK(result.state == DiskState::changed);
  CHECK(result.sha256 == sha256_hex(QByteArray("# B\n")));
}

TEST_CASE("check_disk: bytes equal to kept_sha256 give same") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QByteArray kept_bytes = "# Kept\n";
  const QString path = write_file(QDir(dir.path()), QStringLiteral("a.md"), kept_bytes);
  const DiskCheck result = check_disk(path, sha256_hex(QByteArray("# Saved\n")), sha256_hex(kept_bytes));
  CHECK(result.state == DiskState::same);
}

TEST_CASE("check_disk: a missing file gives gone") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString path = QDir(dir.path()).filePath(QStringLiteral("gone.md"));
  const DiskCheck result = check_disk(path, sha256_hex(QByteArray("# A\n")), QByteArray());
  CHECK(result.state == DiskState::gone);
  CHECK(result.sha256.isEmpty());
}
