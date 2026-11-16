#include <catch2/catch_test_macros.hpp>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#include <vector>

#include "drafts.hpp"
#include "lumos/log.hpp"

namespace {

QByteArray hash_hex(const QByteArray& data) { return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex(); }

bool is_owner_only(const QString& path, bool exe) {
  const QFileDevice::Permissions perms = QFileInfo(path).permissions();
  if (!perms.testFlag(QFileDevice::ReadOwner) || !perms.testFlag(QFileDevice::WriteOwner)) return false;
  if (exe && !perms.testFlag(QFileDevice::ExeOwner)) return false;
  const QFileDevice::Permissions group_other = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
                                                QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
  return !(perms & group_other);
}

void touch(const QString& path) {
  QFile f(path);
  REQUIRE(f.open(QIODevice::WriteOnly));
}

void set_mtime(const QString& path, const QDateTime& when) {
  QFile f(path);
  REQUIRE(f.open(QIODevice::ReadWrite));
  REQUIRE(f.setFileTime(when, QFileDevice::FileModificationTime));
}

}  

TEST_CASE("drafts: round trip of a path draft and an untitled draft") {
  const QByteArray saved = QByteArray(64, 'a');
  const std::vector<Draft> drafts = {
      Draft{.untitled = false, .path = "/a b/caf\xc3\xa9/100%\nsplit.md", .saved_sha256 = saved,
            .text = QByteArray("line1\r\nline2\0end", 16)},
      Draft{.untitled = true, .path = "", .saved_sha256 = "", .text = QByteArray()},
      Draft{.untitled = true, .path = "", .saved_sha256 = "", .text = "no final line feed"},
  };
  for (const Draft& d : drafts) {
    const DraftRead read = decode_draft(encode_draft(d));
    CHECK(read.state == DraftState::ok);
    CHECK(read.draft.untitled == d.untitled);
    if (!d.untitled) {
      CHECK(read.draft.path == d.path);
      CHECK(read.draft.saved_sha256 == d.saved_sha256);
    }
    CHECK(read.draft.text == d.text);
  }
}

TEST_CASE("drafts: exact header bytes of one fixed draft") {
  const Draft d{.untitled = false, .path = "/a/x.md", .saved_sha256 = QByteArray(64, '7'), .text = "hello"};
  const QByteArray expected = "lumos-md-draft 1\n"
                               "kind=path\n"
                               "path=/a/x.md\n"
                               "saved-sha256=" +
                               QByteArray(64, '7') + "\n"
                               "size=5\n"
                               "text-sha256=" +
                               hash_hex("hello") + "\n\nhello";
  CHECK(encode_draft(d) == expected);
}

TEST_CASE("drafts: a body cut short is damaged, header fields kept") {
  const Draft d{.untitled = false, .path = "/a/x.md", .saved_sha256 = QByteArray(64, '7'), .text = "hello world"};
  QByteArray bytes = encode_draft(d);
  bytes.chop(4);  
  const DraftRead read = decode_draft(bytes);
  CHECK(read.state == DraftState::damaged);
  CHECK(read.draft.path == d.path);
  CHECK(read.draft.saved_sha256 == d.saved_sha256);
  CHECK(read.draft.text == "hello w");
}

TEST_CASE("drafts: one flipped body byte is damaged") {
  const Draft d{.untitled = true, .path = "", .saved_sha256 = "", .text = "hello"};
  QByteArray bytes = encode_draft(d);
  bytes[bytes.size() - 1] = 'x';  
  const DraftRead read = decode_draft(bytes);
  CHECK(read.state == DraftState::damaged);
  CHECK(read.draft.text == "hellx");
}

TEST_CASE("drafts: extra bytes after the body is damaged") {
  const Draft d{.untitled = true, .path = "", .saved_sha256 = "", .text = "hello"};
  QByteArray bytes = encode_draft(d);
  bytes += "extra";
  const DraftRead read = decode_draft(bytes);
  CHECK(read.state == DraftState::damaged);
  CHECK(read.draft.text == "helloextra");
}

TEST_CASE("drafts: an unknown header key is skipped") {
  const Draft d{.untitled = true, .path = "", .saved_sha256 = "", .text = "hello"};
  QByteArray bytes = encode_draft(d);
  bytes.replace("\nsize=", "\nextra-key=value\nsize=");
  const DraftRead read = decode_draft(bytes);
  CHECK(read.state == DraftState::ok);
  CHECK(read.draft.text == "hello");
}

