#ifndef APERY_THREAD_HPP
#define APERY_THREAD_HPP

#include "common.hpp"
#include "evaluate.hpp"
#include "usi.hpp"
#include "tt.hpp"
#include "search.hpp"
#include "movePicker.hpp"

class Thread {

  std::thread nativeThread;
  Mutex mutex;
  ConditionVariable sleepCondition;
  bool exit, searching;

public:
  Thread();
  virtual ~Thread();
  virtual void search();
  void idle_loop();
  void start_searching(bool resume = false);
  void wait_for_search_finished();
  void wait(std::atomic_bool& b);

    size_t pvIdx;
	size_t idx;
    int maxPly, callsCnt, nmp_ply, nmp_odd;

    Position rootPos;
    Search::RootMoves rootMoves;
    Depth rootDepth;
    Depth completedDepth;
    std::atomic_bool resetCalls;

	// 近代的なMovePickerではオーダリングのために、スレッドごとにhistoryとcounter movesのtableを持たないといけない。
	CounterMoveHistory counterMoves;
	ButterflyHistory mainHistory;
	LowPlyHistory lowPlyHistory;
	CapturePieceToHistory captureHistory;

	// コア数が多いか、長い持ち時間においては、ContinuationHistoryもスレッドごとに確保したほうが良いらしい。
	// cf. https://github.com/official-stockfish/Stockfish/commit/5c58d1f5cb4871595c07e6c2f6931780b5ac05b5
	// continuationHistory[inCheck][Capture]
	ContinuationHistory continuationHistory[2][2];

	uint64_t ttHitAverage;

	// nmpMinPly : null moveの前回の適用ply
	// nmpColor  : null moveの前回の適用Color
	int nmpMinPly;
	Color nmpColor;
};

struct MainThread : public Thread {
  virtual void search();

  bool easyMovePlayed, failedLow;
  double bestMoveChanges;
  Score previousScore;
};

struct ThreadPool : public std::vector<Thread*> {

	void init();
	void exit();

	MainThread* main() { return static_cast<MainThread*>(at(0)); }
	void startThinking(const Position& pos, const Search::LimitsType& limits, const std::vector<Move>& searchMoves);
    void readUSIOptions();
    uint64_t nodes_searched();
};

extern ThreadPool Threads;

#endif // #ifndef APERY_THREAD_HPP
