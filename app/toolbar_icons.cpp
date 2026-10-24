
#include "toolbar_icons.hpp"

#include <QFont>
#include <QGuiApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>

namespace {

void glyph(QPainter& p, const QString& text, double pixel_size, double cx, double cy, bool bold = false,
           bool italic = false) {
  QFont f = QGuiApplication::font();
  f.setPixelSize(100);
  f.setBold(bold);
  f.setItalic(italic);
  QPainterPath path;
  path.addText(0, 0, f, text);
  const QRectF box = path.boundingRect();
  const double scale = pixel_size / 100;
  QTransform t;
  t.translate(cx - box.center().x() * scale, cy - box.center().y() * scale);
  t.scale(scale, scale);
  p.fillPath(t.map(path), p.pen().color());
}

constexpr double kRows[] = {3, 8, 13};
void text_lines(QPainter& p, double left) {
  for (const double y : kRows) p.drawLine(QPointF(left, y), QPointF(14.5, y));
}

void numbers(QPainter& p, double cx) {
  const QString digits[] = {QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")};
  for (int i = 0; i < 3; ++i) glyph(p, digits[i], 5, cx, kRows[i], true);
}

void code_mark(QPainter& p) {
  p.drawPolyline(QPolygonF({{5, 4}, {1.5, 8}, {5, 12}}));
  p.drawPolyline(QPolygonF({{11, 4}, {14.5, 8}, {11, 12}}));
  p.drawLine(QPointF(9.5, 3), QPointF(6.5, 13));
}

void grid(QPainter& p, const QRectF& r, const QColor& c) {
  QColor head = c;
  head.setAlpha(90);
  p.fillRect(QRectF(r.left(), r.top(), r.width(), r.height() / 3), head);
  p.drawRect(r);
  for (int i = 1; i < 3; ++i) {
    const double x = r.left() + r.width() * i / 3;
    const double y = r.top() + r.height() * i / 3;
    p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
  }
}

void draw(QPainter& p, ToolIcon kind, const QColor& c) {
  switch (kind) {
    case ToolIcon::new_file:
      p.drawPolygon(QPolygonF({{3.5, 1.5}, {9.5, 1.5}, {12.5, 4.5}, {12.5, 14.5}, {3.5, 14.5}}));
      p.drawPolyline(QPolygonF({{9.5, 1.5}, {9.5, 4.5}, {12.5, 4.5}}));
      break;
    case ToolIcon::open:
      p.drawPolygon(QPolygonF({{1.5, 3}, {6, 3}, {7.5, 5}, {14.5, 5}, {14.5, 13.5}, {1.5, 13.5}}));
      p.drawLine(QPointF(1.5, 7), QPointF(14.5, 7));
      break;
    case ToolIcon::save:
      p.drawPolygon(QPolygonF({{2, 2}, {12, 2}, {14, 4}, {14, 14}, {2, 14}}));
      p.drawRect(QRectF(5, 2, 5.5, 3.5));
      p.drawRect(QRectF(4.5, 8.5, 7, 5.5));
      break;
    case ToolIcon::undo:
    case ToolIcon::redo: {
      if (kind == ToolIcon::redo) p.setTransform(QTransform(-1, 0, 0, 1, 16, 0), true);
      QPainterPath arc;
      arc.moveTo(13, 13);
      arc.cubicTo(13, 7.5, 10, 5.5, 3.5, 5.5);
      p.drawPath(arc);
      p.drawPolyline(QPolygonF({{6.5, 2.5}, {3.5, 5.5}, {6.5, 8.5}}));
      break;
    }
    case ToolIcon::find: {
      p.drawEllipse(QPointF(6.5, 6.5), 4.5, 4.5);
      QPen thick = p.pen();
      thick.setWidthF(2.2);
      p.setPen(thick);
      p.drawLine(QPointF(10, 10), QPointF(14, 14));
      break;
    }
    case ToolIcon::bold: glyph(p, QStringLiteral("B"), 12, 8, 8, true); break;
    case ToolIcon::italic: glyph(p, QStringLiteral("I"), 12, 8, 8, false, true); break;
    case ToolIcon::strike:
      glyph(p, QStringLiteral("S"), 12, 8, 8);
      p.drawLine(QPointF(2.5, 8), QPointF(13.5, 8));
      break;
    case ToolIcon::code: code_mark(p); break;
    case ToolIcon::link:
      p.translate(8, 8);
      p.rotate(-45);
      p.drawRoundedRect(QRectF(-7, -2.5, 8, 5), 2.5, 2.5);
      p.drawRoundedRect(QRectF(-1, -2.5, 8, 5), 2.5, 2.5);
      break;
    case ToolIcon::bullet_list:
      p.save();
      p.setBrush(c);
      for (const double y : kRows) p.drawEllipse(QPointF(2.8, y), 1.3, 1.3);
      p.restore();
      text_lines(p, 6.5);
      break;
    case ToolIcon::ordered_list:
      numbers(p, 2.8);
      text_lines(p, 6.5);
      break;
    case ToolIcon::task_list:
      p.drawRect(QRectF(1.5, 2, 4.5, 4.5));
      p.drawRect(QRectF(1.5, 9.5, 4.5, 4.5));
      p.drawPolyline(QPolygonF({{2.5, 4.2}, {3.6, 5.3}, {5.2, 3}}));
      p.drawLine(QPointF(8.5, 4.25), QPointF(14.5, 4.25));
      p.drawLine(QPointF(8.5, 11.75), QPointF(14.5, 11.75));
      break;
    case ToolIcon::quote:
      p.fillRect(QRectF(1.5, 2, 2, 12), c);
      p.drawLine(QPointF(6.5, 4), QPointF(14.5, 4));
      p.drawLine(QPointF(6.5, 8), QPointF(14.5, 8));
      p.drawLine(QPointF(6.5, 12), QPointF(11.5, 12));
      break;
    case ToolIcon::renumber:
      numbers(p, 3.5);
      p.drawLine(QPointF(10.5, 2), QPointF(10.5, 13.5));
      p.drawPolyline(QPolygonF({{7.5, 10.5}, {10.5, 13.5}, {13.5, 10.5}}));
      break;
    case ToolIcon::horizontal_line: {
      QColor faint = c;
      faint.setAlpha(100);
      p.save();
      p.setPen(QPen(faint, 1));
      p.drawLine(QPointF(2, 3.5), QPointF(14, 3.5));
      p.drawLine(QPointF(2, 12.5), QPointF(14, 12.5));
      p.restore();
      p.setPen(QPen(c, 2, Qt::SolidLine, Qt::RoundCap));
      p.drawLine(QPointF(2, 8), QPointF(14, 8));
      break;
    }
    case ToolIcon::code_block:
      p.drawRoundedRect(QRectF(0.5, 1.5, 15, 13), 2, 2);
      p.translate(3.2, 3.2);
      p.scale(0.6, 0.6);
      code_mark(p);
      break;
    case ToolIcon::table: grid(p, QRectF(1.5, 2.5, 13, 11), c); break;
    case ToolIcon::format_table:
      grid(p, QRectF(1.5, 1.5, 13, 8), c);
      p.drawLine(QPointF(2, 13), QPointF(14, 13));
      p.drawPolyline(QPolygonF({{4, 11.5}, {2, 13}, {4, 14.5}}));
      p.drawPolyline(QPolygonF({{12, 11.5}, {14, 13}, {12, 14.5}}));
      break;
  }
}

class PaintedIcon : public QIconEngine {
 public:
  explicit PaintedIcon(ToolIcon kind) : kind_(kind) {}

  void paint(QPainter* p, const QRect& rect, QIcon::Mode mode, QIcon::State) override {
    const QPalette pal = QGuiApplication::palette();
    const QColor c = pal.color(mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active, QPalette::ButtonText);
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->translate(rect.topLeft());
    p->scale(rect.width() / 16.0, rect.height() / 16.0);
    p->setPen(QPen(c, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->setBrush(Qt::NoBrush);
    draw(*p, kind_, c);
    p->restore();
  }

  QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
    return scaledPixmap(size, mode, state, 1.0);
  }

  QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override {
    QPixmap pm(size * scale);
    pm.fill(Qt::transparent);
    {
      QPainter p(&pm);
      paint(&p, QRect(QPoint(0, 0), pm.size()), mode, state);
    }
    pm.setDevicePixelRatio(scale);
    return pm;
  }

  QIconEngine* clone() const override { return new PaintedIcon(kind_); }

 private:
  ToolIcon kind_;
};

}  

QIcon toolbar_icon(ToolIcon kind) { return QIcon(new PaintedIcon(kind)); }