TEST_CASE("drafts: unreadable inputs give all bytes back") {
  const Draft base{.untitled = true, .path = "", .saved_sha256 = "", .text = "hello"};
  const QByteArray good = encode_draft(base);

  QByteArray wrong_magic = good;
  wrong_magic.replace("lumos-md-draft 1", "lumos-md-draf 1");

  QByteArray wrong_version = good;
  wrong_version.replace("lumos-md-draft 1", "lumos-md-draft 2");

  QByteArray no_blank_line = good;
  no_blank_line.replace("\n\nhello", "\nhello");  

  QByteArray huge_header = QByteArray("lumos-md-draft 1\n") + QByteArray(70000, 'a') + "=1\n\nhello";

  QByteArray size_missing = good;
  size_missing.replace("size=5\n", "");

  QByteArray bad_hex = good;
  bad_hex.replace(hash_hex("hello"), QByteArray(64, 'g'));

  QByteArray path_without_path = QByteArray("lumos-md-draft 1\nkind=path\nsize=5\ntext-sha256=") + hash_hex("hello") +
                                  "\n\nhello";

  QByteArray no_equals = good;
  no_equals.replace("kind=untitled\n", "kind=untitled\nbadline\n");

  for (const QByteArray& bytes : {wrong_magic, wrong_version, no_blank_line, huge_header, size_missing, bad_hex,
                                   path_without_path, no_equals}) {
    const DraftRead read = decode_draft(bytes);
    CHECK(read.state == DraftState::unreadable);
    CHECK(read.bytes == bytes);
    CHECK(read.draft.text.isEmpty());
  }
}

TEST_CASE("drafts: path_draft_id is stable and distinguishes paths") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString target = dir.filePath("target.md");
  QFile tf(target);
  REQUIRE(tf.open(QIODevice::WriteOnly));
  tf.close();
  const QString link = dir.filePath("link.md");
  REQUIRE(QFile::link(target, link));

  const QString id_target = path_draft_id(target);
  CHECK(id_target == path_draft_id(target));  
  CHECK(id_target.startsWith("p-"));
  CHECK(id_target.size() == 18);  

  CHECK(id_target == path_draft_id(link));  

  const QString other = dir.filePath("other.md");
  QFile of(other);
  REQUIRE(of.open(QIODevice::WriteOnly));
  of.close();
  CHECK(id_target != path_draft_id(other));

  const QString missing = dir.filePath("gone.md");
  const QString id_missing = path_draft_id(missing);
  CHECK(id_missing.startsWith("p-"));
  CHECK(id_missing == path_draft_id(missing));
}

TEST_CASE("drafts: new_untitled_draft_id has the right form and is random") {
  const QString a = new_untitled_draft_id();
  const QString b = new_untitled_draft_id();
  CHECK(a.startsWith("u-"));
  CHECK(a.size() == 18);  
  CHECK(a != b);
}

TEST_CASE("drafts: is_draft_id rejects bad forms") {
  CHECK_FALSE(is_draft_id(""));
  CHECK_FALSE(is_draft_id("/"));
  CHECK_FALSE(is_draft_id(".."));
  CHECK_FALSE(is_draft_id("ABC"));
  CHECK_FALSE(is_draft_id(QString(41, 'a')));
  CHECK(is_draft_id(QString(40, 'a')));
  CHECK(is_draft_id("p-0123456789abcdef"));
}

TEST_CASE("drafts: write_draft, read_draft and remove_draft") {
  QTemporaryDir temp;
  REQUIRE(temp.isValid());
  const QString dir = temp.filePath("drafts");
  const Draft d{.untitled = true, .path = "", .saved_sha256 = "", .text = "hello"};

  REQUIRE(write_draft(dir, "u-0000000000000000", d));
  CHECK(is_owner_only(dir, true));
  CHECK(is_owner_only(QDir(dir).filePath("u-0000000000000000.draft"), false));

  const DraftRead read = read_draft(dir, "u-0000000000000000");
  CHECK(read.state == DraftState::ok);
  CHECK(read.draft.text == "hello");
  CHECK(read.written.isValid());

  const DraftRead missing = read_draft(dir, "u-1111111111111111");
  CHECK(missing.state == DraftState::missing);

  CHECK(remove_draft(dir, "u-0000000000000000"));
  CHECK(read_draft(dir, "u-0000000000000000").state == DraftState::missing);
  CHECK(remove_draft(dir, "u-0000000000000000"));  
}

TEST_CASE("drafts: list_draft_ids skips bad names and sorts newest first") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const Draft d{.untitled = true, .path = "", .saved_sha256 = "", .text = "x"};
  REQUIRE(write_draft(dir.path(), "u-aaaaaaaaaaaaaaaa", d));
  REQUIRE(write_draft(dir.path(), "u-bbbbbbbbbbbbbbbb", d));
  REQUIRE(write_draft(dir.path(), "u-cccccccccccccccc", d));
  touch(dir.filePath("u-aaaaaaaaaaaaaaaa.lock"));  
  touch(dir.filePath("u-aaaaaaaaaaaaaaaa.txt"));  
  touch(dir.filePath("BAD.draft"));  

  const QDateTime t0 = QDateTime::currentDateTimeUtc().addSecs(-30);
  set_mtime(dir.filePath("u-aaaaaaaaaaaaaaaa.draft"), t0);
  set_mtime(dir.filePath("u-bbbbbbbbbbbbbbbb.draft"), t0.addSecs(10));
  set_mtime(dir.filePath("u-cccccccccccccccc.draft"), t0.addSecs(20));

  CHECK(list_draft_ids(dir.path()) ==
        QStringList{"u-cccccccccccccccc", "u-bbbbbbbbbbbbbbbb", "u-aaaaaaaaaaaaaaaa"});
}

