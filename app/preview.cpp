#include "preview.hpp"

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QContextMenuEvent>
#include <QFileInfo>
#include <QImageReader>
#include <QMenu>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextDocument>
#include <QToolButton>

#include <algorithm>
#include <cmath>

#include "link_open.hpp"
#include "lumos/log.hpp"

namespace {

constexpr std::string_view kComp = "app";

std::optional<bool> task_marker_checked(const lumos::SyntaxTree& tree, lumos::NodeId id) {
  for (lumos::NodeId c : tree.children(id))
    if (tree.node(c).kind == lumos::NodeKind::task_marker) return tree.node(c).checked;
  return std::nullopt;
}

lumos::md::NodeOutput task_item_node_hook(const lumos::SyntaxTree& tree, const lumos::TextSource&,
                                          lumos::NodeId id, bool entering, std::string& out) {
  if (tree.node(id).kind != lumos::NodeKind::list_item) return lumos::md::NodeOutput::keep;
  const std::optional<bool> checked = task_marker_checked(tree, id);
  if (!checked) return lumos::md::NodeOutput::keep;
  if (entering) {
    if (!out.empty() && out.back() != '\n') out += '\n';
    out += *checked ? "<li>\xE2\x98\x91 " : "<li>\xE2\x98\x90 ";
  } else {
    out += "</li>\n";
  }
  return lumos::md::NodeOutput::replace_tags;
}

}  

PreviewView::PreviewView(QWidget* parent) : QTextBrowser(parent) {
  setOpenLinks(false);
  setOpenExternalLinks(false);
  document()->setDocumentMargin(36);
  connect(this, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) { on_anchor_clicked(url); });
}

void PreviewView::show_document(const lumos::Document& doc, const QString& note_path) {
  if (note_path != note_path_) image_cache_.clear();
  note_path_ = note_path;
  links_.clear();
  ++render_count_;

  lumos::md::HtmlOptions options;
  options.source_anchors = true;
  options.url_hook = [this](lumos::md::UrlKind kind, std::string_view url) { return resolve_url(kind, url); };
  options.node_hook = &task_item_node_hook;

  std::string html;
  const bool ok = lumos::md::render_html(doc.tree(), doc.buffer(), options, html);
  note_size_ = doc.size();
  anchors_stale_ = true;

  if (!ok) {
    links_.clear();
    anchors_.clear();
    anchors_stale_ = false;
    setPlainText(tr("The preview is too large (over 16 MiB of HTML)."));
    lumos::log(lumos::LogLevel::warn, kComp, "html_over_cap", {{"bytes", std::to_string(doc.size())}});
    return;
  }
  QString qhtml = QString::fromStdString(html);
  qhtml.replace(QStringLiteral("\n</code></pre>"), QStringLiteral("</code></pre>"));
  setHtml(qhtml);
}

std::optional<std::string> PreviewView::resolve_url(lumos::md::UrlKind kind, std::string_view url) {
  if (kind == lumos::md::UrlKind::link) {
    links_.emplace_back(url);
    return "lumos-link:" + std::to_string(links_.size() - 1);
  }
  lumos::LinkTarget target;
  target.kind = lumos::md::UrlKind::image;
  target.url = std::string(url);
  target.form = lumos::url_form(url);
  const QUrl resolved = link_url(target, note_path_);
  if (!resolved.isLocalFile()) return std::nullopt;
  return QUrl::fromLocalFile(resolved.toLocalFile()).toString(QUrl::FullyEncoded).toStdString();
}

void PreviewView::on_anchor_clicked(const QUrl& url) {
  const QString text = url.toString();
  const QString prefix = QStringLiteral("lumos-link:");
  if (!text.startsWith(prefix)) return;
  bool ok = false;
  const qulonglong index = text.mid(prefix.size()).toULongLong(&ok);
  if (!ok || index >= links_.size() || !link_clicked_) return;
  lumos::LinkTarget target;
  target.kind = lumos::md::UrlKind::link;
  target.url = links_[static_cast<std::size_t>(index)];
  target.form = lumos::url_form(target.url);
  link_clicked_(target);
}

