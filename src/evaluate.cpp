#include "evaluate.hpp"
#include "position.hpp"
#include "search.hpp"
#include "thread.hpp"

KPPBoardIndexStartToPiece g_kppBoardIndexStartToPiece;

std::array<s16, 2> Evaluater::KPP[SquareNum][fe_end][fe_end];
std::array<s32, 2> Evaluater::KKP[SquareNum][SquareNum][fe_end];
std::array<s32, 2> Evaluater::KK[SquareNum][SquareNum];

EvaluateHashTable g_evalTable;
void prefetch_evalhash(const Key key)
{
	prefetch(g_evalTable[key >> 1]);
}

const int kppArray[31] = {
	0,        f_pawn,   f_lance,  f_knight,
	f_silver, f_bishop, f_rook,   f_gold,   
	0,        f_gold,   f_gold,   f_gold,
	f_gold,   f_horse,  f_dragon,
	0,
	0,        e_pawn,   e_lance,  e_knight,
	e_silver, e_bishop, e_rook,   e_gold,   
	0,        e_gold,   e_gold,   e_gold,
	e_gold,   e_horse,  e_dragon
};

const int kppHandArray[ColorNum][HandPieceNum] = {
	{f_hand_pawn, f_hand_lance, f_hand_knight, f_hand_silver,
	 f_hand_gold, f_hand_bishop, f_hand_rook},
	{e_hand_pawn, e_hand_lance, e_hand_knight, e_hand_silver,
	 e_hand_gold, e_hand_bishop, e_hand_rook}
};

namespace {
	EvalSum doapc(const Position& pos, const int index[2]) {
		const Square sq_bk = pos.kingSquare(Black);
		const Square sq_wk = pos.kingSquare(White);
		const int* list0 = pos.cplist0();
		const int* list1 = pos.cplist1();

		EvalSum sum;
		sum.p[0] = { 0, 0 };
		sum.p[1] = { 0, 0 };
		sum.p[2][0] = Evaluater::KKP[sq_bk][sq_wk][index[0]][0];
		sum.p[2][1] = Evaluater::KKP[sq_bk][sq_wk][index[0]][1];
		const auto* pkppb = Evaluater::KPP[sq_bk         ][index[0]];
		const auto* pkppw = Evaluater::KPP[inverse(sq_wk)][index[1]];
#if defined USE_AVX2_EVAL
		__m256i zero = _mm256_setzero_si256();
		__m256i sum0 = zero;
		__m256i sum1 = zero;
		for (int i = 0; i < pos.nlist(); i += 8){
			__m256i indexes0 = _mm256_loadu_si256((const __m256i*)(&list0[i]));
			__m256i indexes1 = _mm256_loadu_si256((const __m256i*)(&list1[i]));
			__m256i w0 = _mm256_i32gather_epi32(reinterpret_cast<const int*>(pkppb), indexes0, 4);
			__m256i w1 = _mm256_i32gather_epi32(reinterpret_cast<const int*>(pkppw), indexes1, 4);

			__m256i w0lo = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(w0, 0));
			sum0 = _mm256_add_epi32(sum0, w0lo);
			__m256i w0hi = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(w0, 1));
			sum0 = _mm256_add_epi32(sum0, w0hi);

			__m256i w1lo = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(w1, 0));
			sum1 = _mm256_add_epi32(sum1, w1lo);
			__m256i w1hi = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(w1, 1));
			sum1 = _mm256_add_epi32(sum1, w1hi);
        }

		sum0 = _mm256_add_epi32(sum0, _mm256_srli_si256(sum0, 8));
		__m128i sum0_128 = _mm_add_epi32(_mm256_extracti128_si256(sum0, 0), _mm256_extracti128_si256(sum0, 1));
		std::array<int32_t, 2> sum0_array;
		_mm_storel_epi64(reinterpret_cast<__m128i*>(&sum0_array), sum0_128);
		sum.p[0] += sum0_array;

		sum1 = _mm256_add_epi32(sum1, _mm256_srli_si256(sum1, 8));
		__m128i sum1_128 = _mm_add_epi32(_mm256_extracti128_si256(sum1, 0), _mm256_extracti128_si256(sum1, 1));
		std::array<int32_t, 2> sum1_array;
		_mm_storel_epi64(reinterpret_cast<__m128i*>(&sum1_array), sum1_128);
		sum.p[1] += sum1_array;