TEST_CASE("drafts: lock_draft blocks a second lock, released lock can be taken again") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  auto first = lock_draft(dir.path(), "u-aaaaaaaaaaaaaaaa");
  REQUIRE(first != nullptr);
  CHECK(lock_draft(dir.path(), "u-aaaaaaaaaaaaaaaa") == nullptr);
  first.reset();
  CHECK(lock_draft(dir.path(), "u-aaaaaaaaaaaaaaaa") != nullptr);
}

TEST_CASE("drafts: lock_draft takes over a dead pid's lock file") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString id = "u-aaaaaaaaaaaaaaaa";
  const QString lock_path = dir.filePath(id + ".lock");

  QByteArray tail_after_pid;
  {
    auto first = lock_draft(dir.path(), id);
    REQUIRE(first != nullptr);
    QFile f(lock_path);
    REQUIRE(f.open(QIODevice::ReadOnly));
    const QByteArray content = f.readAll();
    tail_after_pid = content.mid(content.indexOf('\n'));
  }
  REQUIRE_FALSE(QFile::exists(lock_path));

  QProcess finished;
  finished.start("/bin/true", QStringList());
  REQUIRE(finished.waitForStarted());
  const qint64 dead_pid = finished.processId();
  REQUIRE(dead_pid > 0);
  REQUIRE(finished.waitForFinished());

  {
    QFile f(lock_path);
    REQUIRE(f.open(QIODevice::WriteOnly));
    f.write(QByteArray::number(dead_pid) + tail_after_pid);
  }

  auto second = lock_draft(dir.path(), id);
  CHECK(second != nullptr);
}

TEST_CASE("drafts: claim_path_draft_id takes -2 when the first is locked") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString path = "/some/fake/note.md";
  const QString base = path_draft_id(path);
  const Draft d{.untitled = false, .path = path, .saved_sha256 = QByteArray(64, '0'), .text = "x"};

  std::unique_ptr<QLockFile> lock1;
  const QString id1 = claim_path_draft_id(dir.path(), path, &lock1);
  REQUIRE(id1 == base);
  REQUIRE(lock1 != nullptr);
  REQUIRE(write_draft(dir.path(), id1, d));

  std::unique_ptr<QLockFile> lock2;
  const QString id2 = claim_path_draft_id(dir.path(), path, &lock2);
  CHECK(id2 == base + "-2");
  REQUIRE(lock2 != nullptr);
  REQUIRE(write_draft(dir.path(), id2, d));

  const QDateTime t0 = QDateTime::currentDateTimeUtc().addSecs(-10);
  set_mtime(dir.filePath(id1 + ".draft"), t0);
  set_mtime(dir.filePath(id2 + ".draft"), t0.addSecs(5));

  CHECK(path_draft_ids(dir.path(), path) == QStringList{id2, id1});
}

TEST_CASE("drafts: path_draft_ids finds -3 when -2 is gone") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString path = "/some/fake/note.md";
  const QString base = path_draft_id(path);
  const Draft d{.untitled = false, .path = path, .saved_sha256 = QByteArray(64, '0'), .text = "x"};
  REQUIRE(write_draft(dir.path(), base + "-3", d));
  REQUIRE(write_draft(dir.path(), base + "-x", d));  
  CHECK(path_draft_ids(dir.path(), path) == QStringList{base + "-3"});
}

TEST_CASE("drafts: claim_path_draft_id gives up when no lock can be made") {
  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString file = dir.filePath("plain-file");
  QFile f(file);
  REQUIRE(f.open(QIODevice::WriteOnly));
  f.close();
  std::unique_ptr<QLockFile> lock;
  CHECK(claim_path_draft_id(file + "/drafts", "/some/fake/note.md", &lock).isEmpty());
  CHECK(lock == nullptr);
}

TEST_CASE("drafts: logs carry no marker word of the text nor the path") {
  constexpr std::string_view marker = "QZXDRAFTMARK";
  std::vector<std::string> lines;
  lumos::set_log_sink([&](lumos::LogLevel, std::string_view line) { lines.emplace_back(line); });
  lumos::set_log_level(lumos::LogLevel::debug);

  QTemporaryDir dir;
  REQUIRE(dir.isValid());
  const QString path = QStringLiteral("/a/%1.md").arg(QString::fromLatin1(marker.data(), static_cast<int>(marker.size())));
  const Draft d{.untitled = false, .path = path, .saved_sha256 = QByteArray(64, '0'),
                .text = QByteArray(marker.data(), static_cast<int>(marker.size()))};
  REQUIRE(write_draft(dir.path(), "p-0000000000000000", d));
  (void)read_draft(dir.path(), "p-0000000000000000");
  (void)remove_draft(dir.path(), "p-0000000000000000");

  lumos::set_log_sink(nullptr);
  lumos::set_log_level(lumos::LogLevel::info);

  REQUIRE_FALSE(lines.empty());
  for (const std::string& line : lines) {
    INFO(line);
    CHECK(line.find(marker) == std::string::npos);
  }
}
