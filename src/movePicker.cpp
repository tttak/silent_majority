﻿#include "movePicker.hpp"
#include "generateMoves.hpp"
#include "thread.hpp"

namespace {
const Score LVATable[PieceTypeNum] = {
	Score(0), Score(1), Score(2), Score(3), Score(4), Score(7), Score(8), Score(6), Score(10000),
	Score(5), Score(5), Score(5), Score(5), Score(9), Score(10)
};
inline Score LVA(const PieceType pt) { return LVATable[pt]; }

enum Stages {
	MAIN_SEARCH, CAPTURES_INIT, GOOD_CAPTURES, KILLERS, COUNTERMOVE, QUIET_INIT, QUIET, BAD_CAPTURES,
	EVASION, EVASIONS_INIT, ALL_EVASIONS,
	PROBCUT, PROBCUT_INIT, PROBCUT_CAPTURES,
	/*QSEARCH_WITH_CHECKS, QCAPTURES_1, CHECKS,*/
	QSEARCH_NO_CHECKS, QCAPTURES_INIT, QCAPTURES,
	QSEARCH_RECAPTURES, QRECAPTURES
};

  void insertion_sort(MoveStack* begin, MoveStack* end)
  {
    MoveStack tmp, *p, *q;

    for (p = begin + 1; p < end; ++p)
    {
        tmp = *p;
        for (q = p; q != begin && *(q-1) < tmp; --q)
            *q = *(q-1);
        *q = tmp;
    }
  }

  MoveStack pick_best(MoveStack* begin, MoveStack* end)
  {
      std::swap(*begin, *std::max_element(begin, end));
      return *begin;
  }

} // namespace

MovePicker::MovePicker(const Position& p, const Move ttm, const Depth d, Search::Stack* s)
	: pos(p), ss(s), depth(d)
{
	assert(Depth0 < d);

    Square prevSq = (ss-1)->currentMove.to();
    countermove = pos.thisThread()->counterMoves[pos.piece(prevSq)][prevSq];

    stage = pos.inCheck() ? EVASION : MAIN_SEARCH;
	ttMove = (!ttm.isNone() && pos.moveIsPseudoLegal(ttm) ? ttm : Move::moveNone());
	stage += ttMove.isNone();
}

// 静止探索で呼ばれる。
MovePicker::MovePicker(const Position& p, Move ttm, const Depth d, const Square sq)
	: pos(p)
{
	assert(d <= Depth0);

	if (pos.inCheck())
        stage = EVASION;

	// todo: ここで Stockfish は qcheck がある。

	else if (DepthQRecaptures < d)
        stage = QSEARCH_NO_CHECKS;

	else {
        stage = QSEARCH_RECAPTURES;
		recaptureSquare = sq;
		ttm = Move::moveNone();
		return;
	}

	ttMove = (!ttm.isNone() && pos.moveIsPseudoLegal(ttm) ? ttm : Move::moveNone());
    stage += ttMove.isNone();
}

MovePicker::MovePicker(const Position& p, const Move ttm, Score th)
	: pos(p), threshold(th)
{
	assert(!pos.inCheck());

	stage = PROBCUT;

	ttMove = !ttm.isNone()
		&& pos.moveIsPseudoLegal(ttm)
		&& ttm.isCapture()
		&& pos.see(ttm) > threshold ? ttm : MOVE_NONE;

	stage += ttMove.isNone();
}

void MovePicker::scoreCaptures() {
	for (auto& m : *this) {
		const Move move = m;
		m.score = Position::pieceScore(pos.piece(move.to())) - LVA(move.pieceTypeFrom());
	}
}

template <bool IsDrop> void MovePicker::scoreNonCapturesMinusPro() {
	const HistoryStats& history = pos.thisThread()->history;
#ifdef FROMTO
	const FromToStats& fromTo = pos.thisThread()->fromTo;
	Color c = pos.turn();
#endif

	const CounterMoveStats* cm = (ss - 1)->counterMoves;
	const CounterMoveStats* fm = (ss - 2)->counterMoves;
	const CounterMoveStats* f2 = (ss - 4)->counterMoves;

	for (auto& m : *this) {
		const Move move = m;

		m.score = history[pos.moved_piece(move)][move.to()]
			+ (cm ? (*cm)[pos.moved_piece(move)][move.to()] : ScoreZero)
			+ (fm ? (*fm)[pos.moved_piece(move)][move.to()] : ScoreZero)
			+ (f2 ? (*f2)[pos.moved_piece(move)][move.to()] : ScoreZero)
#ifdef FROMTO
			+ fromTo.get(c, m)
#endif
			;
	}
}