#elif defined USE_SSE_EVAL
		sum.m[0] = _mm_set_epi32(0, 0, *reinterpret_cast<const s32*>(&pkppw[list1[0]][0]), *reinterpret_cast<const s32*>(&pkppb[list0[0]][0]));
		sum.m[0] = _mm_cvtepi16_epi32(sum.m[0]);
		for (int i = 1; i < pos.nlist(); ++i) {
			__m128i tmp;
			tmp = _mm_set_epi32(0, 0, *reinterpret_cast<const s32*>(&pkppw[list1[i]][0]), *reinterpret_cast<const s32*>(&pkppb[list0[i]][0]));
			tmp = _mm_cvtepi16_epi32(tmp);
			sum.m[0] = _mm_add_epi32(sum.m[0], tmp);
		}
#else
		sum.p[0][0] = pkppb[list0[0]][0];
		sum.p[0][1] = pkppb[list0[0]][1];
		sum.p[1][0] = pkppw[list1[0]][0];
		sum.p[1][1] = pkppw[list1[0]][1];
		for (int i = 1; i < pos.nlist(); ++i) {
			sum.p[0] += pkppb[list0[i]];
			sum.p[1] += pkppw[list1[i]];
		}
#endif

		return sum;
	}
	std::array<s32, 2> doablack(const Position& pos, const int index[2]) {
		const Square sq_bk = pos.kingSquare(Black);
		const int* list0 = pos.cplist0();

		const auto* pkppb = Evaluater::KPP[sq_bk         ][index[0]];
		std::array<s32, 2> sum = {{pkppb[list0[0]][0], pkppb[list0[0]][1]}};
		for (int i = 1; i < pos.nlist(); ++i) {
			sum[0] += pkppb[list0[i]][0];
			sum[1] += pkppb[list0[i]][1];
		}
		return sum;
	}
	std::array<s32, 2> doawhite(const Position& pos, const int index[2]) {
		const Square sq_wk = pos.kingSquare(White);
		const int* list1 = pos.cplist1();

		const auto* pkppw = Evaluater::KPP[inverse(sq_wk)][index[1]];
		std::array<s32, 2> sum = {{pkppw[list1[0]][0], pkppw[list1[0]][1]}};
		for (int i = 1; i < pos.nlist(); ++i) {
			sum[0] += pkppw[list1[i]][0];
			sum[1] += pkppw[list1[i]][1];
		}
		return sum;
	}

