#include "search.hpp"
#include "position.hpp"
#include "usi.hpp"
#include "evaluate.hpp"
#include "movePicker.hpp"
#include "tt.hpp"
#include "generateMoves.hpp"
#include "thread.hpp"
#include "timeManager.hpp"
#include "book.hpp"
#include "YaneuraOu/misc.h"

namespace Search {

	SignalsType Signals;
	LimitsType Limits;
	StateStackPtr SetUpStates;

    std::vector<Move> SearchMoves;

#if defined LEARN
	STATIC Score alpha;
	STATIC Score beta;
#endif
#if defined INANIWA_SHIFT
	InaniwaFlag inaniwaFlag;
#endif
};

using namespace Search;

namespace {
    enum NodeType { NonPV, PV };

	constexpr uint64_t ttHitAverageWindow = 4096;
	constexpr uint64_t ttHitAverageResolution = 1024;

    // Sizes and phases of the skip-blocks, used for distributing search depths across the threads
    const int skipSize[] = { 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4 };
    const int skipPhase[] = { 0, 1, 0, 1, 2, 3, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 6, 7 };

	const Score Tempo = Score(20); // Must be visible to search
    //const int razorMargin[] = { 0, 570, 603, 554 };
    const int razorMargin = 531;
    inline Score futilityMargin(const Depth d, bool improving) { return Score(217 * (d / OnePly - improving));} // PARAM_FUTILITY_MARGIN_ALPHA 150 -> 147 -> 150
    //inline Score futilityMargin(const Depth d) { return Score(170 * d / OnePly);} // Yomita

	// 探索深さを減らすためのReductionテーブル
	int Reductions[MaxLegalMoves]; // [depth or moveNumber]

	// 残り探索深さをこの深さだけ減らす。
	// improvingとは、評価値が2手前から上がっているかのフラグ。上がっていないなら
	// 悪化していく局面なので深く読んでも仕方ないからreduction量を心もち増やす。
	Depth reduction(bool i, Depth d, int mn) {
		int r = Reductions[d / OnePly] * Reductions[mn];
		return ((r + 511) / 1024 + (!i && r > 1007)) * OnePly;
	}

    // Threshold used for countermoves based pruning.
    const int CounterMovePruneThreshold = 0;

	constexpr int futility_move_count(bool improving, int depth) {
		int d = depth / OnePly;
		return (4 + d * d) / (2 - improving);
	}

	// depthに基づく、historyとstatsのupdate bonus
	int stat_bonus(Depth d) {
		// Stockfish 9になって、move_picker.hのupdateで32倍していたのをやめたので、
		// ここでbonusの計算のときに32倍しておくことになった。
		return d > 15 ? -8 : 19 * d * d + 155 * d - 132;
	}

	struct Skill {
		Skill(int l) : level(l) {}
		bool enabled() const { return level < 20; }
		bool time_to_pick(Depth depth) const { return depth / OnePly == 1 + level; }
		Move best_move(size_t multiPV) { return best ? best : pick_best(multiPV); }
		Move pick_best(size_t multiPV);

		int level;
		Move best = MOVE_NONE;
	};

	// Breadcrumbs are used to mark nodes as being searched by a given thread.
	struct Breadcrumb {
		std::atomic<Thread*> thread;
		std::atomic<Key> key;
	};
	std::array<Breadcrumb, 1024> breadcrumbs;

	// ThreadHolding keeps track of which thread left breadcrumbs at the given node for potential reductions.
	// A free node will be marked upon entering the moves loop, and unmarked upon leaving that loop, by the ctor/dtor of this struct.
	struct ThreadHolding {
		explicit ThreadHolding(Thread* thisThread, Key posKey, int ply) {
			location = ply < 8 ? &breadcrumbs[posKey & (breadcrumbs.size() - 1)] : nullptr;
			otherThread = false;
			owning = false;
			if (location)
			{
			   // see if another already marked this location, if not, mark it ourselves.
				Thread* tmp = (*location).thread.load(std::memory_order_relaxed);
				if (tmp == nullptr)
				{
					(*location).thread.store(thisThread, std::memory_order_relaxed);
					(*location).key.store(posKey, std::memory_order_relaxed);
					owning = true;
				}
				else if (tmp != thisThread
						 && (*location).key.load(std::memory_order_relaxed) == posKey)
					otherThread = true;
			}
		}

		~ThreadHolding() {
			if (owning) // free the marked location.
				(*location).thread.store(nullptr, std::memory_order_relaxed);
		}

		bool marked() { return otherThread; }

	private:
		Breadcrumb* location;
		bool otherThread, owning;
	};

    struct EasyMoveManager {

      void clear() {
        stableCnt = 0;
        expectedPosKey = 0;
        pv[0] = pv[1] = pv[2] = MOVE_NONE;
      }

      Move get(Key key) const {
        return expectedPosKey == key ? pv[2] : MOVE_NONE;
      }

      void update(Position& pos, const std::vector<Move>& newPv) {

        assert(newPv.size() >= 3);

        // Keep track of how many times in a row 3rd ply remains stable
        stableCnt = (newPv[2] == pv[2]) ? stableCnt + 1 : 0;

        if (!std::equal(newPv.begin(), newPv.begin() + 3, pv))
        {
          std::copy(newPv.begin(), newPv.begin() + 3, pv);

          StateInfo st[2];
          pos.doMove(newPv[0], st[0]);
          pos.doMove(newPv[1], st[1]);
          expectedPosKey = pos.getKey();
          pos.undoMove(newPv[1]);
          pos.undoMove(newPv[0]);
        }
      }

      int stableCnt;
      Key expectedPosKey;
      Move pv[3];
    };

    EasyMoveManager EasyMove;
	Score DrawScore[ColorNum];

    template <NodeType NT>
    Score search(Position& pos, Search::Stack* ss, Score alpha, Score beta, const Depth depth, const bool cutNode);

    template <NodeType NT, bool INCHECK>
    Score qsearch(Position& pos, Search::Stack* ss, Score alpha, Score beta, const Depth depth = Depth0);

#if defined INANIWA_SHIFT
    void detectInaniwa(const Position& pos);
#endif

    void checkTime();

#if 1
	Move Skill::pick_best(size_t multiPV) {

		const RootMoves& rootMoves = Threads.main()->rootMoves;
		static PRNG rng(now());

		Score topScore = rootMoves[0].score;
		int delta = std::min(topScore - rootMoves[multiPV - 1].score, PawnScore);
		int weakness = 120 - 2 * level;
		int maxScore = -ScoreInfinite;

		for (size_t i = 0; i < multiPV; ++i)
		{
			int push = (weakness * int(topScore - rootMoves[i].score)
						+ delta * (rng.rand<unsigned>() % weakness)) / 128;

			if (rootMoves[i].score + push > maxScore)
			{
				maxScore = rootMoves[i].score + push;
				best = rootMoves[i].pv[0];
			}
		}

		return best;
	}
#endif
	Score scoreToTT(const Score s, const Ply ply) {
		assert(s != ScoreNone);

		return (ScoreMateInMaxPly <= s ? s + ply
				: s <= ScoreMatedInMaxPly ? s - ply
				: s);
	}

	Score scoreFromTT(const Score s, const Ply ply) {
		return (s == ScoreNone ? ScoreNone
				: ScoreMateInMaxPly <= s ? s - ply
				: s <= ScoreMatedInMaxPly ? s + ply
				: s);
	}

	void updatePV(Move* pv, Move move, Move* childPv) {

		for (*pv++ = move; childPv && *childPv != MOVE_NONE; )
			*pv++ = *childPv++;
		*pv = MOVE_NONE;
	}

	// update_continuation_histories()は、1,2,4手前の指し手と現在の指し手との指し手ペアによって
	// continuationHistoryを更新する。
	// 1手前に対する現在の指し手 ≒ counterMove  (応手)
	// 2手前に対する現在の指し手 ≒ followupMove (継続手)
	// 4手前に対する現在の指し手 ≒ followupMove (継続手)
	void update_continuation_histories(Stack* ss, Piece pc, Square to, int bonus)
	{
		for (int i : {1, 2, 4, 6})
		{
			if (ss->inCheck && i > 2)
				break;

			if ((ss - i)->currentMove.isOK())
				(*(ss - i)->continuationHistory)[to][pc] << bonus;
		}
	}

	// update_quiet_stats()は、新しいbest moveが見つかったときにmove soring heuristicsを更新する。
	// 具体的には駒を取らない指し手のstat tables、killer等を更新する。
	// move      = これが良かった指し手
	// quiets    = 悪かった指し手(このnodeで生成した指し手)
	// quietsCnt = ↑の数
	void update_quiet_stats(const Position& pos, Stack* ss, Move move, int bonus, Depth depth)
	{
		//   killerのupdate

		// killer 2本しかないので[0]と違うならいまの[0]を[1]に降格させて[0]と差し替え
		if (ss->killers[0] != move)
		{
			ss->killers[1] = ss->killers[0];
			ss->killers[0] = move;
		}

		//   historyのupdate
		Color us = pos.side_to_move();

		Thread* thisThread = pos.thisThread();
		thisThread->mainHistory[move.from_to()][us] << bonus;
		update_continuation_histories(ss, pos.movedPiece(move), move.to(), bonus);

		if ((ss - 1)->currentMove.isOK())
		{
			// 直前に移動させた升(その升に移動させた駒がある。今回の指し手はcaptureではないはずなので)
			Square prevSq = (ss - 1)->currentMove.to();
			thisThread->counterMoves[prevSq][pos.piece(prevSq)] = move;
		}

		if (depth > 12 * OnePly && ss->ply < MAX_LPH)
			thisThread->lowPlyHistory[ss->ply][move.from_to()] << stat_bonus(depth - 7 * OnePly);
	}

