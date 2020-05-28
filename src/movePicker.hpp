#ifndef APERY_MOVEPICKER_HPP
#define APERY_MOVEPICKER_HPP

#include "move.hpp"
#include "position.hpp"
#include "search.hpp"
#include "YaneuraOu/movepick.h"

class MovePicker {
public:
	MovePicker(const MovePicker&) = delete;
	MovePicker& operator=(const MovePicker&) = delete;

	MovePicker(const Position&, Move, const Depth, const Square, const ButterflyHistory* mh, const CapturePieceToHistory* cph, const PieceToHistory** ch);
	MovePicker(const Position&, const Move, Score, const CapturePieceToHistory* cph);
    MovePicker(const Position&, const Move, const Depth, Search::Stack*, const ButterflyHistory* mh, const LowPlyHistory* lp, const CapturePieceToHistory* cph, const PieceToHistory** ch, int pl);
	Move nextMove(bool skipQuiets = false);

private:
	void scoreCaptures();
	template <bool IsDrop> void scoreNonCapturesMinusPro();
	void scoreEvasions();
    ExtMove* begin() { return cur; }
    ExtMove* end() { return endMoves; }

	const Position& pos;
    const Search::Stack* ss;
    Move countermove;
	Depth depth;
	Move ttMove;
	Square recaptureSquare;
	Score threshold;
    int stage;
	ExtMove* cur;
	ExtMove* endMoves;
	ExtMove* endBadCaptures;
	ExtMove moves[MaxLegalMoves];

	// LowPlyHistory
	int ply;

	// コンストラクタで渡されたhistroyのポインタを保存しておく変数。
	const ButterflyHistory* mainHistory;
	const LowPlyHistory* lowPlyHistory;
	const CapturePieceToHistory* captureHistory;
	const PieceToHistory** continuationHistory;
};

#endif // #ifndef APERY_MOVEPICKER_HPP