#if defined INANIWA_SHIFT
	Score inaniwaScoreBody(const Position& pos) {
		Score score = ScoreZero;
		if (pos.csearcher()->inaniwaFlag == InaniwaIsBlack) {
			if (pos.piece(SQ81) == WKnight) score += 700 * FVScale;
			if (pos.piece(SQ21) == WKnight) score += 700 * FVScale;
			if (pos.piece(SQ93) == WKnight) score += 700 * FVScale;
			if (pos.piece(SQ13) == WKnight) score += 700 * FVScale;
			if (pos.piece(SQ73) == WKnight) score += 400 * FVScale;
			if (pos.piece(SQ33) == WKnight) score += 400 * FVScale;
			if (pos.piece(SQ85) == WKnight) score += 700 * FVScale;
			if (pos.piece(SQ25) == WKnight) score += 700 * FVScale;
			if (pos.piece(SQ65) == WKnight) score += 100 * FVScale;
			if (pos.piece(SQ45) == WKnight) score += 100 * FVScale;
			if (pos.piece(SQ57) == BPawn)   score += 200 * FVScale;
			if (pos.piece(SQ56) == BPawn)   score += 200 * FVScale;
			if (pos.piece(SQ55) == BPawn)   score += 200 * FVScale;
		}
		else {
			assert(pos.csearcher()->inaniwaFlag == InaniwaIsWhite);
			if (pos.piece(SQ89) == BKnight) score -= 700 * FVScale;
			if (pos.piece(SQ29) == BKnight) score -= 700 * FVScale;
			if (pos.piece(SQ97) == BKnight) score -= 700 * FVScale;
			if (pos.piece(SQ17) == BKnight) score -= 700 * FVScale;
			if (pos.piece(SQ77) == BKnight) score -= 400 * FVScale;
			if (pos.piece(SQ37) == BKnight) score -= 400 * FVScale;
			if (pos.piece(SQ85) == BKnight) score -= 700 * FVScale;
			if (pos.piece(SQ25) == BKnight) score -= 700 * FVScale;
			if (pos.piece(SQ65) == BKnight) score -= 100 * FVScale;
			if (pos.piece(SQ45) == BKnight) score -= 100 * FVScale;
			if (pos.piece(SQ53) == WPawn)   score -= 200 * FVScale;
			if (pos.piece(SQ54) == WPawn)   score -= 200 * FVScale;
			if (pos.piece(SQ55) == WPawn)   score -= 200 * FVScale;
		}
		return score;
	}
	inline Score inaniwaScore(const Position& pos) {
		if (pos.csearcher()->inaniwaFlag == NotInaniwa) return ScoreZero;
		return inaniwaScoreBody(pos);
	}
#endif

	bool calcDifference(Position& pos, Search::Stack* ss) {
#if defined INANIWA_SHIFT
		if (pos.csearcher()->inaniwaFlag != NotInaniwa) return false;
#endif
		if ((ss-1)->staticEvalRaw.p[0][0] == ScoreNotEvaluated)
			return false;

		const Move lastMove = (ss-1)->currentMove;
		assert(lastMove != Move::moveNull());

		if (lastMove.pieceTypeFrom() == King) {
			EvalSum diff = (ss-1)->staticEvalRaw; // 本当は diff ではないので名前が良くない。
			const Square sq_bk = pos.kingSquare(Black);
			const Square sq_wk = pos.kingSquare(White);
			diff.p[2] = Evaluater::KK[sq_bk][sq_wk];
			diff.p[2][0] += pos.material() * FVScale;
			if (pos.turn() == Black) {
				const auto* ppkppw = Evaluater::KPP[inverse(sq_wk)];
				const int* list1 = pos.plist1();
				diff.p[1][0] = 0;
				diff.p[1][1] = 0;

#if defined USE_AVX2_EVAL
				const int* list0 = pos.plist0();
				__m256i zero = _mm256_setzero_si256();
				__m256i diffp1 = zero;
				#pragma unroll
				for (int i = 0; i < pos.nlist() ; ++i)
				{
					diff.p[2] += Evaluater::KKP[sq_bk][sq_wk][list0[i]];
					const int k1 = list1[i];
					const auto* pkppw = ppkppw[k1];
					int j = 0;
					for (; j + 8 < i; j += 8)
					{
						__m256i indexes = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&list1[j]));
						__m256i w = _mm256_i32gather_epi32(reinterpret_cast<const int*>(pkppw), indexes, 4);
						__m256i wlo = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(w, 0));
						diffp1 = _mm256_add_epi32(diffp1, wlo);
						__m256i whi = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(w, 1));
						diffp1 = _mm256_add_epi32(diffp1, whi);
					}

					for (; j + 4 < i; j += 4)
					{
						__m128i indexes = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&list1[j]));
						__m128i w = _mm_i32gather_epi32(reinterpret_cast<const int*>(pkppw), indexes, 4);
						__m256i wlo = _mm256_cvtepi16_epi32(w);
						diffp1 = _mm256_add_epi32(diffp1, wlo);
					}

					for (; j < i; ++j)
					{
						const int l1 = list1[j];
						diff.p[1] += pkppw[l1];
					}
				}

				diffp1 = _mm256_add_epi32(diffp1, _mm256_srli_si256(diffp1, 8));
				__m128i diffp1_128 = _mm_add_epi32(_mm256_extracti128_si256(diffp1, 0), _mm256_extracti128_si256(diffp1, 1));
				std::array<int32_t, 2> diffp1_sum;
				_mm_storel_epi64(reinterpret_cast<__m128i*>(&diffp1_sum), diffp1_128);
				diff.p[1] += diffp1_sum;