void PreviewView::set_look(const QColor& background, const QColor& lines, const QColor& code) {
  QPalette p = palette();
  p.setColor(QPalette::Base, background);
  setPalette(p);
  document()->setDefaultStyleSheet(
      QStringLiteral("table { border-collapse: collapse; margin-top: 4px; margin-bottom: 8px; }"
                     " th, td { border: 1px solid %1; padding: 4px 8px; }"
                     " pre { background-color: %2; }")
          .arg(lines.name(), code.name()));
}

void PreviewView::set_link_clicked(std::function<void(const lumos::LinkTarget&)> callback) {
  link_clicked_ = std::move(callback);
}

QVariant PreviewView::loadResource(int type, const QUrl& name) {
  if (type != QTextDocument::ImageResource || !name.isLocalFile()) return QVariant();
  const QString path = name.toLocalFile();
  const QDateTime modified = QFileInfo(path).lastModified();
  const auto cached = image_cache_.find(path);
  if (cached != image_cache_.end() && cached->second.modified == modified) return cached->second.image;

  QImageReader reader(path);
  reader.setAutoTransform(true);
  const QSize size = reader.size();
  const qreal dpr = devicePixelRatioF();
  const int max_width = static_cast<int>(std::ceil(800 * dpr));
  if (size.isValid() && size.width() > max_width)
    reader.setScaledSize(QSize(max_width, std::max(1, size.height() * max_width / size.width())));
  const bool scaled = reader.scaledSize().isValid();
  QImage image = reader.read();
  if (image.isNull()) return QVariant();
  if (scaled) image.setDevicePixelRatio(dpr);
  image_cache_[path] = {modified, image};
  return image;
}

QMenu* PreviewView::make_context_menu() {
  auto* menu = new QMenu(this);
  QAction* copy_action = menu->addAction(tr("Copy"));
  copy_action->setEnabled(textCursor().hasSelection());
  connect(copy_action, &QAction::triggered, this, [this] { copy(); });
  QAction* select_all_action = menu->addAction(tr("Select All"));
  connect(select_all_action, &QAction::triggered, this, [this] { selectAll(); });
  return menu;
}

void PreviewView::contextMenuEvent(QContextMenuEvent* event) {
  QMenu* const menu = make_context_menu();
  menu->exec(event->globalPos());
  menu->deleteLater();
}

void PreviewView::resizeEvent(QResizeEvent* event) {
  QTextBrowser::resizeEvent(event);
  anchors_stale_ = true;
}

void PreviewView::rebuild_anchors() {
  anchors_.clear();
  QTextDocument* const doc = document();
  QAbstractTextDocumentLayout* const layout = doc->documentLayout();
  std::optional<std::size_t> last_offset;
  for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
    const int y = static_cast<int>(layout->blockBoundingRect(block).top());
    for (auto it = block.begin(); !it.atEnd(); ++it) {
      const QTextFragment fragment = it.fragment();
      if (!fragment.isValid()) continue;
      for (const QString& name : fragment.charFormat().anchorNames()) {
        if (!name.startsWith(QLatin1String("lumos-"))) continue;
        bool ok = false;
        const qulonglong offset = name.mid(6).toULongLong(&ok);
        if (!ok) continue;
        if (last_offset && offset <= *last_offset) continue;
        anchors_.emplace_back(static_cast<std::size_t>(offset), y);
        last_offset = static_cast<std::size_t>(offset);
      }
    }
  }
  anchors_stale_ = false;
}

const std::vector<std::pair<std::size_t, int>>& PreviewView::anchors() {
  if (anchors_stale_) rebuild_anchors();
  return anchors_;
}

void PreviewView::scroll_to_offset(std::size_t offset) {
  const auto& anchor_list = anchors();
  const int doc_height = static_cast<int>(document()->size().height());

  std::size_t lower_offset = 0;
  int lower_y = 0;
  std::size_t upper_offset = note_size_;
  int upper_y = doc_height;
  for (const auto& [anchor_offset, anchor_y] : anchor_list) {
    if (anchor_offset == 0) continue;
    if (anchor_offset <= offset) {
      lower_offset = anchor_offset;
      lower_y = anchor_y;
    } else {
      upper_offset = anchor_offset;
      upper_y = anchor_y;
      break;
    }
  }

  int y = lower_y;
  if (upper_offset > lower_offset) {
    const double t = static_cast<double>(offset - lower_offset) / static_cast<double>(upper_offset - lower_offset);
    y = static_cast<int>(std::lround(lower_y + t * (upper_y - lower_y)));
  }
  QScrollBar* const bar = verticalScrollBar();
  bar->setValue(std::clamp(y, bar->minimum(), bar->maximum()));
}

