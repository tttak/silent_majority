// NNUE評価関数の入力特徴量HalfKPの定義

#include "../../../config.h"

#if defined(EVAL_NNUE)

#include "half_kp.h"
#include "index_list.h"

namespace Eval {

namespace NNUE {

namespace Features {

// 玉の位置とBonaPieceから特徴量のインデックスを求める
template <Side AssociatedKing>
inline IndexType HalfKP<AssociatedKing>::MakeIndex(Square sq_k, BonaPiece p) {
  return static_cast<IndexType>(fe_end) * static_cast<IndexType>(sq_k) + p;
}

// 駒の情報を取得する
template <Side AssociatedKing>
inline void HalfKP<AssociatedKing>::GetPieces(
    const Position& pos, Color perspective,
    BonaPiece** pieces, Square* sq_target_k) {
#if 0
  *pieces = (perspective == BLACK) ?
      pos.eval_list()->piece_list_fb() :
      pos.eval_list()->piece_list_fw();
  const PieceNumber target = (AssociatedKing == Side::kFriend) ?
      static_cast<PieceNumber>(PIECE_NUMBER_KING + perspective) :
      static_cast<PieceNumber>(PIECE_NUMBER_KING + ~perspective);
  *sq_target_k = static_cast<Square>(((*pieces)[target] - f_king) % SQ_NB);
#endif
}

// 特徴量のうち、値が1であるインデックスのリストを取得する
template <Side AssociatedKing>
void HalfKP<AssociatedKing>::AppendActiveIndices(
    const Position& pos, Color perspective, IndexList* active) {
  // コンパイラの警告を回避するため、配列サイズが小さい場合は何もしない
  if (RawFeatures::kMaxActiveDimensions < kMaxActiveDimensions) return;

  Square sq_target_k = pos.kingSquare(perspective);
  if (perspective == White) {
    sq_target_k = inverse(sq_target_k);
  }

  auto pos_ = const_cast<Position*>(&pos);
  const int* plist = (perspective == Black) ? pos_->plist0() : pos_->plist1();

  for (PieceNumber i = PIECE_NUMBER_ZERO; i < PIECE_NUMBER_KING; ++i) {
    active->push_back(MakeIndex(sq_target_k, (BonaPiece)plist[i]));
  }
}

// 特徴量のうち、一手前から値が変化したインデックスのリストを取得する
template <Side AssociatedKing>
void HalfKP<AssociatedKing>::AppendChangedIndices(
    const Position& pos, Color perspective,
    IndexList* removed, IndexList* added) {

  Square sq_target_k = pos.kingSquare(perspective);
  if (perspective == White) {
    sq_target_k = inverse(sq_target_k);
  }

  const auto& cl = pos.state()->cl;
  for (int i = 0; i < cl.size; ++i) {
    if (cl.listindex[i] >= PIECE_NUMBER_KING) continue;
    const auto old_p = static_cast<BonaPiece>(
        cl.clistpair[i].oldlist[perspective]);
    removed->push_back(MakeIndex(sq_target_k, old_p));
    const auto new_p = static_cast<BonaPiece>(
        cl.clistpair[i].newlist[perspective]);
    added->push_back(MakeIndex(sq_target_k, new_p));
  }
}

template class HalfKP<Side::kFriend>;
template class HalfKP<Side::kEnemy>;

}  // namespace Features

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)