#else
				for (int i = 0; i < pos.nlist(); ++i) {
					const int k1 = list1[i];
					const auto* pkppw = ppkppw[k1];
					for (int j = 0; j < i; ++j) {
						const int l1 = list1[j];
						diff.p[1] += pkppw[l1];
					}
					diff.p[2][0] -= Evaluater::KKP[inverse(sq_wk)][inverse(sq_bk)][k1][0];
					diff.p[2][1] += Evaluater::KKP[inverse(sq_wk)][inverse(sq_bk)][k1][1];
				}
#endif

				if (pos.cl().size == 2) {
					const int listIndex_cap = pos.cl().listindex[1];
					diff.p[0] += doablack(pos, pos.cl().clistpair[1].newlist);
					pos.plist0()[listIndex_cap] = pos.cl().clistpair[1].oldlist[0];
					diff.p[0] -= doablack(pos, pos.cl().clistpair[1].oldlist);
					pos.plist0()[listIndex_cap] = pos.cl().clistpair[1].newlist[0];
				}
			}
			else {
				const auto* ppkppb = Evaluater::KPP[sq_bk         ];
				const int* list0 = pos.plist0();
				diff.p[0][0] = 0;
				diff.p[0][1] = 0;

#if defined USE_AVX2_EVAL
				__m256i zero = _mm256_setzero_si256();
				__m256i diffp0 = zero;
				#pragma unroll
				for (int i = 0; i < pos.nlist(); ++i)
				{
					const int k0 = list0[i];
					const auto* pkppb = ppkppb[k0];
					diff.p[2] += Evaluater::KKP[sq_bk][sq_wk][k0];
					int j = 0;
					for (; j + 8 < i; j += 8)
					{
						__m256i indexes = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&list0[j]));
						__m256i w = _mm256_i32gather_epi32(reinterpret_cast<const int*>(pkppb), indexes, 4);
						__m256i wlo = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(w, 0));
						diffp0 = _mm256_add_epi32(diffp0, wlo);
						__m256i whi = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(w, 1));
						diffp0 = _mm256_add_epi32(diffp0, whi);
					}

					for (; j + 4 < i; j += 4)
					{
						__m128i indexes = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&list0[j]));
						__m128i w = _mm_i32gather_epi32(reinterpret_cast<const int*>(pkppb), indexes, 4);
						__m256i wlo = _mm256_cvtepi16_epi32(w);
						diffp0 = _mm256_add_epi32(diffp0, wlo);
					}

					for (; j < i; ++j)
					{
						const int l0 = list0[j];
						diff.p[0] += pkppb[l0];
					}
				}

				diffp0 = _mm256_add_epi32(diffp0, _mm256_srli_si256(diffp0, 8));
				__m128i diffp0_128 = _mm_add_epi32(_mm256_extracti128_si256(diffp0, 0), _mm256_extracti128_si256(diffp0, 1));
				std::array<int32_t, 2> diffp0_sum;
				_mm_storel_epi64(reinterpret_cast<__m128i*>(&diffp0_sum), diffp0_128);
				diff.p[0] += diffp0_sum;
