﻿// NNUE評価関数で用いる入力特徴量とネットワーク構造

#ifndef _NNUE_ARCHITECTURE_H_
#define _NNUE_ARCHITECTURE_H_

#include "../../config.h"

#if defined(EVAL_NNUE)

// 入力特徴量とネットワーク構造が定義されたヘッダをincludeする

#if defined(EVAL_NNUE_HALFKP_KK)
#include "architectures/halfkp-kk_256x2-32-32.h"
#elif defined(EVAL_NNUE_HALFKP_PP)
#include "architectures/halfkp-pp_256x2-32-32.h"
#elif defined(EVAL_NNUE_HALFKP_GAMEPLY40x4)
#include "architectures/halfkp_gameply40x4_256x2-32-32.h"
#elif defined(EVAL_NNUE_HALFKPKFILE)
#include "architectures/halfkpkfile_256x2-32-32.h"
#else
#include "architectures/halfkp_256x2-32-32.h"
#endif

namespace Eval {

namespace NNUE {

static_assert(kTransformedFeatureDimensions % kMaxSimdWidth == 0, "");
static_assert(Network::kOutputDimensions == 1, "");
static_assert(std::is_same<Network::OutputType, std::int32_t>::value, "");

// 差分計算の代わりに全計算を行うタイミングのリスト
constexpr auto kRefreshTriggers = RawFeatures::kRefreshTriggers;

}  // namespace NNUE

}  // namespace Eval

#endif  // defined(EVAL_NNUE)

#endif
