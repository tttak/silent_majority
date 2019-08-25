#include <thread>
#include "misc.h"


// --- 以下、やねうら王で独自追加したコード

// --------------------
//  Timer
// --------------------

void sleep(int ms)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