	// update_all_stats() updates stats at the end of search() when a bestMove is found
	void update_all_stats(const Position& pos, Stack* ss, Move bestMove, Value bestValue, Value beta, Square prevSq,
						  Move* quietsSearched, int quietCount, Move* capturesSearched, int captureCount, Depth depth) {
		int bonus1, bonus2;
		Color us = pos.side_to_move();
		Thread* thisThread = pos.thisThread();
		CapturePieceToHistory& captureHistory = thisThread->captureHistory;
		Piece moved_piece = pos.movedPiece(bestMove);
		PieceType captured = pieceToPieceType(pos.piece(bestMove.to()));

		bonus1 = stat_bonus(depth + 1 * OnePly);
		bonus2 = bestValue > beta + 128 /*PawnValueMg*/
			? bonus1               // larger bonus
			: stat_bonus(depth);   // smaller bonus

		if (!bestMove.isCaptureOrPromotion())
		{
			update_quiet_stats(pos, ss, bestMove, bonus2, depth);

			// Decrease all the non-best quiet moves
			for (int i = 0; i < quietCount; ++i)
			{
				thisThread->mainHistory[quietsSearched[i].from_to()][us] << -bonus2;
				update_continuation_histories(ss, pos.movedPiece(quietsSearched[i]), quietsSearched[i].to(), -bonus2);
			}
		}
		else
			captureHistory[bestMove.to()][moved_piece][captured] << bonus1;

		// Extra penalty for a quiet TT or main killer move in previous ply when it gets refuted
		if (((ss - 1)->moveCount == 1 || ((ss - 1)->currentMove == (ss - 1)->killers[0]))
			&& !pos.captured_piece())
			update_continuation_histories(ss - 1, pos.piece(prevSq), prevSq, -bonus1);

		// Decrease all the non-best capture moves
		for (int i = 0; i < captureCount; ++i)
		{
			moved_piece = pos.movedPiece(capturesSearched[i]);
			captured = pieceToPieceType(pos.piece(capturesSearched[i].to()));
			captureHistory[capturesSearched[i].to()][moved_piece][captured] << -bonus1;
		}
	}

	std::string scoreToUSI(const Score score) {
		std::stringstream ss;

		if (abs(score) < ScoreMateInMaxPly)
			// cp は centi pawn の略
			ss << "cp " << score * 100 / PawnScore;
		else
			// mate の後には、何手で詰むかを表示する。
			ss << "mate " << (0 < score ? ScoreMate0Ply - score : -ScoreMate0Ply - score);

		return ss.str();
	}

#if defined BISHOP_IN_DANGER
	BishopInDangerFlag detectBishopInDanger(const Position& pos) {
		if (pos.gamePly() <= 60) {
			const Color them = oppositeColor(pos.turn());
			if (pos.hand(pos.turn()).exists<HBishop>()
				&& pos.bbOf(Silver, them).isSet(inverseIfWhite(them, SQ27))
				&& (pos.bbOf(King  , them).isSet(inverseIfWhite(them, SQ48))
					|| pos.bbOf(King  , them).isSet(inverseIfWhite(them, SQ47))
					|| pos.bbOf(King  , them).isSet(inverseIfWhite(them, SQ59)))
				&& pos.bbOf(Pawn  , them).isSet(inverseIfWhite(them, SQ37))
				&& pos.piece(inverseIfWhite(them, SQ28)) == Empty
				&& pos.piece(inverseIfWhite(them, SQ38)) == Empty
				&& pos.piece(inverseIfWhite(them, SQ39)) == Empty)
			{
				return (pos.turn() == Black ? BlackBishopInDangerIn28 : WhiteBishopInDangerIn28);
			}
			else if (pos.hand(pos.turn()).exists<HBishop>()
					 && pos.hand(them).exists<HBishop>()
					 && pos.piece(inverseIfWhite(them, SQ78)) == Empty
					 && pos.piece(inverseIfWhite(them, SQ79)) == Empty
					 && pos.piece(inverseIfWhite(them, SQ68)) == Empty
					 && pos.piece(inverseIfWhite(them, SQ69)) == Empty
					 && pos.piece(inverseIfWhite(them, SQ98)) == Empty
					 && (pieceToPieceType(pos.piece(inverseIfWhite(them, SQ77))) == Silver
						 || pieceToPieceType(pos.piece(inverseIfWhite(them, SQ88))) == Silver)
					 && (pieceToPieceType(pos.piece(inverseIfWhite(them, SQ77))) == Knight
						 || pieceToPieceType(pos.piece(inverseIfWhite(them, SQ89))) == Knight)
					 && ((pieceToPieceType(pos.piece(inverseIfWhite(them, SQ58))) == Gold
						  && pieceToPieceType(pos.piece(inverseIfWhite(them, SQ59))) == King)
						 || pieceToPieceType(pos.piece(inverseIfWhite(them, SQ59))) == Gold))
			{
				return (pos.turn() == Black ? BlackBishopInDangerIn78 : WhiteBishopInDangerIn78);
			}
			else if (pos.hand(pos.turn()).exists<HBishop>()
					 && pos.hand(them).exists<HBishop>()
					 && pos.piece(inverseIfWhite(them, SQ38)) == Empty
					 && pos.piece(inverseIfWhite(them, SQ18)) == Empty
					 && pieceToPieceType(pos.piece(inverseIfWhite(them, SQ28))) == Silver
					 && (pieceToPieceType(pos.piece(inverseIfWhite(them, SQ58))) == King
						 || pieceToPieceType(pos.piece(inverseIfWhite(them, SQ57))) == King
						 || pieceToPieceType(pos.piece(inverseIfWhite(them, SQ58))) == Gold
						 || pieceToPieceType(pos.piece(inverseIfWhite(them, SQ57))) == Gold)
					 && (pieceToPieceType(pos.piece(inverseIfWhite(them, SQ59))) == King
						 || pieceToPieceType(pos.piece(inverseIfWhite(them, SQ59))) == Gold))
			{
				return (pos.turn() == Black ? BlackBishopInDangerIn38 : WhiteBishopInDangerIn38);
			}
		}
		return NotBishopInDanger;
	}
#endif

std::string pvInfoToUSI(Position& pos, const Depth depth, const Score alpha, const Score beta) {
	std::stringstream ss;
    const int t = Time.elapsed() + 1;
    const RootMoves& rootMoves = pos.thisThread()->rootMoves;
    size_t pvIdx = pos.thisThread()->pvIdx;
    size_t multiPV = std::min((size_t)Options["MultiPV"], rootMoves.size());
    uint64_t nodesSearched = Threads.nodes_searched();

    for (size_t i = multiPV - 1; 0 <= static_cast<int>(i); --i) {
		const bool update = (i <= pvIdx);

		if (depth == OnePly && !update)
			continue;

		const Depth d = (update ? depth : depth - OnePly);
		const Score s = (update ? rootMoves[i].score : rootMoves[i].previousScore);

        if (ss.rdbuf()->in_avail()) // Not at first line
            ss << "\n";

		ss << "info depth " << d / OnePly
		   << " seldepth " << pos.thisThread()->maxPly
           << " multipv " << i + 1
		   << " score " << scoreToUSI(s);

        if (i == pvIdx)
           ss << (s >= beta ? " lowerbound" : s <= alpha ? " upperbound" : "");

        ss << " nodes " << nodesSearched
           << " nps " << nodesSearched * 1000 / t;

        if (t > 1000) // Earlier makes little sense
            ss << " hashfull " << TT.hashfull();

		ss << " time " << t
		   << " pv";

		for (Move m : rootMoves[i].pv)
			ss << " " << m.toUSI();

		ss << std::endl;
	}
	return ss.str();
}
} // namespace

void Search::init() {

	for (int i = 1; i < MaxLegalMoves; ++i)
		Reductions[i] = int((24.8 + std::log(Threads.size())) * std::log(i));
}

void Search::clear() {

	TT.clear();

	for (Thread* th : Threads)
	{
		th->resetCalls = true;

		th->nmpMinPly = 0;

		th->counterMoves.fill(MOVE_NONE);
		th->mainHistory.fill(0);
		th->lowPlyHistory.fill(0);
		th->captureHistory.fill(0);

		// ここは、未初期化のときに[SQ_ZERO][NO_PIECE]を指すので、ここを-1で初期化しておくことによって、
		// history > 0 を条件にすれば自ずと未初期化のときは除外されるようになる。
		for (bool inCheck : { false, true })
			for (StatsType c : { NoCaptures, Captures })
			{
				for (auto& to : th->continuationHistory[inCheck][c])
					for (auto& h : to)
						h->fill(0);
				th->continuationHistory[inCheck][c][SQ_ZERO][NO_PIECE]->fill(Search::CounterMovePruneThreshold - 1);
			}
	}

	Threads.main()->previousScore = ScoreInfinite;
}

