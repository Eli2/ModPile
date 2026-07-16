#include <catch2/catch_test_macros.hpp>

#include "../src/blocking_queue.h"

TEST_CASE("clearing a blocking queue discards all pending items", "[task][queue]") {
	queue<int> pending;
	pending.push(1);
	pending.push(2);
	pending.push(3);

	pending.clear();

	CHECK(pending.snapshot().empty());
	pending.push(4);
	CHECK(pending.pop() == 4);
}
