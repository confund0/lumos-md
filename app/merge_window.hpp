#pragma once

#include <QDialog>
#include <QString>

#include <string>
#include <vector>

#include "line_merge.hpp"

class QEvent;
class QLabel;
class QObject;
class QPlainTextEdit;
class QPushButton;

struct MergeNames {
  QString other_title;  
};

class MergeWindow : public QDialog {
 public:
  enum class Pane { yours, other, result };

  MergeWindow(const LineMerge& merge, const QString& name, const MergeNames& names, QWidget* parent = nullptr);

  std::string result() const;
  int conflicts() const;
  int open_conflicts() const;
  int current_conflict() const;
  void pick(Pick pick);
  void use_all(Pick side);

  void undo();
  void redo();
  bool can_undo() const;
  bool can_redo() const;

  QPlainTextEdit* pane(Pane pane) const;
  QPushButton* use_button() const;       
  QPushButton* use_this_button(Pane side) const;  
  QPushButton* use_all_button(Pane side) const;   
  QPushButton* use_both_button() const;
  QLabel* conflict_label() const;
  QPushButton* undo_button() const;
  QPushButton* redo_button() const;

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void rebuild();
  void scroll_to_current_conflict();
  void move_to_next_open_after(int start);
  void apply_pick(int ordinal, Pick chosen);
  void commit_undo_step(const std::vector<Pick>& before_picks, int before_conflict);

  struct UndoStep {
    std::vector<Pick> picks;
    int current_conflict = -1;
  };

  LineMerge merge_;
  MergeNames names_;
  std::vector<Pick> picks_;
  int current_conflict_ = -1;
  std::vector<UndoStep> undo_stack_;
  std::vector<UndoStep> redo_stack_;
  std::vector<int> row_chunk_;
  std::vector<int> chunk_first_row_;
  std::vector<int> chunk_conflict_ordinal_;
  std::vector<int> ordinal_to_chunk_;

  class LineView;
  LineView* yours_view_ = nullptr;
  LineView* other_view_ = nullptr;
  LineView* result_view_ = nullptr;
  QLabel* conflict_label_ = nullptr;
  QPushButton* use_this_yours_button_ = nullptr;
  QPushButton* use_all_yours_button_ = nullptr;
  QPushButton* use_this_other_button_ = nullptr;
  QPushButton* use_all_other_button_ = nullptr;
  QPushButton* use_both_button_ = nullptr;
  QPushButton* use_button_ = nullptr;
  QPushButton* undo_button_ = nullptr;
  QPushButton* redo_button_ = nullptr;
};