// 入玉勝ちかどうかを判定
bool nyugyoku(const Position& pos) {
	// CSA ルールでは、一 から 六 の条件を全て満たすとき、入玉勝ち宣言が出来る。
	// 判定が高速に出来るものから順に判定していく事にする。

	// 一 宣言側の手番である。

	// この関数を呼び出すのは自分の手番のみとする。ponder では呼び出さない。

	// 六 宣言側の持ち時間が残っている。

	// 持ち時間が無ければ既に負けなので、何もチェックしない。

	// 五 宣言側の玉に王手がかかっていない。
	if (pos.inCheck())
		return false;

	const Color us = pos.turn();
	// 敵陣のマスク
	const Bitboard opponentsField = (us == Black ? inFrontMask<Black, Rank4>() : inFrontMask<White, Rank6>());

	// 二 宣言側の玉が敵陣三段目以内に入っている。
	if (!pos.bbOf(King, us).andIsAny(opponentsField))
		return false;

	// 四 宣言側の敵陣三段目以内の駒は、玉を除いて10枚以上存在する。
	const int ownPiecesCount = (pos.bbOf(us) & opponentsField).popCount() - 1;
	if (ownPiecesCount < 10)
		return false;

	// 三 宣言側が、大駒5点小駒1点で計算して
	//     先手の場合28点以上の持点がある。
	//     後手の場合27点以上の持点がある。
	//     点数の対象となるのは、宣言側の持駒と敵陣三段目以内に存在する玉を除く宣言側の駒のみである。
	const int ownBigPiecesCount = (pos.bbOf(Rook, Dragon, Bishop, Horse) & opponentsField & pos.bbOf(us)).popCount();
	const Hand hand = pos.hand(us);
	const int val = ownPiecesCount + (ownBigPiecesCount + hand.numOf<HRook>() + hand.numOf<HBishop>()) * 4
		+ hand.numOf<HPawn>() + hand.numOf<HLance>() + hand.numOf<HKnight>()
		+ hand.numOf<HSilver>() + hand.numOf<HGold>();
#if defined LAW_24
	if (val < 31)
		return false;
#else
	if (val < (us == Black ? 28 : 27))
		return false;
#endif

	return true;
}

void MainThread::search() {
	static Book book;
    Position& pos = rootPos;
    Color us = pos.turn();
    bool isbook = false;
	Time.init(Limits, us, pos.gamePly());
	std::uniform_int_distribution<int> dist(Options["Min_Book_Ply"], Options["Max_Book_Ply"]);
	const Ply book_ply = dist(g_randomTimeSeed);
	bool nyugyokuWin = false;

	int contempt = Options["Contempt"] * PawnScore / 100; // From centipawns
	DrawScore[us] = ScoreDraw - Score(contempt);
	DrawScore[~us] = ScoreDraw + Score(contempt);

	// 指し手が無ければ負け
	if (rootMoves.empty()) {
		rootMoves.push_back(RootMove(Move::moveNone()));
		SYNCCOUT << "info depth 0 score "
			<< scoreToUSI(-ScoreMate0Ply)
			<< SYNCENDL;

		goto finalize;
	}

#if defined LEARN
#else
	if (nyugyoku(pos)) {
		nyugyokuWin = true;
		goto finalize;
	}
#endif

#if defined LEARN
	threads[0]->searching = true;
#else

	SYNCCOUT << "info string book_ply " << book_ply << SYNCENDL;
	if (Options["OwnBook"] && pos.gamePly() <= book_ply) {
		const std::tuple<Move, Score> bookMoveScore = book.probe(pos, Options["Book_File"], Options["Best_Book_Move"]);
		if (std::get<0>(bookMoveScore) && std::find(rootMoves.begin(),
															  rootMoves.end(),
															  std::get<0>(bookMoveScore)) != rootMoves.end())
		{
			std::swap(rootMoves[0], *std::find(rootMoves.begin(),
											   rootMoves.end(),
											   std::get<0>(bookMoveScore)));
			SYNCCOUT << "info"
					 << " score " << scoreToUSI(std::get<1>(bookMoveScore))
					 << " pv " << std::get<0>(bookMoveScore).toUSI()
					 << SYNCENDL;

            isbook = true;
			goto finalize;
		}
	}
#if defined BISHOP_IN_DANGER
	{
		auto deleteFunc = [](const std::string& str) {
			auto it = std::find_if(std::begin(rootMoves), std::end(rootMoves), [&str](const RootMove& rm) {
					return rm.pv_[0].toCSA() == str;
				});
			if (it != std::end(rootMoves))
				rootMoves.erase(it);
		};
		switch (detectBishopInDanger(pos)) {
		case NotBishopInDanger: break;
		case BlackBishopInDangerIn28: deleteFunc("0082KA"); break;
		case WhiteBishopInDangerIn28: deleteFunc("0028KA"); break;
		case BlackBishopInDangerIn78: deleteFunc("0032KA"); break;
		case WhiteBishopInDangerIn78: deleteFunc("0078KA"); break;
		case BlackBishopInDangerIn38: deleteFunc("0072KA"); break;
		case WhiteBishopInDangerIn38: deleteFunc("0038KA"); break;
		default: UNREACHABLE;
		}
	}
#endif

#if defined INANIWA_SHIFT
	detectInaniwa(pos);
#endif

    for (Thread* th : Threads)
    {
      th->maxPly = 0;
      th->rootDepth = Depth0;
      if (th != this)
      {
        //th->nmp_ply = th->nmp_odd = 0;
        th->rootPos = Position(rootPos, th);
        th->rootMoves = rootMoves;
        th->start_searching();
      }
    }
#endif
    Thread::search(); // Let's start searching!

#if defined LEARN
#else

finalize:
	if (Limits.npmsec)
		Time.availableNodes += Limits.inc[us] - Threads.nodes_searched();

	if (!Signals.stop && (Limits.ponder || Limits.infinite)) {
		Signals.stopOnPonderhit = true;
		wait(Signals.stop);
	}

    // Wait until all threads have finished
	for (Thread* th : Threads)
		if (th != this)
			th->wait_for_search_finished();

    // Check if there are threads with a better score than main thread
    Thread* bestThread = this;
	if (!this->easyMovePlayed
		&& !isbook
		&&  Options["MultiPV"] == 1
		&& !Limits.depth
		&& !Skill(Options["Skill_Level"]).enabled()
		&& rootMoves[0].pv[0] != MOVE_NONE)
	{
		std::map<Move, int64_t> votes;
		Score minScore = this->rootMoves[0].score;

		// Find out minimum score and reset votes for moves which can be voted
		for (Thread* th : Threads)
			minScore = std::min(minScore, th->rootMoves[0].score);

		for (Thread* th : Threads) {
			// ワーカースレッドのなかで最小を記録したスコアからの増分
			votes[th->rootMoves[0].pv[0]] +=
				(th->rootMoves[0].score - minScore + 14) * int(th->completedDepth);

			if (bestThread->rootMoves[0].score >= ScoreMateInMaxPly)
			{
				// Make sure we pick the shortest mate
				if (th->rootMoves[0].score > bestThread->rootMoves[0].score)
					bestThread = th;
			}
			else if (th->rootMoves[0].score >= ScoreMateInMaxPly
					 || votes[th->rootMoves[0].pv[0]] > votes[bestThread->rootMoves[0].pv[0]])
				bestThread = th;
		}
	}

    previousScore = bestThread->rootMoves[0].score;

    if (bestThread != this)
        SYNCCOUT << pvInfoToUSI(bestThread->rootPos, bestThread->completedDepth, -ScoreInfinite, ScoreInfinite) << SYNCENDL;

#ifdef RESIGN
    if (!isbook && previousScore < -Options["Resign"] 
		&& !Skill(Options["Skill_Level"]).enabled()) // 
        SYNCCOUT << "bestmove resign" << SYNCENDL;
#endif
    if (nyugyokuWin)
        SYNCCOUT << "bestmove win" << SYNCENDL;
    else if (!bestThread->rootMoves[0].pv[0])
        SYNCCOUT << "bestmove resign" << SYNCENDL;
    else {
        SYNCCOUT << "bestmove " << bestThread->rootMoves[0].pv[0].toUSI();
        if (bestThread->rootMoves[0].pv.size() > 1 || bestThread->rootMoves[0].extractPonderFromTT(pos))
            std::cout << " ponder " << bestThread->rootMoves[0].pv[1].toUSI();
        
        std::cout << SYNCENDL;
    }
#endif
}

