// NNUE評価関数の入力特徴量PPの定義

#include "../../../config.h"

#if defined(EVAL_NNUE)

#include "pp.h"
#include "index_list.h"

namespace Eval {

namespace NNUE {

namespace Features {

// BonaPieceから特徴量のインデックスを求める
inline IndexType PP::MakeIndex(BonaPiece bp1, BonaPiece bp2) {
  IndexType bp_max = std::max(bp1, bp2);
  IndexType bp_min = std::min(bp1, bp2);

  return bp_max * (bp_max - 1) / 2 + bp_min;
}

// 特徴量のうち、値が1であるインデックスのリストを取得する
void PP::AppendActiveIndices(
    const Position& pos, Color perspective, IndexList* active) {
  // コンパイラの警告を回避するため、配列サイズが小さい場合は何もしない
  if (RawFeatures::kMaxActiveDimensions < kMaxActiveDimensions) return;

  auto pos_ = const_cast<Position*>(&pos);
  const int* plist = (perspective == Black) ? pos_->plist0() : pos_->plist1();

  for (PieceNumber pn1 = PIECE_NUMBER_ZERO; pn1 < PIECE_NUMBER_KING; ++pn1) {
    for (PieceNumber pn2 = PIECE_NUMBER_ZERO; pn2 < pn1; ++pn2) {
      active->push_back(MakeIndex(static_cast<BonaPiece>(plist[pn1]), static_cast<BonaPiece>(plist[pn2])));
    }
  }
}

// 特徴量のうち、一手前から値が変化したインデックスのリストを取得する
void PP::AppendChangedIndices(
    const Position& pos, Color perspective,
    IndexList* removed, IndexList* added) {
  const auto& cl = pos.state()->cl;

  auto pos_ = const_cast<Position*>(&pos);
  const int* plist = (perspective == Black) ? pos_->plist0() : pos_->plist1();

  bool dirty_p[2] = { false, false };

  for (int i = 0; i < cl.size; ++i) {
    if (cl.listindex[i] < PIECE_NUMBER_KING) {
      dirty_p[i] = true;
    }
  }

  if (dirty_p[0]) {
    if (dirty_p[1]) {
      PieceNumber dirty_pn_p0 = static_cast<PieceNumber>(cl.listindex[0]);
      BonaPiece old_p0 = static_cast<BonaPiece>(cl.clistpair[0].oldlist[perspective]);
      BonaPiece new_p0 = static_cast<BonaPiece>(cl.clistpair[0].newlist[perspective]);

      PieceNumber dirty_pn_p1 = static_cast<PieceNumber>(cl.listindex[1]);
      BonaPiece old_p1 = static_cast<BonaPiece>(cl.clistpair[1].oldlist[perspective]);
      BonaPiece new_p1 = static_cast<BonaPiece>(cl.clistpair[1].newlist[perspective]);

      for (PieceNumber pn_p = PIECE_NUMBER_ZERO; pn_p < PIECE_NUMBER_KING; ++pn_p) {
        if (pn_p != dirty_pn_p0) {
          if (pn_p != dirty_pn_p1) {
            removed->push_back(MakeIndex(static_cast<BonaPiece>(plist[pn_p]), old_p0));
            added->push_back(MakeIndex(static_cast<BonaPiece>(plist[pn_p]), new_p0));

            removed->push_back(MakeIndex(static_cast<BonaPiece>(plist[pn_p]), old_p1));
            added->push_back(MakeIndex(static_cast<BonaPiece>(plist[pn_p]), new_p1));
          }
        }
      }

      removed->push_back(MakeIndex(old_p0, old_p1));
      added->push_back(MakeIndex(new_p0, new_p1));
    }
    else {
      PieceNumber dirty_pn_p = static_cast<PieceNumber>(cl.listindex[0]);
      BonaPiece old_p = static_cast<BonaPiece>(cl.clistpair[0].oldlist[perspective]);
      BonaPiece new_p = static_cast<BonaPiece>(cl.clistpair[0].newlist[perspective]);

      for (PieceNumber pn_p = PIECE_NUMBER_ZERO; pn_p < PIECE_NUMBER_KING; ++pn_p) {
        if (pn_p != dirty_pn_p) {
          removed->push_back(MakeIndex(static_cast<BonaPiece>(plist[pn_p]), old_p));
          added->push_back(MakeIndex(static_cast<BonaPiece>(plist[pn_p]), new_p));
        }
      }
    }
  }
  else {
    if (dirty_p[1]) {
      PieceNumber dirty_pn_p = static_cast<PieceNumber>(cl.listindex[1]);
      BonaPiece old_p = static_cast<BonaPiece>(cl.clistpair[1].oldlist[perspective]);
      BonaPiece new_p = static_cast<BonaPiece>(cl.clistpair[1].newlist[perspective]);

      for (PieceNumber pn_p = PIECE_NUMBER_ZERO; pn_p < PIECE_NUMBER_KING; ++pn_p) {
        if (pn_p != dirty_pn_p) {
          removed->push_back(MakeIndex(static_cast<BonaPiece>(plist[pn_p]), old_p));
          added->push_back(MakeIndex(static_cast<BonaPiece>(plist[pn_p]), new_p));
        }
      }
    }
  }
}

}  // namespace Features

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)