void MovePicker::scoreEvasions() {
	const HistoryStats& history = pos.thisThread()->history;
#ifdef FROMTO
	const FromToStats& fromTo = pos.thisThread()->fromTo;
	Color c = pos.turn();
#endif
	for (auto& m : *this) {
		const Move move = m;

		if (move.isCaptureOrPromotion()) {
			m.score = pos.capturePieceScore(pos.piece(move.to())) + HistoryStats::Max;
			if (move.isPromotion()) {
				const PieceType pt = pieceToPieceType(pos.piece(move.from()));
				m.score += pos.promotePieceScore(pt);
			}
		}
		else
			m.score = history.value(move.isDrop(), pos.moved_piece(move), move.to())
#ifdef FROMTO
			+ fromTo.get(c, move)
#endif
			;
	}
}

Move MovePicker::nextMove() {
	Move move;

	switch (stage) {

	case MAIN_SEARCH: case EVASION: case QSEARCH_NO_CHECKS:
	case PROBCUT:
		++stage;
		return ttMove;

	case CAPTURES_INIT:
		endBadCaptures = cur = moves;
		endMoves = generateMoves<CapturePlusPro>(cur, pos);
		scoreCaptures();
		++stage;

	case GOOD_CAPTURES:
		while (cur < endMoves)
		{
			move = pick_best(cur++, endMoves);
			if (move != ttMove) {
				if (ScoreZero <= pos.see(move))
					return move;

				// Losing capture, move it to the beginning of the array
				*endBadCaptures++ = move;
			}
		}
		++stage;

		move = ss->killers[0];  // First killer move
		if (!move.isNone()
			&& move != ttMove
			&& pos.moveIsPseudoLegal(move, true)
			&& pos.piece(move.to()) == Empty)
			return move;

	case KILLERS:
		++stage;
		move = ss->killers[1]; // Second killer move
		if (!move.isNone()
			&& move != ttMove
			&& pos.moveIsPseudoLegal(move, true)
			&& pos.piece(move.to()) == Empty)
			return move;

	case COUNTERMOVE:
		++stage;
		move = countermove;
		if (move != MOVE_NONE
			&& move != ttMove
			&& move != ss->killers[0]
			&& move != ss->killers[1]
			&& pos.moveIsPseudoLegal(move, true)
			&& pos.piece(move.to()) == Empty)
			return move;

	case QUIET_INIT:
		cur = endBadCaptures;
		endMoves = generateMoves<NonCaptureMinusPro>(cur, pos);
		scoreNonCapturesMinusPro<false>();
		cur = endMoves;
		endMoves = generateMoves<Drop>(cur, pos);
		scoreNonCapturesMinusPro<true>();
		cur = endBadCaptures;
		if (depth < static_cast<Depth>(3 * OnePly))
		{
			MoveStack* goodQuiet = std::partition(cur, endMoves, [](const MoveStack& m)
			                                      { return m.score > ScoreZero; });
			insertion_sort(cur, goodQuiet);
		}
		else
			insertion_sort(cur, endMoves);
		++stage;

	case QUIET:
		while (cur < endMoves)
		{
			move = *cur++;
			if (move != ttMove
				&& move != ss->killers[0]
				&& move != ss->killers[1]
				&& move != countermove)
				return move;
		}
		++stage;
		cur = moves; // Point to beginning of bad captures

	case BAD_CAPTURES:
		if (cur < endBadCaptures)
			return *cur++;
		break;

	case EVASIONS_INIT:
		cur = moves;
		endMoves = generateMoves<Evasion>(cur, pos);
		if (endMoves - moves > 1)
			scoreEvasions();
		++stage;

	case ALL_EVASIONS:
		while (cur < endMoves)
		{
			move = pick_best(cur++, endMoves);
			if (move != ttMove)
				return move;
		}
		break;

	case PROBCUT_INIT:
		cur = moves;
		endMoves = generateMoves<CapturePlusPro>(cur, pos);
		scoreCaptures();
		++stage;

	case PROBCUT_CAPTURES:
		while (cur < endMoves)
		{
			move = pick_best(cur++, endMoves);
			// todo: see が確実に駒打ちじゃないから、内部で駒打ちか判定してるのは少し無駄。
			if (move != ttMove && threshold < pos.see(move))
				return move;
		}
		break;

	case QCAPTURES_INIT:
		cur = moves;
		endMoves = generateMoves<CapturePlusPro>(cur, pos);
		scoreCaptures();
		++stage;

	case QCAPTURES:
		while (cur < endMoves)
		{
			move = pick_best(cur++, endMoves);
			if (move != ttMove)
				return move;
		}
		break;

	case QSEARCH_RECAPTURES:
		cur = moves;
		endMoves = generateMoves<Recapture>(moves, pos, recaptureSquare);
		scoreCaptures();
		++stage;

	case QRECAPTURES:
		while (cur < endMoves)
		{
			move = pick_best(cur++, endMoves);
			assert(move.to() == recaptureSquare);
			return move;
		}
		break;

	default:
		UNREACHABLE;
	}
	return Move::moveNone();
}