void Thread::search() {

    Stack stack[MaxPly+10], *ss = stack+7;
    Score bestScore, alpha, beta, delta;
    Move easyMove = MOVE_NONE;
    MainThread* mainThread = (this == Threads.main() ? Threads.main() : nullptr);
    int lastInfoTime = -1; // 将棋所のコンソールが詰まる問題への対処用
	int pv_interval = Options["PvInterval"]; // PVの出力間隔[ms]

	std::memset(ss-7, 0, 10 * sizeof(Stack));
    for (int i = 7; i > 0; i--) {
        (ss-i)->continuationHistory = &this->continuationHistory[0][0][SQ_ZERO][NO_PIECE];
    }

    bestScore = delta = alpha = -ScoreInfinite;
    beta = ScoreInfinite;
    completedDepth = Depth0;

	if (mainThread)
	{
		easyMove = EasyMove.get(rootPos.getKey());
		EasyMove.clear();
		mainThread->easyMovePlayed = mainThread->failedLow = false;
		mainThread->bestMoveChanges = 0;
		TT.newSearch();
	}

#if defined LEARN
	// 高速化の為に浅い探索は反復深化しないようにする。学習時は浅い探索をひたすら繰り返す為。
	depth = std::max<Ply>(0, limits.depth - 1);
#else

#endif

	size_t multiPV = Options["MultiPV"];
	Skill skill(Options["Skill_Level"]);

	if (skill.enabled())
		multiPV = std::max(multiPV, (size_t)4);

	multiPV = std::min(multiPV, rootMoves.size());

	ttHitAverage = ttHitAverageWindow * ttHitAverageResolution / 2;

	// 反復深化で探索を行う。
	while ( (rootDepth = rootDepth + OnePly) < DepthMax
		   && !Signals.stop
		   && (!Limits.depth || Threads.main()->rootDepth / OnePly <= Limits.depth)) {

        // Distribute search depths across the threads
        if (idx) {
            int i = (idx - 1) % 20;
            if (((rootDepth / OnePly + rootPos.gamePly() + skipPhase[i]) / skipSize[i]) % 2)
                continue;
        }

		if (mainThread)
			mainThread->bestMoveChanges *= 0.505, mainThread->failedLow = false;

		// 前回の iteration の結果を全てコピー
		for (RootMove& rm : rootMoves)
			rm.previousScore = rm.score;

		// Multi PV loop
		for (pvIdx = 0; pvIdx < multiPV && !Signals.stop; ++pvIdx) {
#if defined LEARN
			alpha = this->alpha;
			beta = this->beta;
#else
			// aspiration search
			// alpha, beta をある程度絞ることで、探索効率を上げる。
			if (rootDepth >= 5 * OnePly) {
				delta = static_cast<Score>(21); // PARAM_ASPIRATION_SEARCH_DELTA 18 -> 16 -> 18
				alpha = std::max(rootMoves[pvIdx].previousScore - delta, -ScoreInfinite);
				beta  = std::min(rootMoves[pvIdx].previousScore + delta,  ScoreInfinite);
			}
#endif

			// aspiration search の window 幅を、初めは小さい値にして探索し、
			// fail high/low になったなら、今度は window 幅を広げて、再探索を行う。
			while (true) {
				// 探索を行う。
				(ss-1)->staticEvalRaw.p[0][0] = ss->staticEvalRaw.p[0][0] = ScoreNotEvaluated;
				bestScore = ::search<PV>(rootPos, ss, alpha, beta, rootDepth, false);
				// 先頭が最善手になるようにソート
				std::stable_sort(rootMoves.begin() + pvIdx, rootMoves.end());

#if 0
                // 詰みを発見したら即指す。
                if (!Limits.ponder && ScoreMateInMaxPly <= abs(bestScore) && abs(bestScore) < ScoreInfinite) {
                    SYNCCOUT << pvInfoToUSI(rootPos, rootDepth, alpha, beta) << SYNCENDL;
                    Signals.stop = true;
                }
#endif

#if defined LEARN
				break;
#endif

				if (Signals.stop)
					break;

#ifdef USE_extractPVFromTT
				if (mainThread
					&& multiPV == 1
					&& (bestScore <= alpha || bestScore >= beta)
					&& 3000 < Time.elapsed()
					// 将棋所のコンソールが詰まるのを防ぐ。
					&& (60000 < Time.elapsed() || lastInfoTime + 3000 < Time.elapsed()))
				{
					lastInfoTime = Time.elapsed();
					SYNCCOUT << pvInfoToUSI(rootPos, rootDepth, alpha, beta) << SYNCENDL;
				}
#endif
				// fail high/low のとき、aspiration window を広げる。
				if (bestScore <= alpha) {
					// 勝ち(負け)だと判定したら、最大の幅で探索を試してみる。
					beta = (alpha + beta) / 2;
					alpha = std::max(bestScore - delta, -ScoreInfinite);

					if (mainThread)
					{
						mainThread->failedLow = true;
						Signals.stopOnPonderhit = false;
					}
				}
				else if (bestScore >= beta) {
					alpha = (alpha + beta) / 2;
					beta = std::min(bestScore + delta, ScoreInfinite);
				}
				else
					break;

				delta += delta / 4 + 5;

				assert(-ScoreInfinite <= alpha && beta <= ScoreInfinite);
			}

			std::stable_sort(rootMoves.begin(), rootMoves.begin() + pvIdx + 1);

			if (!mainThread)
				continue;

			if ((Signals.stop || pvIdx + 1 == multiPV || 3000 < Time.elapsed())
			    // 将棋所のコンソールが詰まるのを防ぐ。
			    && (rootDepth < 4 || lastInfoTime + pv_interval < Time.elapsed()))
			{
				lastInfoTime = Time.elapsed();
				SYNCCOUT << pvInfoToUSI(rootPos, rootDepth, alpha, beta) << SYNCENDL;
			}
		}

		if (!Signals.stop)
			completedDepth = rootDepth;

		if (!mainThread)
			continue;

		if (skill.enabled() && skill.time_to_pick(rootDepth))
			skill.pick_best(multiPV);

#if 0
		// Have we found a "mate in x"?
		if (Limits.mate
			&& bestScore >= ScoreMateInMaxPly
			&& ScoreMate0Ply - bestScore <= 2 * Limits.mate)
			Signals.stop = true;

#endif

		if (Limits.useTimeManagement()) {
			if (!Signals.stop && !Signals.stopOnPonderhit) {

				const int F[] = { mainThread->failedLow,
				  bestScore - mainThread->previousScore };

				int improvingFactor = std::max(229, std::min(715, 357 + 119 * F[0] - 6 * F[1]));
				double unstablePvFactor = 1 + mainThread->bestMoveChanges;

				bool doEasyMove = rootMoves[0].pv[0] == easyMove
					&& mainThread->bestMoveChanges < 0.03
					&& Time.elapsed() > Time.optimum() * 5 / 44;

				if (rootMoves.size() == 1
					|| Time.elapsed() > Time.optimum() * unstablePvFactor * improvingFactor / 628
					|| (mainThread->easyMovePlayed = doEasyMove))
				{
					if (Limits.ponder)
						Signals.stopOnPonderhit = true;
					else
						Signals.stop = true;
				}
			}

			if (rootMoves[0].pv.size() >= 3)
				EasyMove.update(rootPos, rootMoves[0].pv);
			else
				EasyMove.clear();
		}
	}

	if (!mainThread)
		return;

	if (EasyMove.stableCnt < 6 || mainThread->easyMovePlayed)
		EasyMove.clear();

	if (skill.enabled())
		std::swap(rootMoves[0], *std::find(rootMoves.begin(),
										   rootMoves.end(), skill.best_move(multiPV)));

	//SYNCCOUT << pvInfoToUSI(rootPos, rootDepth-1, alpha, beta) << SYNCENDL;
}

#if defined INANIWA_SHIFT
// 稲庭判定
void Searcher::detectInaniwa(const Position& pos) {
	if (inaniwaFlag == NotInaniwa && 20 <= pos.gamePly()) {
		const Rank Trank3 = (pos.turn() == Black ? Rank3 : Rank7); // not constant
		const Bitboard mask = rankMask(Trank3) & ~fileMask<File9>() & ~fileMask<File1>();
		if ((pos.bbOf(Pawn, oppositeColor(pos.turn())) & mask) == mask) {
			inaniwaFlag = (pos.turn() == Black ? InaniwaIsWhite : InaniwaIsBlack);
			tt.clear();
		}
	}
}
#endif