#else
				for (int i = 0; i < pos.nlist(); ++i) {
					const int k0 = list0[i];
					const auto* pkppb = ppkppb[k0];
					for (int j = 0; j < i; ++j) {
						const int l0 = list0[j];
						diff.p[0] += pkppb[l0];
					}
					diff.p[2] += Evaluater::KKP[sq_bk][sq_wk][k0];
				}
#endif

				if (pos.cl().size == 2) {
					const int listIndex_cap = pos.cl().listindex[1];
					diff.p[1] += doawhite(pos, pos.cl().clistpair[1].newlist);
					pos.plist1()[listIndex_cap] = pos.cl().clistpair[1].oldlist[1];
					diff.p[1] -= doawhite(pos, pos.cl().clistpair[1].oldlist);
					pos.plist1()[listIndex_cap] = pos.cl().clistpair[1].newlist[1];
				}
			}
			ss->staticEvalRaw = diff;
		}
		else {
#if defined USE_AVX2_EVAL
			pos.plist0()[38] = pos.plist0()[39] = pos.plist1()[38] = pos.plist1()[39] = 0;
#endif
			const int listIndex = pos.cl().listindex[0];
			auto diff = doapc(pos, pos.cl().clistpair[0].newlist);
			if (pos.cl().size == 1) {
				pos.plist0()[listIndex] = pos.cl().clistpair[0].oldlist[0];
				pos.plist1()[listIndex] = pos.cl().clistpair[0].oldlist[1];
				diff -= doapc(pos, pos.cl().clistpair[0].oldlist);
			}
			else {
				assert(pos.cl().size == 2);
				diff += doapc(pos, pos.cl().clistpair[1].newlist);
				diff.p[0] -= Evaluater::KPP[pos.kingSquare(Black)         ][pos.cl().clistpair[0].newlist[0]][pos.cl().clistpair[1].newlist[0]];
				diff.p[1] -= Evaluater::KPP[inverse(pos.kingSquare(White))][pos.cl().clistpair[0].newlist[1]][pos.cl().clistpair[1].newlist[1]];
				const int listIndex_cap = pos.cl().listindex[1];
				pos.plist0()[listIndex_cap] = pos.cl().clistpair[1].oldlist[0];
				pos.plist1()[listIndex_cap] = pos.cl().clistpair[1].oldlist[1];

				pos.plist0()[listIndex] = pos.cl().clistpair[0].oldlist[0];
				pos.plist1()[listIndex] = pos.cl().clistpair[0].oldlist[1];
				diff -= doapc(pos, pos.cl().clistpair[0].oldlist);

				diff -= doapc(pos, pos.cl().clistpair[1].oldlist);
				diff.p[0] += Evaluater::KPP[pos.kingSquare(Black)         ][pos.cl().clistpair[0].oldlist[0]][pos.cl().clistpair[1].oldlist[0]];
				diff.p[1] += Evaluater::KPP[inverse(pos.kingSquare(White))][pos.cl().clistpair[0].oldlist[1]][pos.cl().clistpair[1].oldlist[1]];
				pos.plist0()[listIndex_cap] = pos.cl().clistpair[1].newlist[0];
				pos.plist1()[listIndex_cap] = pos.cl().clistpair[1].newlist[1];
			}
			pos.plist0()[listIndex] = pos.cl().clistpair[0].newlist[0];
			pos.plist1()[listIndex] = pos.cl().clistpair[0].newlist[1];

			diff.p[2][0] += pos.materialDiff() * FVScale;

			ss->staticEvalRaw = diff + (ss-1)->staticEvalRaw;
		}

		return true;
	}

	int make_list_unUseDiff(const Position& pos, int list0[EvalList::ListSize], int list1[EvalList::ListSize], int nlist) {
		auto func = [&](const Bitboard& posBB, const int f_pt, const int e_pt) {
			Square sq;
			Bitboard bb;
			bb = (posBB) & pos.bbOf(Black);
			FOREACH_BB(bb, sq, {
					list0[nlist] = (f_pt) + sq;
					list1[nlist] = (e_pt) + inverse(sq);
					nlist    += 1;
				});
			bb = (posBB) & pos.bbOf(White);
			FOREACH_BB(bb, sq, {
					list0[nlist] = (e_pt) + sq;
					list1[nlist] = (f_pt) + inverse(sq);
					nlist    += 1;
				});
		};
		func(pos.bbOf(Pawn  ), f_pawn  , e_pawn  );
		func(pos.bbOf(Lance ), f_lance , e_lance );
		func(pos.bbOf(Knight), f_knight, e_knight);
		func(pos.bbOf(Silver), f_silver, e_silver);
		const Bitboard goldsBB = pos.goldsBB();
		func(goldsBB         , f_gold  , e_gold  );
		func(pos.bbOf(Bishop), f_bishop, e_bishop);
		func(pos.bbOf(Horse ), f_horse , e_horse );
		func(pos.bbOf(Rook  ), f_rook  , e_rook  );
		func(pos.bbOf(Dragon), f_dragon, e_dragon);

		return nlist;
	}

	void evaluateBody(Position& pos, Search::Stack* ss) {
		if (calcDifference(pos, ss)) {
			assert([&] {
					const auto score = ss->staticEvalRaw.sum(pos.turn());
					return (evaluateUnUseDiff(pos) == score);
				}());
			return;
		}
#if defined USE_AVX2_EVAL
			pos.plist0()[38] = pos.plist0()[39] = pos.plist1()[38] = pos.plist1()[39] = 0;
#endif
		const Square sq_bk = pos.kingSquare(Black);
		const Square sq_wk = pos.kingSquare(White);
		const int* list0 = pos.plist0();
		const int* list1 = pos.plist1();

		const auto* ppkppb = Evaluater::KPP[sq_bk         ];
		const auto* ppkppw = Evaluater::KPP[inverse(sq_wk)];

		EvalSum sum;
		sum.p[2] = Evaluater::KK[sq_bk][sq_wk];
#if defined USE_AVX2_EVAL
		sum.p[0] = { 0, 0 };
		sum.p[1] = { 0, 0 };
		__m256i zero = _mm256_setzero_si256();
		__m256i sum0 = zero;
		__m256i sum1 = zero;
		#pragma unroll
		for (int i = 0; i < pos.nlist() ; ++i) {
			const int k0 = list0[i];
			const int k1 = list1[i];
			const auto* pkppb = ppkppb[k0];
			const auto* pkppw = ppkppw[k1];
			int j = 0;
			#pragma unroll
			for (; j + 8 < i; j += 8) {
				__m256i indexes0 = _mm256_loadu_si256((const __m256i*)(&list0[j]));
				__m256i indexes1 = _mm256_loadu_si256((const __m256i*)(&list1[j]));
				__m256i w0 = _mm256_i32gather_epi32(reinterpret_cast<const int*>(pkppb), indexes0, 4);
				__m256i w1 = _mm256_i32gather_epi32(reinterpret_cast<const int*>(pkppw), indexes1, 4);

				__m256i w0lo = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(w0, 0));
				sum0 = _mm256_add_epi32(sum0, w0lo);
				__m256i w0hi = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(w0, 1));
				sum0 = _mm256_add_epi32(sum0, w0hi);

				__m256i w1lo = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(w1, 0));
				sum1 = _mm256_add_epi32(sum1, w1lo);
				__m256i w1hi = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(w1, 1));
				sum1 = _mm256_add_epi32(sum1, w1hi);
			}
			#pragma unroll
			for (; j + 4 < i; j += 4) {
				__m128i indexes0 = _mm_loadu_si128((const __m128i*)(&list0[j]));
				__m128i indexes1 = _mm_loadu_si128((const __m128i*)(&list1[j]));
				__m128i w0 = _mm_i32gather_epi32(reinterpret_cast<const int*>(pkppb), indexes0, 4);
				__m128i w1 = _mm_i32gather_epi32(reinterpret_cast<const int*>(pkppw), indexes1, 4);

				__m256i w0lo = _mm256_cvtepi16_epi32(w0);
				sum0 = _mm256_add_epi32(sum0, w0lo);

				__m256i w1lo = _mm256_cvtepi16_epi32(w1);
				sum1 = _mm256_add_epi32(sum1, w1lo);
			}
			#pragma unroll
			for (; j < i; ++j) {
				sum.p[0] += pkppb[list0[j]];
				sum.p[1] += pkppw[list1[j]];
			}
			sum.p[2] += Evaluater::KKP[sq_bk][sq_wk][k0];
		}
		sum0 = _mm256_add_epi32(sum0, _mm256_srli_si256(sum0, 8));
		__m128i sum0_128 = _mm_add_epi32(_mm256_extracti128_si256(sum0, 0), _mm256_extracti128_si256(sum0, 1));
		std::array<int32_t, 2> sum0_array;
		_mm_storel_epi64(reinterpret_cast<__m128i*>(&sum0_array), sum0_128);
		sum.p[0] += sum0_array;

		sum1 = _mm256_add_epi32(sum1, _mm256_srli_si256(sum1, 8));
		__m128i sum1_128 = _mm_add_epi32(_mm256_extracti128_si256(sum1, 0), _mm256_extracti128_si256(sum1, 1));
		std::array<int32_t, 2> sum1_array;
		_mm_storel_epi64(reinterpret_cast<__m128i*>(&sum1_array), sum1_128);
		sum.p[1] += sum1_array;
