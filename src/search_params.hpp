#ifndef SM_SEARCH_PARAMS_HPP
#define SM_SEARCH_PARAMS_HPP

#define LIMIT1 (0.16)
#define LIMIT2 (0.64)
#define IS_PROLOGUE1 (progress < LIMIT1)
#define IS_PROLOGUE2 (progress < 0.32)
#define IS_PROLOGUE3 (progress < LIMIT2)

// --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< ---
// step7
// Futility pruning: child node (skipped when in check)
//#define PARAM_FUTILITY_RETURN_DEPTH 7
//#define PARAM_FUTILITY_RETURN_DEPTH 9 // change!!
#define PARAM_FUTILITY_RETURN_DEPTH (int)(progress < LIMIT1 ? 9 : (progress < LIMIT2 ? 10 : 11))


// --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< ---
// step8
// null move
//#define PARAM_NULL_MOVE_MARGIN 32 // YaneuraOu
//#define PARAM_NULL_MOVE_MARGIN 35
#define PARAM_NULL_MOVE_MARGIN 35 // stay!

#define PARAM_NULL_MOVE_DYNAMIC_ALPHA 823 //823 -> 818 -> 823 // stay!
#define PARAM_NULL_MOVE_DYNAMIC_BETA 67 //67 -> 67 // stay!

//#define PARAM_NULL_MOVE_RETURN_DEPTH 13 // YaneuraOu
#define PARAM_NULL_MOVE_RETURN_DEPTH 12 // stay!
//#define PARAM_NULL_MOVE_RETURN_DEPTH (int)(progress < LIMIT1 ? 14 : (progress < LIMIT2 ? 13 : 12))


// --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< ---
// step9
// probcut
//#define PARAM_PROBCUT_DEPTH 7 // YaneuraOu
//#define PARAM_PROBCUT_DEPTH 5
#define PARAM_PROBCUT_DEPTH 5 // stay!

//#define PARAM_PROBCUT_MARGIN 220 // YaneuraOu
//#define PARAM_PROBCUT_MARGIN 200
#define PARAM_PROBCUT_MARGIN 200 // stay!


// --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< ---
// step10
// internal iterative deepening
#define PARAM_IID_MARGIN_ALPHA 256 // 256 -> 251 -> 256 // stay!


// --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< ---
// moves_loop

//#define PARAM_SINGULAR_EXTENSION_DEPTH 8 // YaneuraOu
//#define PARAM_SINGULAR_EXTENSION_DEPTH 7 // change
#define PARAM_SINGULAR_EXTENSION_DEPTH (int)(progress < LIMIT1 ? 7 : (progress < LIMIT2 ? 8 : 9))


// --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< ---
// step11
// Loop through moves
#define PARAM_PRUNING_BY_MOVE_COUNT_DEPTH 16 // 16 -> 16 // stay!


// --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< ---
// step13
// futility pruning

//#define PARAM_PRUNING_BY_HISTORY_DEPTH 9 // YaneuraOu
//#define PARAM_PRUNING_BY_HISTORY_DEPTH 9 // change!
#define PARAM_PRUNING_BY_HISTORY_DEPTH (int)(progress < LIMIT1 ? 9 : (progress < LIMIT2 ? 10 : 11))

//#define PARAM_FUTILITY_AT_PARENT_NODE_DEPTH 12 // change!
#define PARAM_FUTILITY_AT_PARENT_NODE_DEPTH (int)(progress < LIMIT1 ? 12 : (progress < LIMIT2 ? 13 : 14))

//#define PARAM_FUTILITY_AT_PARENT_NODE_MARGIN1 246 // YaneuraOu
//#define PARAM_FUTILITY_AT_PARENT_NODE_MARGIN1 256
#define PARAM_FUTILITY_AT_PARENT_NODE_MARGIN1 256 // stay!

#define PARAM_FUTILITY_MARGIN_BETA 147 // 200 -> 147 // change!
#define PARAM_FUTILITY_AT_PARENT_NODE_GAMMA1 40 // 35 -> 40 // change!
#define PARAM_FUTILITY_AT_PARENT_NODE_GAMMA2 51 // new!


// --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< ---
// step15
// LMR
//#define PARAM_CUTNODE_REDUCTION_MARGIN 2 // YaneuraOu
#define PARAM_CUTNODE_REDUCTION_MARGIN 2 // change!
//#define PARAM_CUTNODE_REDUCTION_MARGIN (int)(progress < LIMIT1 ? 2 : (progress < LIMIT2 ? 3 : 4))

//#define PARAM_REDUCTION_BY_HISTORY 4000 // YaneuraOu
#define PARAM_REDUCTION_BY_HISTORY 4000 // stay!


// --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< --- 8< ---
// qsearch
#define PARAM_FUTILITY_MARGIN_QUIET 145 // 128 -> 145

#endif