namespace {
template <NodeType NT>
Score search(Position& pos, Stack* ss, Score alpha, Score beta, const Depth depth, const bool cutNode) {

    const bool PvNode = NT == PV;
    const bool rootNode = PvNode && (ss - 1)->ply == 0;

	assert(-ScoreInfinite <= alpha && alpha < beta && beta <= ScoreInfinite);
	assert(PvNode || (alpha == beta - 1));
	assert(Depth0 < depth && depth < DepthMax);
    assert(!(PvNode && cutNode));
    assert(depth / OnePly * OnePly == depth);

    Move pv[MaxPly+1], quietsSearched[64], capturesSearched[32];
	StateInfo st;
	TTEntry* tte;
	Key posKey;
	Move ttMove, move, excludedMove, bestMove;
	Depth extension, newDepth;
	Score bestScore, score, ttScore, eval;
    bool ttHit, inCheck, givesCheck, singularExtensionNode, improving;
	bool captureOrPawnPromotion, doFullDepthSearch, moveCountPruning, ttCapture;
    Piece movedPiece;
	int moveCount, quietCount, captureCount;
	Square prevSq;
	bool ttPv, formerPv, singularLMR, didLMR;

	// 残り探索深さが1手未満であるなら静止探索を呼び出す
	if (depth <= 0) {
		return pos.inCheck() ?
				  qsearch<NT, true >(pos, ss, alpha, beta)
				: qsearch<NT, false>(pos, ss, alpha, beta);
	}

	// step1
	// initialize node
    Thread* thisThread = pos.thisThread();
    ss->inCheck = inCheck = pos.inCheck();
    moveCount = quietCount = captureCount = ss->moveCount = 0;
	ss->statScore = 0;
    bestScore = -ScoreInfinite;
    ss->ply = (ss-1)->ply + 1;
	bool priorCapture = (pos.captured_piece() != Empty);
	Color us = pos.turn();

    if (thisThread->resetCalls.load(std::memory_order_relaxed))
    {
        thisThread->resetCalls = false;
		// At low node count increase the checking rate to about 0.1% of nodes
		// otherwise use a default value.
		thisThread->callsCnt = Limits.nodes ? std::min(4096, int(Limits.nodes / 1024))
			                                : 4096;
    }
    if (--thisThread->callsCnt <= 0)
    {
        for (Thread* th : Threads)
            th->resetCalls = true;

        checkTime();
    }

	if (PvNode && thisThread->maxPly < ss->ply)
		thisThread->maxPly = ss->ply;

	if (!rootNode) {
		// step2
		// stop と最大探索深さのチェック
		switch (pos.isDraw(16)) {
		case NotRepetition      : if (!Signals.stop.load(std::memory_order_relaxed) && ss->ply <= MaxPly) { break; }
		case RepetitionDraw     : return DrawScore[pos.turn()];
		case RepetitionWin      : return mateIn(ss->ply);
		case RepetitionLose     : return matedIn(ss->ply);
		case RepetitionSuperior : if (ss->ply != 2) { return ScoreMateInMaxPly; } break;
		case RepetitionInferior : if (ss->ply != 2) { return ScoreMatedInMaxPly; } break;
		default                 : UNREACHABLE;
		}

		// step3
		// mate distance pruning
		alpha = std::max(matedIn(ss->ply), alpha);
		beta = std::min(mateIn(ss->ply+1), beta);
		if (beta <= alpha)
			return alpha;
	}

    ss->currentMove = (ss+1)->excludedMove = bestMove = Move::moveNone();
	(ss+2)->killers[0] = (ss+2)->killers[1] = Move::moveNone();
	prevSq = (ss-1)->currentMove.to();

	if (rootNode)
		(ss + 4)->statScore = 0;
	else
		(ss + 2)->statScore = 0;

	// step4
	// trans position table lookup
	excludedMove = ss->excludedMove;
#if 1
	posKey = pos.getKey() ^ Key(excludedMove.value() << 1);
#else
	posKey = (!excludedMove ? pos.getKey() : pos.getExclusionKey());
#endif
	tte = TT.probe(posKey, ttHit);
    ttScore = (ttHit ? scoreFromTT(tte->score(), ss->ply) : ScoreNone);
	ttMove = (rootNode ? thisThread->rootMoves[thisThread->pvIdx].pv[0]
			  : ttHit ? move16toMove(tte->move(), pos)
			  : Move::moveNone());
	ttPv = PvNode || (ttHit && tte->is_pv());
	formerPv = ttPv && !PvNode;

	if (ttPv && depth > 12 * OnePly && ss->ply - 1 < MAX_LPH && !pos.captured_piece() && (ss - 1)->currentMove.isOK())
		thisThread->lowPlyHistory[ss->ply - 1][(ss - 1)->currentMove.from_to()] << stat_bonus(depth - 5 * OnePly);

	// thisThread->ttHitAverage can be used to approximate the running average of ttHit
	thisThread->ttHitAverage = (ttHitAverageWindow - 1) * thisThread->ttHitAverage / ttHitAverageWindow
							 + ttHitAverageResolution * ttHit;

	if (!PvNode
		&& ttHit
		&& tte->depth() >= depth
		&& ttScore != ScoreNone // アクセス競合が起きたときのみ、ここに引っかかる。
		&& (ttScore >= beta ? (tte->bound() & BoundLower)
			                : (tte->bound() & BoundUpper)))
	{
		if (ttMove)
		{
			if (ttScore >= beta)
			{
				if (!ttMove.isCapture())
					update_quiet_stats(pos, ss, ttMove, stat_bonus(depth), depth);

				// Extra penalty for a quiet TT move in previous ply when it gets refuted
				if ((ss-1)->moveCount == 1 && !priorCapture)
					update_continuation_histories(ss - 1, pos.piece(prevSq), prevSq, -stat_bonus(depth + OnePly));
			}
			// Penalty for a quiet ttMove that fails low
			else if (!ttMove.isCaptureOrPromotion()) // change
			{
				int penalty = -stat_bonus(depth);
				thisThread->mainHistory[ttMove.from_to()][us] << penalty;
				update_continuation_histories(ss, pos.movedPiece(ttMove), ttMove.to(), penalty);
			}
		}
		return ttScore;
	}

	if (!rootNode) {
		if (nyugyoku(pos)) {
    	    ss->staticEval = bestScore = mateIn(ss->ply);
    	    tte->save(posKey, scoreToTT(bestScore, ss->ply), ttPv, BoundExact, depth,
    	              Move::moveNone(), ss->staticEval, TT.generation());
    	    return bestScore;
		}
		if (!inCheck && (move = pos.mateMoveIn1Ply()) != MOVE_NONE) {
    		ss->staticEval = bestScore = mateIn(ss->ply);
    		tte->save(posKey, scoreToTT(bestScore, ss->ply), ttPv, BoundExact, depth,
					  move, ss->staticEval, TT.generation());
			return bestScore;
		}
	}

	CapturePieceToHistory& captureHistory = thisThread->captureHistory;

	// step5
	// evaluate the position statically
	eval = ss->staticEval = evaluate(pos, ss); // Bonanza の差分評価の為、evaluate() を常に呼ぶ。
	if (inCheck) {
		eval = ss->staticEval = ScoreNone;
		improving = false;
		goto moves_loop;
	}
	else if (ttHit) {
		if (ttScore != ScoreNone
			&& (tte->bound() & (eval < ttScore ? BoundLower : BoundUpper)))
		{
			eval = ttScore;
		}
	}
	else {
#if 0
        if ((ss-1)->currentMove == MOVE_NULL)
            eval = ss->staticEval = -(ss-1)->staticEval + 2 * Tempo;
#endif
		tte->save(posKey, ScoreNone, ttPv, BoundNone, DepthNone, Move::moveNone(), 
				  ss->staticEval, TT.generation());
	}

	// step6
	// razoring
	if (!rootNode
		&& depth == OnePly
		&& eval + razorMargin <= alpha)
		//&& eval + razorMargin[depth / OnePly] <= alpha)
	{
		return qsearch<NonPV, false>(pos, ss, alpha, beta);
	}

	improving = (ss - 2)->staticEval == ScoreNone ? (ss->staticEval > (ss - 4)->staticEval
			 || (ss - 4)->staticEval == ScoreNone) : ss->staticEval > (ss - 2)->staticEval;

	// step7
	// Futility pruning: child node (skipped when in check)
	if (!PvNode
		&& depth < 6 * OnePly // PARAM_FUTILITY_RETURN_DEPTH 7 -> 9
		&& eval - futilityMargin(depth, improving) >= beta
		&& eval < ScoreKnownWin)
	{
		return eval;
	}

	// step8
	// null move
	if (!PvNode
		&& (ss - 1)->currentMove != Move::moveNull()
		&& (ss - 1)->statScore < 23397
		&& eval >= beta
		&& eval >= ss->staticEval
		&& ss->staticEval >= beta - 32 * depth / OnePly - 30 * improving + 120 * ttPv + 292
		&& !excludedMove
		//&&  pos.non_pawn_material(us)
		&& (ss->ply >= thisThread->nmpMinPly || us != thisThread->nmpColor)
		)
	{
        assert(eval - beta >= 0);

        Depth R = ((854 // PARAM_NULL_MOVE_DYNAMIC_ALPHA 823 -> 818 -> 823
					+ 68 // PARAM_NULL_MOVE_DYNAMIC_BETA 67 -> 67
					* depth / OnePly) / 258 + std::min(int(eval - beta) / 192, 3)) * OnePly;

        ss->currentMove = Move::moveNull();
        ss->continuationHistory = &thisThread->continuationHistory[0][0][SQ_ZERO][NO_PIECE];

		pos.doNullMove<true>(st);
		(ss+1)->staticEvalRaw = (ss)->staticEvalRaw; // 評価値の差分評価の為。
		Score nullScore = (depth-R < OnePly ? -qsearch<NonPV, false>(pos, ss+1, -beta, -beta+1)
		                                    : -search<NonPV>(pos, ss+1, -beta, -beta+1, depth-R, !cutNode));
		pos.doNullMove<false>(st);

		if (nullScore >= beta) {
			if (nullScore >= ScoreMateInMaxPly)
				nullScore = beta;

			//if (abs(beta) < ScoreKnownWin && (depth < 12 * OnePly || thisThread->nmp_ply)) // PARAM_NULL_MOVE_RETURN_DEPTH 12 -> 14 -> 12
			if (thisThread->nmpMinPly || (abs(beta) < ScoreKnownWin && depth < 13 * OnePly)) // PARAM_NULL_MOVE_RETURN_DEPTH 12 -> 14 -> 12
				return nullScore;

			// thisThread->nmp_ply = ss->ply + 3 * (depth-R) / 4;
			// thisThread->nmp_odd = ss->ply % 2;
			thisThread->nmpMinPly = ss->ply + 3 * (depth - R) / OnePly / 4;
			thisThread->nmpColor = us;

			assert(Depth0 < depth - R);
			const Score s = (depth-R < OnePly ? qsearch<NonPV, false>(pos, ss, beta-1, beta)
                                              : search<NonPV>(pos, ss, beta-1, beta, depth-R, false));

			// thisThread->nmp_odd = thisThread->nmp_ply = 0;
			thisThread->nmpMinPly = 0;

			if (s >= beta)
				return nullScore;
		}
	}

	// step9
	// probcut
	if (!PvNode
		&& depth >= 5 * OnePly // PARAM_PROBCUT_DEPTH 5 -> 5
		&& abs(beta) < ScoreMateInMaxPly)
	{
		const Score rbeta = std::min(beta + 189 - 45 * improving, ScoreInfinite); // PARAM_PROBCUT_MARGIN 200 -> 194 -> 200
		const Depth rdepth = depth - 4 * OnePly;
		const Score threshold = rbeta - ss->staticEval;

		assert(rdepth >= OnePly);
		assert((ss-1)->currentMove.isOK());

		MovePicker mp(pos, ttMove, threshold, &captureHistory);
		const CheckInfo ci(pos);
		int probCutCount = 0;

		// 試行回数は3回までとする。(よさげな指し手を3つ試して駄目なら駄目という扱い)
		// cf. Do move-count pruning in probcut : https://github.com/official-stockfish/Stockfish/commit/b87308692a434d6725da72bbbb38a38d3cac1d5f
		while ((move = mp.nextMove()) != MOVE_NONE
				&& probCutCount < 2 + 2 * cutNode
				&& !(move == ttMove
					&& tte->depth() >= rdepth
					&& ttScore < rbeta))
		{
			if (move != excludedMove && pos.pseudoLegalMoveIsLegal<false, false>(move, ci.pinned)) {
				captureOrPawnPromotion = move.isCapture() /*true*/;
				probCutCount++;

				ss->currentMove = move;
				ss->continuationHistory = &thisThread->continuationHistory[ss->inCheck][captureOrPawnPromotion][move.to()][pos.movedPiece(move)];

				givesCheck = pos.moveGivesCheck(move, ci);

				pos.doMove(move, st, ci, givesCheck);
				(ss+1)->staticEvalRaw.p[0][0] = ScoreNotEvaluated;

				// この指し手がよさげであることを確認するための予備的なqsearch
				score = givesCheck ?
							  -qsearch<NonPV, true >(pos, ss + 1, -rbeta, -rbeta + 1)
							: -qsearch<NonPV, false>(pos, ss + 1, -rbeta, -rbeta + 1);

				if (score >= rbeta)
					score = -search<NonPV>(pos, ss + 1, -rbeta, -rbeta + 1, rdepth, !cutNode);

				pos.undoMove(move);

				if (score >= rbeta)
					return score;
			}
		}
	}

	// step10
	// internal iterative deepening
	if (depth >= 7 * OnePly
		&& !ttMove
		//&& (PvNode || (ss->staticEval + 256 >= beta))) // PARAM_IID_MARGIN_ALPHA 256 -> 251 -> 256
		)
	{
        //Depth d = (3 * depth / (4 * OnePly) - 2) * OnePly;
        Depth d = depth - 7 * OnePly;
		search<NT>(pos, ss, alpha, beta, d, cutNode);

		tte = TT.probe(posKey, ttHit);
		ttMove = (ttHit ? move16toMove(tte->move(), pos) : Move::moveNone());
		ttScore = (ttHit ? scoreFromTT(tte->score(), ss->ply) : ScoreNone);
	}

moves_loop:

	// contHist[0]  = Counter Move History    : ある指し手が指されたときの応手
	// contHist[1]  = Follow up Move History  : 2手前の自分の指し手の継続手
	// contHist[3]  = Follow up Move History2 : 4手前からの継続手
	const PieceToHistory* contHist[] = { (ss - 1)->continuationHistory, (ss - 2)->continuationHistory,
										  nullptr, (ss - 4)->continuationHistory,
										  nullptr, (ss - 6)->continuationHistory };

	Piece prevPc = pos.piece(prevSq);
	//Move countermove = thisThread->counterMoves[prevSq][prevPc];

	MovePicker mp(pos, ttMove, depth, ss,
					&thisThread->mainHistory, 
					&thisThread->lowPlyHistory, 
					&captureHistory, 
					contHist, 
					depth > 12 * OnePly ? ss->ply : MaxPly);

	const CheckInfo ci(pos);
	score = bestScore;

	// 指し手生成のときにquietの指し手を省略するか。
	singularLMR = moveCountPruning = false;

	ttCapture = ttMove && ttMove.isCaptureOrPawnPromotion();

	// Mark this node as being searched.
	ThreadHolding th(thisThread, posKey, ss->ply);

	// step11
	// Loop through moves
	while ((move = mp.nextMove(moveCountPruning)) != MOVE_NONE) {
		if (move == excludedMove)
			continue;

		if (rootNode && !std::count(thisThread->rootMoves.begin() + thisThread->pvIdx,
									thisThread->rootMoves.end(), move))
			continue;

		ss->moveCount = ++moveCount;

#if 0
		if (rootNode && thisThread == Threads.main() && 3000 < Timer.elapsed()) {
				SYNCCOUT << "info depth " << depth
						 << " currmove " << move.toUSI()
						 << " currmovenumber " << moveCount + pvIdx << SYNCENDL;
			}
#endif

        if (PvNode)
            (ss+1)->pv = nullptr;

		extension = Depth0;

		//captureOrPawnPromotion = move.isCaptureOrPromotion();
		captureOrPawnPromotion = move.isCapture();

        movedPiece = pos.movedPiece(move);
		givesCheck = pos.moveGivesCheck(move, ci);

		// この指し手による駒の移動先の升。historyの値などを調べたいのでいま求めてしまう。
		const Square movedSq = move.to();

		// Calculate new depth for this move
		newDepth = depth - OnePly;

		// step12

		// 浅い深さでの枝刈り
		if (!rootNode
			&& bestScore > ScoreMatedInMaxPly)
		{
			// move countベースの枝刈りを実行するかどうかのフラグ
			moveCountPruning = moveCount >= futility_move_count(improving, depth);

			// Reduced depth of the next LMR search
			// 次のLMR探索における軽減された深さ
			Depth lmrDepth = std::max(newDepth - reduction(improving, depth, moveCount), Depth0);

			if (!captureOrPawnPromotion && !givesCheck) {

				// Countermovesに基づいた枝刈り(historyの値が悪いものに関してはskip) : ~20 Elo
				if (lmrDepth < (4 + ((ss - 1)->statScore > 0 || (ss - 1)->moveCount == 1)) * OnePly
					&& ((*contHist[0])[movedSq][movedPiece] < CounterMovePruneThreshold)
					&& ((*contHist[1])[movedSq][movedPiece] < CounterMovePruneThreshold))
					continue;

				// Futility pruning: parent node : ~5 Elo
				// 親nodeの時点で子nodeを展開する前にfutilityの対象となりそうなら枝刈りしてしまう。
				if (lmrDepth < 6 * OnePly
					&& !ss->inCheck
					&& ss->staticEval + 235
						+ 172 * lmrDepth / OnePly <= alpha
					&&    (*contHist[0])[movedSq][movedPiece]
						+ (*contHist[1])[movedSq][movedPiece]
						+ (*contHist[3])[movedSq][movedPiece] < 27400)
					continue;

				// Prune moves with negative SEE : ~20 Elo
				// SEEが負の指し手を枝刈り
				if (!pos.seeGe(move, Score(-(32 - std::min(int(lmrDepth / OnePly), 18)) * (lmrDepth / OnePly) * (lmrDepth / OnePly))))
					continue;
			}
			else {
				// Capture history based pruning when the move doesn't give check
				if (!givesCheck
					&& lmrDepth < 1 * OnePly
					&& captureHistory[movedSq][movedPiece][pieceToPieceType(pos.piece(move.to()))] < 0)
					continue;

				// See based pruning
				// やねうら王の独自のコード。depthの2乗に比例したseeマージン。適用depthに制限なし。
				// しかしdepthの2乗に比例しているのでdepth 10ぐらいから無意味かと..
				// PARAM_FUTILITY_AT_PARENT_NODE_GAMMA2を少し大きめにして調整したほうがよさげ。
				if (!pos.seeGe(move, Value(-51 * (depth / OnePly) * (depth / OnePly))))
					continue;
			}
		}

		// Singular and Gives Check Extensions
		if ( depth >= 6 * OnePly
			&& move == ttMove
			&& !rootNode
			&& !excludedMove // 再帰的なsingular延長はすべきではない
			&& abs(ttScore) < ScoreKnownWin
			&& (tte->bound() & BoundLower)
			&& tte->depth() >= depth - 3 * OnePly
			&& pos.pseudoLegalMoveIsLegal<false, false>(move, ci.pinned))
		{
			// このmargin値は評価関数の性質に合わせて調整されるべき。
			Score singularBeta = ttScore - ((formerPv + 4) * int(depth / OnePly)) / 2;
			Depth singularDepth = ((int(depth / OnePly) - 1 + 3 * formerPv) / 2) * OnePly;

			ss->excludedMove = move;
			score = search<NonPV>(pos, ss, singularBeta-1, singularBeta, singularDepth, cutNode);
			ss->excludedMove = Move::moveNone();

			if (score < singularBeta) {
				extension = OnePly;
				singularLMR = true;
			}

			// Multi-cut pruning
			else if (singularBeta >= beta)
				return singularBeta;

			// If the eval of ttMove is greater than beta we try also if there is an other move that
			// pushes it over beta, if so also produce a cutoff
			else if (ttScore >= beta)
			{
				ss->excludedMove = move;
				score = search<NonPV>(pos, ss, beta - 1, beta, (depth + 3 * OnePly) / 2, cutNode);
				ss->excludedMove = Move::moveNone();

				if (score >= beta)
					return beta;
			}
		}

#if 0	// 王手延長 Yomita
        // 駒損しない王手と有効そうな王手は延長する。
        else if (givesCheck && !moveCountPruning)
            extension = pos.seeGe(move, ScoreZero) ? OnePly : OnePly / 2;
#else
        else if (givesCheck
                 //&& !moveCountPruning
                 &&  pos.seeGe(move, ScoreZero))
            extension = OnePly;
#endif

		newDepth += extension;

		// RootNode, SPNode はすでに合法手であることを確認済み。
		if (!rootNode && !pos.pseudoLegalMoveIsLegal<false, false>(move, ci.pinned)) {
			ss->moveCount = --moveCount;
			continue;
		}

		ss->currentMove = move;
		ss->continuationHistory = &thisThread->continuationHistory[ss->inCheck][captureOrPawnPromotion][movedSq][movedPiece];

		// step14
		pos.doMove(move, st, ci, givesCheck);
		(ss+1)->staticEvalRaw.p[0][0] = ScoreNotEvaluated;

		// step15
		// LMR
		if (depth >= 3 * OnePly
			&& moveCount > 1 + 2 * rootNode
			&& (!captureOrPawnPromotion
				|| moveCountPruning
				|| ss->staticEval + Position::capturePieceScore(pos.captured_piece()) <= alpha
				|| cutNode
				|| thisThread->ttHitAverage < 375 * ttHitAverageResolution * ttHitAverageWindow / 1024))
		{
			// Reduction量
			Depth r = reduction(improving, depth, moveCount);

			// Decrease reduction if the ttHit running average is large
			if (thisThread->ttHitAverage > 500 * ttHitAverageResolution * ttHitAverageWindow / 1024)
				r -= OnePly;

			// Reduction if other threads are searching this position.
			if (th.marked())
				r += OnePly;

			// Decrease reduction if position is or has been on the PV
			if (ttPv)
				r -= 2 * OnePly;

			if (moveCountPruning && !formerPv)
				r += OnePly;

			// Decrease reduction if opponent's move count is high (~10 Elo)
			if ((ss - 1)->moveCount > 14)
				r -= OnePly;

			// Decrease reduction if ttMove has been singularly extended
			if (singularLMR)
				r -= (1 + formerPv) * OnePly;

			if (!captureOrPawnPromotion) // ~5 Elo
			{
				// ~0 Elo
				if (ttCapture)
					r += OnePly;

				// ~5 Elo
				if (cutNode)
					r += 2 * OnePly;

				ss->statScore = thisThread->mainHistory[move.from_to()][pos.turn()]
					+ (*contHist[0])[movedSq][movedPiece]
					+ (*contHist[1])[movedSq][movedPiece]
					+ (*contHist[3])[movedSq][movedPiece]
					- 4926; // 修正項

				// ~ 10 Elo
				if (ss->statScore >= -102 && (ss - 1)->statScore < -114)
					r -= OnePly;
				else if ((ss - 1)->statScore >= -116 && ss->statScore < -154)
					r += OnePly;

				// ~30 Elo
				r -= (ss->statScore / 16434) * OnePly;
			}
			else
			{
				// Increase reduction for captures/promotions if late move and at low depth
				if (depth < 8 * OnePly && moveCount > 2)
					r += OnePly;

				// Unless giving check, this capture is likely bad
				if (!givesCheck
					&& ss->staticEval + Position::capturePieceScore(pos.captured_piece()) + 200 * depth / OnePly <= alpha)
					r += OnePly;
			}

			Depth d = Math::clamp(newDepth - r, OnePly, newDepth);
			score = -search<NonPV>(pos, ss+1, -(alpha+1), -alpha, d, true);

			// 上の探索によりalphaを更新しそうだが、いい加減な探索なので信頼できない。まともな探索で検証しなおす。
			doFullDepthSearch = (score > alpha) && (d != newDepth);
			didLMR = true;
		}
		else
		{
		    // non PVか、PVでも2手目以降であればfull depth searchを行なう。
			doFullDepthSearch = !PvNode || moveCount > 1;
			didLMR = false;
		}

		// step16
		// full depth search
		// PVS
		if (doFullDepthSearch) {
			score = (newDepth < OnePly ?
			  (givesCheck ? -qsearch<NonPV,  true>(pos, ss+1, -(alpha+1), -alpha)
					      : -qsearch<NonPV, false>(pos, ss+1, -(alpha+1), -alpha))
					      : - search<NonPV>(pos, ss+1, -(alpha+1), -alpha, newDepth, !cutNode));

			if (didLMR && !captureOrPawnPromotion)
			{
				int bonus = score > alpha ?  stat_bonus(newDepth)
					                      : -stat_bonus(newDepth);

				if (move == ss->killers[0])
					bonus += bonus / 4;

				update_continuation_histories(ss, movedPiece, move.to(), bonus);
			}
		}

		// 通常の探索
		if (PvNode && (moveCount == 1 || (alpha < score && (rootNode || score < beta)))) {
		//if (PvNode && (moveCount == 1 || score > alpha)) {

            (ss+1)->pv = pv;
            (ss+1)->pv[0] = MOVE_NONE;

			score = (newDepth < OnePly ?
			  (givesCheck ? -qsearch<PV,  true>(pos, ss+1, -beta, -alpha)
					      : -qsearch<PV, false>(pos, ss+1, -beta, -alpha))
					      : - search<PV>(pos, ss+1, -beta, -alpha, newDepth, false));
		}

		// step17
		pos.undoMove(move);

		assert(-ScoreInfinite < score && score < ScoreInfinite);

		// step18
		if (Signals.stop.load(std::memory_order_relaxed))
			return ScoreZero;

		if (rootNode) {
            RootMove& rm = *std::find(thisThread->rootMoves.begin(), thisThread->rootMoves.end(), move);

			if (moveCount == 1 || alpha < score) {
				// PV move or new best move
				rm.score = score;
#ifdef USE_extractPVFromTT
				rm.extractPVFromTT(pos);
#else
                rm.pv.resize(1);

                assert((ss+1)->pv);

                for (Move* m = (ss+1)->pv; *m != MOVE_NONE; ++m)
                    rm.pv.push_back(*m);
#endif
				if (moveCount > 1 && thisThread == Threads.main())
					++static_cast<MainThread*>(thisThread)->bestMoveChanges;
			}
			else
				rm.score = -ScoreInfinite;
		}

		if (score > bestScore) {
			bestScore = score;

			if (score > alpha) {

				bestMove = move;

				if (PvNode && !rootNode) // Update pv even in fail-high case
					updatePV(ss->pv, move, (ss+1)->pv);

				if (PvNode && score < beta)
					alpha = score;
				else {
					// fail high
					assert(score >= beta);
					ss->statScore = 0;
					break;
				}
			}
		}

		if (move != bestMove)
		{
			// 探索した駒を捕獲する指し手を32手目まで
			if (captureOrPawnPromotion && captureCount < 32)
				capturesSearched[captureCount++] = move;

			// 探索した駒を捕獲しない指し手を64手目までquietsSearchedに登録しておく。
			// あとでhistoryなどのテーブルに加点/減点するときに使う。
			if (!captureOrPawnPromotion && quietCount < 64)
				quietsSearched[quietCount++] = move;
		}
    }

	// step20
	if (moveCount == 0)
		bestScore = excludedMove ? alpha : matedIn(ss->ply);

    else if (bestMove)
		update_all_stats(pos, ss, bestMove, bestScore, beta, prevSq, quietsSearched, quietCount, capturesSearched, captureCount, depth);

	else if ((depth >= 3 * OnePly || PvNode)
			&& !priorCapture)
		update_continuation_histories(ss - 1, pos.piece(prevSq) , prevSq, stat_bonus(depth));

    //if (!excludedMove)
	if (!excludedMove && !(rootNode && thisThread->pvIdx))
        tte->save(posKey, scoreToTT(bestScore, ss->ply), ttPv,
                  bestScore >= beta ? BoundLower :
                  PvNode && bestMove ? BoundExact : BoundUpper,
                  depth, bestMove, ss->staticEval, TT.generation());

	assert(-ScoreInfinite < bestScore && bestScore < ScoreInfinite);

	return bestScore;
}

template <NodeType NT, bool INCHECK>
Score qsearch(Position& pos, Stack* ss, Score alpha, Score beta, const Depth depth) {
	const bool PVNode = (NT == PV);
    assert(NT == PV || NT == NonPV);
	assert(INCHECK == pos.inCheck());
	assert(-ScoreInfinite <= alpha && alpha < beta && beta <= ScoreInfinite);
	assert(PVNode || (alpha == beta - 1));
	assert(depth <= Depth0);
    assert(depth / OnePly * OnePly == depth);

    Move pv[MaxPly+1];
	StateInfo st;
	TTEntry* tte;
	Key posKey;
	Move ttMove, move, bestMove;
	Score bestScore, score, ttScore, futilityScore, futilityBase, oldAlpha;
    bool ttHit, givesCheck;
	Depth ttDepth;
	bool pvHit;

	ss->inCheck = INCHECK;
	Thread* thisThread = pos.thisThread();

    if (PVNode) {
        oldAlpha = alpha;
        (ss+1)->pv =pv;
        ss->pv[0] = MOVE_NONE;
    }

	ss->currentMove = bestMove = Move::moveNone();
	ss->ply = (ss-1)->ply + 1;

	if (ss->ply >= MaxPly)
		return DrawScore[pos.turn()];

	assert(0 <= ss->ply && ss->ply < MaxPly);

	ttDepth = ((INCHECK || DepthQChecks <= depth) ? DepthQChecks : DepthQNoChecks);

	posKey = pos.getKey();
	tte = TT.probe(posKey, ttHit);
	ttMove = (ttHit ? move16toMove(tte->move(), pos) : Move::moveNone());
	ttScore = (ttHit ? scoreFromTT(tte->score(), ss->ply) : ScoreNone);
	pvHit = ttHit && tte->is_pv();

	if (!PVNode
        && ttHit
		&& tte->depth() >= ttDepth
		&& ttScore != ScoreNone // アクセス競合が起きたときのみ、ここに引っかかる。
		&& (ttScore >= beta ? (tte->bound() & BoundLower)
			                : (tte->bound() & BoundUpper)))
	{
		return ttScore;
	}

	if (INCHECK) {
		ss->staticEval = ScoreNone;
		bestScore = futilityBase = -ScoreInfinite;
	}
	else {
		if ((move = pos.mateMoveIn1Ply()) != MOVE_NONE)
			return mateIn(ss->ply);

		if (ttHit) {
			if ((ss->staticEval = bestScore = tte->evalScore()) == ScoreNone)
				ss->staticEval = bestScore = evaluate(pos, ss);

			if (ttScore != ScoreNone)
				if (tte->bound() & (ttScore > bestScore ? BoundLower : BoundUpper))
					bestScore = ttScore;
		}
		else
			//ss->staticEval = bestScore = 
            //(((ss-1)->currentMove != MOVE_NULL) ? evaluate(pos, ss) 
            //                                    : -(ss-1)->staticEval + 2 * Tempo);
			ss->staticEval = bestScore = evaluate(pos, ss);

        // Stand pat
		if (bestScore >= beta) {
			if (!ttHit)
				tte->save(posKey, scoreToTT(bestScore, ss->ply), false, BoundLower,
						  DepthNone, Move::moveNone(), ss->staticEval, TT.generation());

			return bestScore;
		}

		if (PVNode && bestScore > alpha)
			alpha = bestScore;

		futilityBase = bestScore + 154; // PARAM_FUTILITY_MARGIN_QUIET 128 -> 145
	}

	evaluate(pos, ss);

	const PieceToHistory* contHist[] = { (ss - 1)->continuationHistory, (ss - 2)->continuationHistory,
										  nullptr, (ss - 4)->continuationHistory,
										  nullptr, (ss - 6)->continuationHistory };

	MovePicker mp(pos, ttMove, depth, (ss-1)->currentMove.to(), &pos.thisThread()->mainHistory, &pos.thisThread()->captureHistory, contHist);
	const CheckInfo ci(pos);

    while ((move = mp.nextMove()) != MOVE_NONE)
	{
		assert(pos.isOK());

		givesCheck = pos.moveGivesCheck(move, ci);
		bool captureOrPawnPromotion = move.isCapture();

		// futility pruning
		if (!INCHECK // 駒打ちは王手回避のみなので、ここで弾かれる。
			&& !givesCheck
			&& futilityBase > -ScoreKnownWin)
		{
			futilityScore = futilityBase + Position::capturePieceScore(pos.piece(move.to()));
			if (move.isPromotion())
				futilityScore += Position::promotePieceScore(move.pieceTypeFrom());

			if (futilityScore <= alpha) {
				bestScore = std::max(bestScore, futilityScore);
				continue;
			}

			if (futilityBase <= alpha && !pos.seeGe(move, ScoreZero + 1)) {
				bestScore = std::max(bestScore, futilityBase);
				continue;
			}
		}

		if (!INCHECK && !pos.seeGe(move, ScoreZero))
		{
			continue;
		}

		if (!pos.pseudoLegalMoveIsLegal<false, false>(move, ci.pinned))
			continue;

		ss->currentMove = move;
		ss->continuationHistory = &thisThread->continuationHistory[ss->inCheck][captureOrPawnPromotion][move.to()][pos.movedPiece(move)];

		pos.doMove(move, st, ci, givesCheck);
		(ss+1)->staticEvalRaw.p[0][0] = ScoreNotEvaluated;
		score = (givesCheck ? -qsearch<NT,  true>(pos, ss+1, -beta, -alpha, depth - OnePly)
				            : -qsearch<NT, false>(pos, ss+1, -beta, -alpha, depth - OnePly));
		pos.undoMove(move);

		assert(-ScoreInfinite < score && score < ScoreInfinite);

		if (score > bestScore) {
			bestScore = score;

			if (score > alpha) {
				bestMove = move;

                if (PVNode) // Update pv even in fail-high case
                    updatePV(ss->pv, move, (ss+1)->pv);

				if (PVNode && score < beta) {
					alpha = score;
				}
				else { // fail high
					//tte->save(posKey, scoreToTT(score, ss->ply), BoundLower,
					//		  ttDepth, move, ss->staticEval, TT.generation());
					//return score;
					break;
				}
			}
		}
	}

	if (INCHECK && bestScore == -ScoreInfinite)
		return matedIn(ss->ply);

	tte->save(posKey, scoreToTT(bestScore, ss->ply), pvHit,
			  ((PVNode && bestScore > oldAlpha) ? BoundExact : BoundUpper),
			  ttDepth, bestMove, ss->staticEval, TT.generation());

	assert(-ScoreInfinite < bestScore && bestScore < ScoreInfinite);

	return bestScore;
}

void checkTime() {
	const int elapsed = Time.elapsed();

    if (Limits.ponder)
		return;

    if (   (Limits.useTimeManagement() && elapsed > Time.maximum() - 10)
        || (Limits.moveTime && elapsed >= Limits.moveTime)
        || (Limits.nodes && Threads.nodes_searched() >= (uint64_t)Limits.nodes))
            Signals.stop = true;
}
} // namespace