namespace {
constexpr int kHandleWidth = 18;
constexpr int kButtonSize = 16;
}  

PreviewSplitter::PreviewSplitter(QWidget* editor_side, QWidget* preview_side, QWidget* parent)
    : QSplitter(Qt::Horizontal, parent) {
  addWidget(editor_side);
  addWidget(preview_side);
  setHandleWidth(kHandleWidth);
  connect(this, &QSplitter::splitterMoved, this, [this](int, int) {
    remember_split();
    notify();
  });
}

QSplitterHandle* PreviewSplitter::createHandle() {
  handle_ = new PreviewSplitterHandle(orientation(), this);
  return handle_;
}

bool PreviewSplitter::preview_full() const {
  const QList<int> s = sizes();
  return s.size() == 2 && s[0] == 0;
}

bool PreviewSplitter::editor_full() const {
  const QList<int> s = sizes();
  return s.size() == 2 && s[1] == 0;
}

void PreviewSplitter::show_preview_full() {
  remember_split();  
  setSizes({0, width()});
  notify();
}

void PreviewSplitter::show_editor_full() {
  remember_split();
  setSizes({width(), 0});
  notify();
}

void PreviewSplitter::restore_split() {
  apply_share();
  notify();
}

void PreviewSplitter::set_preview_share(double share) { share_ = std::clamp(share, 0.05, 0.95); }

void PreviewSplitter::apply_share() {
  refresh();  
  const int total = std::max(0, width() - handleWidth());
  const int preview = static_cast<int>(std::lround(total * share_));
  setSizes({total - preview, preview});
}

void PreviewSplitter::resizeEvent(QResizeEvent* event) {
  QSplitter::resizeEvent(event);
  const QList<int> s = sizes();
  if (s.size() == 2 && s[0] > 0 && s[1] > 0) apply_share();
}

void PreviewSplitter::set_changed(std::function<void()> callback) { changed_ = std::move(callback); }

void PreviewSplitter::remember_split() {
  const QList<int> s = sizes();
  if (s.size() == 2 && s[0] > 0 && s[1] > 0) set_preview_share(static_cast<double>(s[1]) / (s[0] + s[1]));
}

void PreviewSplitter::notify() {
  if (handle_ != nullptr) handle_->refresh();
  if (changed_) changed_();
}

PreviewSplitterHandle::PreviewSplitterHandle(Qt::Orientation orientation, QSplitter* parent)
    : QSplitterHandle(orientation, parent) {
  auto* const self = static_cast<PreviewSplitter*>(parent);
  preview_full_button_ = new QToolButton(this);
  preview_full_button_->setObjectName(QStringLiteral("preview_full_button"));
  preview_full_button_->setText(QString::fromUtf8("\xE2\x97\x80"));  
  preview_full_button_->setAutoRaise(true);
  connect(preview_full_button_, &QToolButton::clicked, self, [self] {
    if (self->editor_full()) self->restore_split();
    else self->show_preview_full();
  });
  editor_full_button_ = new QToolButton(this);
  editor_full_button_->setObjectName(QStringLiteral("editor_full_button"));
  editor_full_button_->setText(QString::fromUtf8("\xE2\x96\xB6"));  
  editor_full_button_->setAutoRaise(true);
  connect(editor_full_button_, &QToolButton::clicked, self, [self] {
    if (self->preview_full()) self->restore_split();
    else self->show_editor_full();
  });
  refresh();
}

void PreviewSplitterHandle::refresh() {
  auto* const self = static_cast<PreviewSplitter*>(splitter());
  preview_full_button_->setEnabled(!self->preview_full());
  editor_full_button_->setEnabled(!self->editor_full());
}

void PreviewSplitterHandle::resizeEvent(QResizeEvent* event) {
  QSplitterHandle::resizeEvent(event);
  const int x = (width() - kButtonSize) / 2;
  const int top = (height() - 2 * kButtonSize) / 2;
  preview_full_button_->setGeometry(x, top, kButtonSize, kButtonSize);
  editor_full_button_->setGeometry(x, top + kButtonSize, kButtonSize, kButtonSize);
}
