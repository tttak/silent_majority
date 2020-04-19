// NNUE評価関数の入力特徴量HalfKPKfileの定義

#include "../../../config.h"

#if defined(EVAL_NNUE)

#include "half_kpkfile.h"
#include "index_list.h"

namespace Eval {

namespace NNUE {

namespace Features {

// 玉の位置とBonaPieceから特徴量のインデックスを求める
template <Side AssociatedKing>
inline IndexType HalfKPKfile<AssociatedKing>::MakeIndex(Square sq_k, BonaPiece p, Square sq_other_k) {
  return (static_cast<IndexType>(fe_end) * static_cast<IndexType>(sq_k) + p) 
       + (static_cast<IndexType>(fe_end) * static_cast<IndexType>(SQ_NB) * static_cast<IndexType>(makeFile(sq_other_k)));
}

// 駒の情報を取得する
template <Side AssociatedKing>
inline void HalfKPKfile<AssociatedKing>::GetPieces(
    const Position& pos, Color perspective,
    BonaPiece** pieces, Square* sq_target_k, Square* sq_other_k) {

  *sq_target_k = pos.kingSquare(perspective);
  *sq_other_k = pos.kingSquare(~perspective);

  if (perspective == White) {
    *sq_target_k = inverse(*sq_target_k);
    *sq_other_k = inverse(*sq_other_k);
  }

  auto pos_ = const_cast<Position*>(&pos);
  *pieces = (BonaPiece*)((perspective == Black) ? pos_->plist0() : pos_->plist1());
}

// 特徴量のうち、値が1であるインデックスのリストを取得する
template <Side AssociatedKing>
void HalfKPKfile<AssociatedKing>::AppendActiveIndices(
    const Position& pos, Color perspective, IndexList* active) {
  // コンパイラの警告を回避するため、配列サイズが小さい場合は何もしない
  if (RawFeatures::kMaxActiveDimensions < kMaxActiveDimensions) return;

  BonaPiece* pieces;
  Square sq_target_k;
  Square sq_other_k;
  GetPieces(pos, perspective, &pieces, &sq_target_k, &sq_other_k);
  for (PieceNumber i = PIECE_NUMBER_ZERO; i < PIECE_NUMBER_KING; ++i) {
    active->push_back(MakeIndex(sq_target_k, pieces[i], sq_other_k));
  }
}

// 特徴量のうち、一手前から値が変化したインデックスのリストを取得する
template <Side AssociatedKing>
void HalfKPKfile<AssociatedKing>::AppendChangedIndices(
    const Position& pos, Color perspective,
    IndexList* removed, IndexList* added) {
  BonaPiece* pieces;
  Square sq_target_k;
  Square sq_other_k;
  GetPieces(pos, perspective, &pieces, &sq_target_k, &sq_other_k);
  const auto& cl = pos.state()->cl;

  for (int i = 0; i < cl.size; ++i) {
    if (cl.listindex[i] >= PIECE_NUMBER_KING) continue;
    const auto old_p = static_cast<BonaPiece>(
        cl.clistpair[i].oldlist[perspective]);
    removed->push_back(MakeIndex(sq_target_k, old_p, sq_other_k));
    const auto new_p = static_cast<BonaPiece>(
        cl.clistpair[i].newlist[perspective]);
    added->push_back(MakeIndex(sq_target_k, new_p, sq_other_k));
  }
}

template class HalfKPKfile<Side::kFriend>;
template class HalfKPKfile<Side::kEnemy>;

}  // namespace Features

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)