bool RootMove::extractPonderFromTT(Position& pos)
{
    StateInfo st;
    bool ttHit;

    assert(pv.size() == 1);

    if (!pv[0].value())
        return false;

    pos.doMove(pv[0], st);
    TTEntry* tte = TT.probe(pos.getKey(), ttHit);

    if (tte != nullptr)
    {
        Move m = tte->move(); // Local copy to be SMP safe
        if (MoveList<Legal>(pos).contains(m))
            pv.push_back(m);
    }

    pos.undoMove(pv[0]);
    return pv.size() > 1;
}

#ifdef USE_extractPVFromTT
void RootMove::extractPVFromTT(Position& pos) {
	StateInfo state[MaxPly+10];
	StateInfo* st = state;
	TTEntry* tte;
	Ply ply = 0;
	Move m = pv[0];
	bool ttHit;

	assert(m && pos.moveIsPseudoLegal(m));

	pv.clear();

	do {
		pv.push_back(m);

		assert(pos.moveIsLegal(pv[ply]));
		pos.doMove(pv[ply++], *st++);
		tte = TT.probe(pos.getKey(), ttHit);
	} while (ttHit
			 // このチェックは少し無駄。駒打ちのときはmove16toMove() 呼ばなくて良い。
			 && pos.moveIsPseudoLegal(m = move16toMove(tte->move(), pos))
			 && pos.pseudoLegalMoveIsLegal<false, false>(m, pos.pinnedBB())
			 && ply < MaxPly
			 && (!pos.isDraw(20) || ply < 6));

	while (ply)
		pos.undoMove(pv[--ply]);
}
#endif
