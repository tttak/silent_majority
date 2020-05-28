#ifndef APERY_SEARCH_HPP
#define APERY_SEARCH_HPP

#include "move.hpp"
#include "pieceScore.hpp"
#include "tt.hpp"
#include "evaluate.hpp"
#include "YaneuraOu/movepick.h"

namespace Search {

	// countermoves based pruningで使う閾値
	constexpr int CounterMovePruneThreshold = 0;

struct Stack {
    Move* pv;
	Ply ply;
	Move currentMove;
	Move excludedMove; // todo: これは必要？
	Move killers[2];
	Score staticEval;
    int moveCount;
	EvalSum staticEvalRaw; // 評価関数の差分計算用、値が入っていないときは [0] を ScoreNotEvaluated にしておく。
						   // 常に Black 側から見た評価値を入れておく。
						   // 0: 双玉に対する評価値, 1: 先手玉に対する評価値, 2: 後手玉に対する評価値

	PieceToHistory* continuationHistory;// historyのうち、counter moveに関するhistoryへのポインタ。実体はThreadが持っている。
	int statScore;						// 一度計算したhistoryの合計値をcacheしておくのに用いる。
	bool inCheck;
};

struct RootMove {

    explicit RootMove(Move m) : pv(1, m) {}

	bool operator < (const RootMove& m) const { return m.score < score; }
	bool operator == (const Move& m) const { return pv[0] == m; }
    bool extractPonderFromTT(Position& pos);
#ifdef USE_extractPVFromTT
	void extractPVFromTT(Position& pos);
#endif

	Score score = -ScoreInfinite;
	Score previousScore = -ScoreInfinite;
	std::vector<Move> pv;
};

typedef std::vector<RootMove> RootMoves;

// 時間や探索深さの制限を格納する為の構造体
struct LimitsType {
    LimitsType() {
      nodes = time[White] = time[Black] = inc[White] = inc[Black] =
      npmsec = movesToGo = depth = moveTime = mate = infinite = ponder = 0;
    }

	bool useTimeManagement() const { 
		return !(mate | depth | nodes | moveTime | infinite); 
	}

	std::vector<Move> searchmoves;
	int time[ColorNum], inc[ColorNum], npmsec, movesToGo, depth, moveTime, mate, infinite, ponder;
	s64 nodes;
    TimePoint startTime;
};

struct SignalsType {
  std::atomic_bool stop, stopOnPonderhit;
};

  extern SignalsType Signals;
  extern LimitsType Limits;
  extern StateStackPtr SetUpStates;

  extern std::vector<Move> SearchMoves;

#if defined LEARN
	STATIC Score alpha;
	STATIC Score beta;
#endif
#if defined INANIWA_SHIFT
	STATIC InaniwaFlag inaniwaFlag;
#endif

	void init();
    void clear();
    Score evaluate(Position& pos, Search::Stack* ss);

}; // namespace Search

enum InaniwaFlag {
	NotInaniwa,
	InaniwaIsBlack,
	InaniwaIsWhite,
	InaniwaFlagNum
};

enum BishopInDangerFlag {
	NotBishopInDanger,
	BlackBishopInDangerIn28,
	WhiteBishopInDangerIn28,
	BlackBishopInDangerIn78,
	WhiteBishopInDangerIn78,
	BlackBishopInDangerIn38,
	WhiteBishopInDangerIn38,
	BishopInDangerFlagNum
};

#endif // #ifndef APERY_SEARCH_HPP
