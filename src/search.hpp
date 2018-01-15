#ifndef APERY_SEARCH_HPP
#define APERY_SEARCH_HPP

#include "move.hpp"
#include "pieceScore.hpp"
#include "tt.hpp"
#include "evaluate.hpp"

template<typename T> struct Stats;
typedef Stats<int> CounterMoveStats;

namespace Progress
{
    struct ProgressSum
    {
        ProgressSum() {};
        void set(int64_t b, int64_t w, Key key) { bkp = b; wkp = w; key_ = key;}
        double rate() const;
        bool isNone(Key key) const {
			if(key == 0) return ((bkp+wkp) == 0); // TODO..
			return key_ != key;
		}
        int64_t bkp, wkp;
		Key key_;
    };
}

namespace Search {

struct Stack {
    Move* pv;
	CounterMoveStats* counterMoves;
	Ply ply;
	Move currentMove;
	Move excludedMove; // todo: これは必要？
	Move killers[2];
	Score staticEval;
	int history;
    int moveCount;
	EvalSum staticEvalRaw; // 評価関数の差分計算用、値が入っていないときは [0] を ScoreNotEvaluated にしておく。
						   // 常に Black 側から見た評価値を入れておく。
						   // 0: 双玉に対する評価値, 1: 先手玉に対する評価値, 2: 後手玉に対する評価値
	Progress::ProgressSum progress;
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

namespace Progress
{
    void load(const std::string& dirName);
    void evaluate(const Position& pos, Search::Stack* ss, double rate[2]);
    void evaluate(const Position& pos, double rate[2]);
    void computeAll(const Position& pos, Search::Stack* ss, double rate[2]);
}

#endif // #ifndef APERY_SEARCH_HPP