#elif defined USE_SSE_EVAL
//#if defined USE_AVX2_EVAL || defined USE_SSE_EVAL
		sum.m[0] = _mm_setzero_si128();
		for (int i = 0; i < pos.nlist(); ++i) {
			const int k0 = list0[i];
			const int k1 = list1[i];
			const auto* pkppb = ppkppb[k0];
			const auto* pkppw = ppkppw[k1];
			for (int j = 0; j < i; ++j) {
				const int l0 = list0[j];
				const int l1 = list1[j];
				__m128i tmp;
				tmp = _mm_set_epi32(0, 0, *reinterpret_cast<const s32*>(&pkppw[l1][0]), *reinterpret_cast<const s32*>(&pkppb[l0][0]));
				tmp = _mm_cvtepi16_epi32(tmp);
				sum.m[0] = _mm_add_epi32(sum.m[0], tmp);
			}
			sum.p[2] += Evaluater::KKP[sq_bk][sq_wk][k0];
		}
#else
		// loop 開始を i = 1 からにして、i = 0 の分のKKPを先に足す。
		sum.p[2] += Evaluater::KKP[sq_bk][sq_wk][list0[0]];
		sum.p[0][0] = 0;
		sum.p[0][1] = 0;
		sum.p[1][0] = 0;
		sum.p[1][1] = 0;
		for (int i = 1; i < pos.nlist(); ++i) {
			const int k0 = list0[i];
			const int k1 = list1[i];
			const auto* pkppb = ppkppb[k0];
			const auto* pkppw = ppkppw[k1];
			for (int j = 0; j < i; ++j) {
				const int l0 = list0[j];
				const int l1 = list1[j];
				sum.p[0] += pkppb[l0];
				sum.p[1] += pkppw[l1];
			}
			sum.p[2] += Evaluater::KKP[sq_bk][sq_wk][k0];
		}
