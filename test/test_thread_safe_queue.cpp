#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <future>

#include "../src/util/thread_safe_queue.h"

using namespace std::chrono_literals;

TEST_CASE("thread-safe queue preserves FIFO order", "[util][queue]") {
	ThreadSafeQueue<int> queue;
	REQUIRE(queue.push(1));
	REQUIRE(queue.push(2));
	REQUIRE(queue.push(3));

	CHECK(queue.try_pop() == 1);
	CHECK(queue.try_pop() == 2);
	CHECK(queue.try_pop() == 3);
	CHECK_FALSE(queue.try_pop().has_value());
}

TEST_CASE("thread-safe queue blocks consumers until an item arrives", "[util][queue]") {
	ThreadSafeQueue<int> queue;
	auto consumer = std::async(std::launch::async, [&] { return queue.pop(); });
	CHECK(consumer.wait_for(0ms) == std::future_status::timeout);

	REQUIRE(queue.push(42));
	CHECK(consumer.get() == 42);
}

TEST_CASE("bounded thread-safe queue blocks producers while full", "[util][queue]") {
	ThreadSafeQueue<int> queue(1);
	REQUIRE(queue.push(1));
	auto producer = std::async(std::launch::async, [&] { return queue.push(2); });
	CHECK(producer.wait_for(20ms) == std::future_status::timeout);

	CHECK(queue.pop() == 1);
	CHECK(producer.get());
	CHECK(queue.pop() == 2);
}

TEST_CASE("closing a thread-safe queue permits draining but rejects pushes", "[util][queue]") {
	ThreadSafeQueue<int> queue;
	REQUIRE(queue.push(1));
	REQUIRE(queue.push(2));
	queue.close();

	CHECK_FALSE(queue.push(3));
	CHECK(queue.pop() == 1);
	CHECK(queue.pop() == 2);
	CHECK_FALSE(queue.pop().has_value());
}

TEST_CASE("closing a thread-safe queue can discard pending items", "[util][queue]") {
	ThreadSafeQueue<int> queue;
	REQUIRE(queue.push(1));
	queue.close(true);

	CHECK_FALSE(queue.pop().has_value());
}

TEST_CASE("closing a thread-safe queue wakes blocked consumers", "[util][queue]") {
	ThreadSafeQueue<int> queue;
	auto consumer = std::async(std::launch::async, [&] { return queue.pop(); });
	queue.close();

	CHECK_FALSE(consumer.get().has_value());
}

TEST_CASE("bounded thread-safe queue push can be cancelled", "[util][queue]") {
	ThreadSafeQueue<int> queue(1);
	REQUIRE(queue.push(1));
	std::atomic_bool abort = false;
	auto producer = std::async(std::launch::async, [&] { return queue.push(2, &abort); });
	abort = true;

	CHECK_FALSE(producer.get());
	CHECK(queue.pop() == 1);
}
