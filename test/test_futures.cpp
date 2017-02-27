#include <gtest/gtest.h>

#include <futures/Future.h>
#include <futures/Promise.h>
#include <futures/EventExecutor.h>
#include "HelperTypes.h"

using namespace futures;
using test::MoveOnlyType;

TEST(Future, Trait) {
	EXPECT_FALSE(std::is_copy_constructible<OkFuture<int>>::value);
	EXPECT_TRUE(std::is_move_constructible<OkFuture<int>>::value);
}

#if 0
TEST(Future, Empty) {
	auto f = makeEmpty<int>();
	auto p = f.poll();
	auto &v = p.value();
	EXPECT_EQ(v, Async<int>());
	EXPECT_TRUE(v.isNotReady());
	auto v1 = v.map([] (int v) { return std::to_string(v); });
}
#endif

TEST(Future, Err) {
	auto f = ErrFuture<int>(folly::make_exception_wrapper<std::runtime_error>("bad"));
	EXPECT_TRUE(f.poll().hasException());
}

TEST(Future, Ok) {
	auto f = makeOk(5);
	auto p = f.poll();
	EXPECT_EQ(p.value().value(), 5);
	EXPECT_ANY_THROW(f.poll());
}

TEST(Future, Move) {
	auto f = makeOk(MoveOnlyType(42));
	auto f1 = f.andThen([] (MoveOnlyType v) {
			EXPECT_EQ(v.GetV(), 42);
			return makeOk();
	});
}

TEST(Future, Shared) {
	auto f = makeOk(42).shared();
	auto f1 = f;
	auto f2 = f;

	bool b = std::is_copy_constructible<SharedFuture<int>>::value;
	EXPECT_TRUE(b);

	EXPECT_EQ(f1.poll().value(), Async<int>(42));
	EXPECT_EQ(f2.poll().value(), Async<int>(42));
}

TEST(Future, AndThen) {
	auto f = makeOk(5);
	auto f1 = f.andThen([] (int v) {
			std::cerr << "HERE: " << v << std::endl;
			return makeOk(0);
	});
	auto f2 = f1.andThen([] (int v) {
			std::cerr << "HERE: " << v << std::endl;
			return makeOk('a');
	});
	auto f3 = f2.poll();
	EXPECT_EQ(f3.value(), Async<char>('a'));
}

TEST(Future, Join) {
	auto f = makeOk(1).join(makeOk(std::string("3")))
		.andThen2([] (int a, std::string b) {
			return makeOk(std::to_string(a) + b);
		});
	auto r = f.poll();
	EXPECT_EQ(r.value(), Async<std::string>("13"));
}

TEST(Future, Join2) {
	auto f = makeOk(1)
		.join(makeOk(std::string("3")))
		>> [] (int a, std::string b) {
			return makeOk(std::to_string(a) + b);
		};
	auto r = f.poll();
	EXPECT_EQ(r.value(), Async<std::string>("13"));
}


TEST(Future, Select) {
	auto f1 = makeOk(1);
	auto f2 = makeOk(2);
	std::vector<OkFuture<int>> fs;
	fs.push_back(std::move(f1));
	fs.push_back(std::move(f2));
	auto f = makeSelect(fs.begin(), fs.end());

	f.wait();
}

TEST(Future, LoopFn) {
	auto f = makeLoop(0, [] (int s) {
			std::cerr << s << std::endl;
			if (s < 10) {
				return makeOk(makeContinue<std::string, int>(s + 1));
			} else {
				return makeOk(makeBreak<std::string, int>("XX"));
			}
	});
	EXPECT_EQ(f.value(), "XX");
}

TEST(Future, Map)
{
	auto f = makeOk(4).map([] (int v) { return std::to_string(v) + "1"; });
	EXPECT_EQ(f.wait()->value(), "41");
}

TEST(Future, OrElse) {
	auto f = makeErr<int>(folly::make_exception_wrapper<IOError>("ERR"))
		.orElse([] () { return makeOk(4); });
	EXPECT_EQ(f.value(), 4);
}

TEST(Future, StaticSelect) {
	auto f = whenAny(
			on(makeEmpty<Unit>(), [] (Unit) {
				FUTURES_DLOG(INFO) << "case0";
			}),
			on(makeOk(1), [] (int v) {
				FUTURES_DLOG(INFO) << "case1";
			}),
			on(makeOk(2), [] (int v) {
				FUTURES_DLOG(INFO) << "case2";
			}),
			on(makeOk(std::string("A")), [] (std::string v) {
				FUTURES_DLOG(INFO) << "case3";
			})
	);
	EXPECT_EQ(f.value(), 1);
}

TEST(Future, StaticWhenAll) {
	auto f = whenAll(makeOk(1), makeOk(std::string("OK")));
	EXPECT_EQ(f.value(), std::make_tuple(1, std::string("OK")));
}