#endif

		sum.p[2][0] += pos.material() * FVScale;
#if defined INANIWA_SHIFT
		sum.p[2][0] += inaniwaScore(pos);
#endif
		ss->staticEvalRaw = sum;

		assert(evaluateUnUseDiff(pos) == sum.sum(pos.turn()));
	}
}

// todo: 無名名前空間に入れる。
Score evaluateUnUseDiff(const Position& pos) {
	int list0[EvalList::ListSize];
	int list1[EvalList::ListSize];

	const Hand handB = pos.hand(Black);
	const Hand handW = pos.hand(White);

	const Square sq_bk = pos.kingSquare(Black);
	const Square sq_wk = pos.kingSquare(White);
	int nlist = 0;

	auto func = [&](const Hand hand, const HandPiece hp, const int list0_index, const int list1_index) {
		for (u32 i = 1; i <= hand.numOf(hp); ++i) {
			list0[nlist] = list0_index + i;
			list1[nlist] = list1_index + i;
			++nlist;
		}
	};
	func(handB, HPawn  , f_hand_pawn  , e_hand_pawn  );
	func(handW, HPawn  , e_hand_pawn  , f_hand_pawn  );
	func(handB, HLance , f_hand_lance , e_hand_lance );
	func(handW, HLance , e_hand_lance , f_hand_lance );
	func(handB, HKnight, f_hand_knight, e_hand_knight);
	func(handW, HKnight, e_hand_knight, f_hand_knight);
	func(handB, HSilver, f_hand_silver, e_hand_silver);
	func(handW, HSilver, e_hand_silver, f_hand_silver);
	func(handB, HGold  , f_hand_gold  , e_hand_gold  );
	func(handW, HGold  , e_hand_gold  , f_hand_gold  );
	func(handB, HBishop, f_hand_bishop, e_hand_bishop);
	func(handW, HBishop, e_hand_bishop, f_hand_bishop);
	func(handB, HRook  , f_hand_rook  , e_hand_rook  );
	func(handW, HRook  , e_hand_rook  , f_hand_rook  );

	nlist = make_list_unUseDiff(pos, list0, list1, nlist);

	const auto* ppkppb = Evaluater::KPP[sq_bk         ];
	const auto* ppkppw = Evaluater::KPP[inverse(sq_wk)];

	EvalSum score;
	score.p[2] = Evaluater::KK[sq_bk][sq_wk];

	score.p[0][0] = 0;
	score.p[0][1] = 0;
	score.p[1][0] = 0;
	score.p[1][1] = 0;
	for (int i = 0; i < nlist; ++i) {
		const int k0 = list0[i];
		const int k1 = list1[i];
		const auto* pkppb = ppkppb[k0];
		const auto* pkppw = ppkppw[k1];
		for (int j = 0; j < i; ++j) {
			const int l0 = list0[j];
			const int l1 = list1[j];
			score.p[0] += pkppb[l0];
			score.p[1] += pkppw[l1];
		}
		score.p[2] += Evaluater::KKP[sq_bk][sq_wk][k0];
	}

	score.p[2][0] += pos.material() * FVScale;

#if defined INANIWA_SHIFT
	score.p[2][0] += inaniwaScore(pos);
#endif

	return static_cast<Score>(score.sum(pos.turn()));
}

Score Search::evaluate(Position& pos, Search::Stack* ss) {
	if (ss->staticEvalRaw.p[0][0] != ScoreNotEvaluated) {
		const Score score = static_cast<Score>(ss->staticEvalRaw.sum(pos.turn()));
		assert(score == evaluateUnUseDiff(pos));
		return score / FVScale;
	}

	const Key keyExcludeTurn = pos.getKeyExcludeTurn();
	EvaluateHashEntry entry = *g_evalTable[keyExcludeTurn]; // atomic にデータを取得する必要がある。
	entry.decode();
	if (entry.key == keyExcludeTurn) {
		ss->staticEvalRaw = entry;
		assert(static_cast<Score>(ss->staticEvalRaw.sum(pos.turn())) == evaluateUnUseDiff(pos));
		return static_cast<Score>(entry.sum(pos.turn())) / FVScale;
	}

	evaluateBody(pos, ss);

	ss->staticEvalRaw.key = keyExcludeTurn;
	ss->staticEvalRaw.encode();
	*g_evalTable[keyExcludeTurn] = ss->staticEvalRaw;
	return static_cast<Score>(ss->staticEvalRaw.sum(pos.turn())) / FVScale;
}
