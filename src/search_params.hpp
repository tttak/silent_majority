#ifndef SM_SEARCH_PARAMS_HPP
#define SM_SEARCH_PARAMS_HPP

// --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< ---
// step7
// Futility pruning: child node (skipped when in check)
//#define PARAM_FUTILITY_RETURN_DEPTH 10 // YaneuraOu
//#define PARAM_FUTILITY_RETURN_DEPTH 7
//#define PARAM_FUTILITY_RETURN_DEPTH (int)(progress > 0.64 ? 7 : (progress > 0.16 ? 10 : 13))
//#define PARAM_FUTILITY_RETURN_DEPTH (int)(progress > 0.64 ? 13 : (progress > 0.16 ? 10 : 7))
#define PARAM_FUTILITY_RETURN_DEPTH (int)(progress > 0.64 ? 15 : (progress > 0.16 ? 10 : 9))


// --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< ---
// step8
// null move
//#define PARAM_NULL_MOVE_MARGIN 32 // YaneuraOu
//#define PARAM_NULL_MOVE_MARGIN 35
//#define PARAM_NULL_MOVE_MARGIN (int)(progress > 0.64 ? 18 : (progress > 0.16 ? 32 : 36))
//#define PARAM_NULL_MOVE_MARGIN (int)(progress > 0.64 ? 36 : (progress > 0.16 ? 32 : 18))
#define PARAM_NULL_MOVE_MARGIN (int)(progress > 0.64 ? 40 : (progress > 0.16 ? 32 : 22))

#define PARAM_NULL_MOVE_DYNAMIC_ALPHA 823 //823 -> 818 -> 823
#define PARAM_NULL_MOVE_DYNAMIC_BETA 67 //67 -> 67

//#define PARAM_NULL_MOVE_RETURN_DEPTH 13 // YaneuraOu
//#define PARAM_NULL_MOVE_RETURN_DEPTH 12
//#define PARAM_NULL_MOVE_RETURN_DEPTH std::max(12, (int)(24 * (1.0 - progress)))
//#define PARAM_NULL_MOVE_RETURN_DEPTH (int)(progress > 0.64 ? 6 : (progress > 0.16 ? 12 : 18))
//#define PARAM_NULL_MOVE_RETURN_DEPTH (int)(progress > 0.64 ? 18 : (progress > 0.16 ? 12 : 6))
#define PARAM_NULL_MOVE_RETURN_DEPTH (int)(progress > 0.64 ? 20 : (progress > 0.16 ? 12 : 8))


// --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< ---
// step9
// probcut
#define PARAM_PROBCUT_DEPTH 7 // YaneuraOu
//#define PARAM_PROBCUT_DEPTH 5
//#define PARAM_PROBCUT_DEPTH (int)(progress > 0.64 ? 5 : (progress > 0.16 ? 6 : 7))
//#define PARAM_PROBCUT_DEPTH (int)(progress > 0.64 ? 7 : (progress > 0.16 ? 6 : 5))

//#define PARAM_PROBCUT_MARGIN 220 // YaneuraOu
//#define PARAM_PROBCUT_MARGIN 200
//#define PARAM_PROBCUT_MARGIN (int)(progress > 0.64 ? 200 : (progress > 0.16 ? 210 : 220))
//#define PARAM_PROBCUT_MARGIN (int)(progress > 0.64 ? 220 : (progress > 0.16 ? 210 : 200))
#define PARAM_PROBCUT_MARGIN (int)(progress > 0.64 ? 240 : (progress > 0.16 ? 220 : 210))


// --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< ---
// step10
// internal iterative deepening
#define PARAM_IID_MARGIN_ALPHA 256 // 256 -> 251 -> 256


// --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< ---
// moves_loop

#define PARAM_SINGULAR_EXTENSION_DEPTH 8 // YaneuraOu
//#define PARAM_SINGULAR_EXTENSION_DEPTH (int)(progress > 0.64 ? 7 : (progress > 0.16 ? 8 : 9))
//#define PARAM_SINGULAR_EXTENSION_DEPTH (int)(progress > 0.64 ? 9 : (progress > 0.16 ? 8 : 7))


// --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< ---
// step11
// Loop through moves
#define PARAM_PRUNING_BY_MOVE_COUNT_DEPTH 16 // 16 -> 16


// --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< ---
// step13
// futility pruning

#define PARAM_PRUNING_BY_HISTORY_DEPTH 9 // YaneuraOu
//#define PARAM_PRUNING_BY_HISTORY_DEPTH std::max(7, (int)(21 * (1.0 - progress)))
//#define PARAM_PRUNING_BY_HISTORY_DEPTH (int)(progress > 0.64 ? 3 : (progress > 0.16 ? 8 : 13))
//#define PARAM_PRUNING_BY_HISTORY_DEPTH (int)(progress > 0.64 ? 13 : (progress > 0.16 ? 8 : 3))

#define PARAM_FUTILITY_AT_PARENT_NODE_DEPTH 12 // 7 -> 12

//#define PARAM_FUTILITY_AT_PARENT_NODE_MARGIN1 246 // YaneuraOu
#define PARAM_FUTILITY_AT_PARENT_NODE_MARGIN1 256
//#define PARAM_FUTILITY_AT_PARENT_NODE_MARGIN1 (int)(progress > 0.64 ? 222: (progress > 0.16 ? 246 : 256 ))
//#define PARAM_FUTILITY_AT_PARENT_NODE_MARGIN1 (int)(progress > 0.64 ? 256 : (progress > 0.16 ? 246 : 222))

#define PARAM_FUTILITY_MARGIN_BETA 147 // 200 -> 147
#define PARAM_FUTILITY_AT_PARENT_NODE_GAMMA1 40 // 35 -> 40
#define PARAM_FUTILITY_AT_PARENT_NODE_GAMMA2 51


// --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< ---
// step15
// LMR
//#define PARAM_CUTNODE_REDUCTION_MARGIN 2 // YaneuraOu
//#define PARAM_CUTNODE_REDUCTION_MARGIN 3 // YaneuraOu
//#define PARAM_CUTNODE_REDUCTION_MARGIN (int)(progress > 0.64 ? 2 : (progress > 0.16 ? 3 : 5))
//#define PARAM_CUTNODE_REDUCTION_MARGIN (int)(progress > 0.64 ? 5 : (progress > 0.16 ? 3 : 2))
#define PARAM_CUTNODE_REDUCTION_MARGIN (int)(progress > 0.64 ? 6 : (progress > 0.16 ? 4 : 3))

#define PARAM_REDUCTION_BY_HISTORY 4000 // YaneuraOu
//#define PARAM_REDUCTION_BY_HISTORY (int)(progress > 0.64 ? 4000 : (progress > 0.16 ? 5000 : 6000))
//#define PARAM_REDUCTION_BY_HISTORY (int)(progress > 0.64 ? 6000 : (progress > 0.16 ? 5000 : 4000))


// --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< ---
// qsearch
#define PARAM_FUTILITY_MARGIN_QUIET 145 // 128 -> 145

#endif
