
#include <thread>
#include <gtest/gtest.h>
#include <futures_mysql.h>
#include <futures/Timeout.h>
#include <futures/Timer.h>
#include <futures/Stream.h>
#include <futures_mysql/Connection.h>
#include <futures_mysql/Pool.h>

using namespace futures;

static mysql::Config getConfig() {
	return mysql::Config{"127.0.0.1", 3306, "root", "123456", "test_todo"};
}

static void PrepareData(EventExecutor *ev) {
	auto f = (mysql::Connection::connect(ev, getConfig())
		>> [] (mysql::Connection::Ptr conn) {
			return conn->exec("DROP TABLE IF EXISTS ut_test_insert")
				>> [conn] (mysql::ResultSet res) {
					return conn->exec("CREATE TABLE ut_test_insert ("
							"id int not null AUTO_INCREMENT, my_name varchar(50), PRIMARY KEY(id))");
				}
				>> [conn] (mysql::ResultSet res) {
					static std::vector<std::string> gVals{"AAAA", "BBBB", "CCCC"};
					return makeIterStream(gVals.begin(), gVals.end())
						.andThen([conn] (const std::string &v) {
								return conn->exec("INSERT INTO ut_test_insert (my_name) VALUES (\"" + v + "\")");
						}).drop();
				}
				>> [conn] (Unit) {
					return conn->close();
				};
		})
		.error([] (folly::exception_wrapper ex) { ex.throwException(); });
	ev->spawn(std::move(f));
	ev->run();
}

TEST(MySQL, Insert) {
	EventExecutor ev;
	auto f = (mysql::Connection::connect(&ev, getConfig())
		>> [] (mysql::Connection::Ptr conn) {
			return conn->exec("DROP TABLE IF EXISTS ut_test_insert")
				>> [conn] (mysql::ResultSet res) {
					return conn->exec("CREATE TABLE ut_test_insert ("
							"id int not null AUTO_INCREMENT, my_name varchar(50), PRIMARY KEY(id))");
				}
				>> [conn] (mysql::ResultSet res) {
					return conn->exec("INSERT INTO ut_test_insert (my_name) VALUES (\"First value\"),"
							"(\"Second value\")");
				}
				>> [conn] (mysql::ResultSet res) {
					EXPECT_EQ(res.getAffectedRows(), 2);
					EXPECT_EQ(res.getInsertId(), 1);
					return conn->close();
				};
		})
		.error([] (folly::exception_wrapper ex) { EXPECT_TRUE(false); });
	ev.spawn(std::move(f));
	ev.run();
}

TEST(MySQL, Query) {
	EventExecutor ev;
	PrepareData(&ev);

	auto f = (mysql::Connection::connect(&ev, getConfig())
		>> [] (mysql::Connection::Ptr conn) {
			return conn->query("SELECT * from ut_test_insert LIMIT 2")
				>> [conn] (mysql::ResultSet rs) {
					EXPECT_EQ(rs.getFields().size(), 2);
					EXPECT_EQ(rs.getBufferedRows().size(), 2);
					auto &row = rs.getBufferedRows()[0];
					EXPECT_EQ(row.getField(1).value(), "AAAA");
					EXPECT_EQ(row.get(0), 1);
					EXPECT_EQ(row.get(1), std::string("AAAA"));
					return conn->close();
				};
		})
		.error([] (folly::exception_wrapper ex) { ex.throwException(); });
	ev.spawn(std::move(f));
	ev.run();
}

TEST(MySQL, ConnectionFail) {
	EventExecutor ev;
	mysql::Config cfg{"127.0.0.1", 3306, "root", "XXX_WRONG", "test_todo"};
	auto f = mysql::Connection::connect(&ev, cfg)
		>> [] (mysql::Connection::Ptr conn) {
			throw std::runtime_error("should error");
			return conn->close();
		};
	ev.spawn(std::move(f));
	ev.run();
}

TEST(MySQL, QueryError) {
	EventExecutor ev;
	auto f = mysql::Connection::connect(&ev, getConfig())
		>> [] (mysql::Connection::Ptr conn) {
			return conn->query("SELECT * FROM xxx_some_invalid_table")
				<< [conn] (Try<mysql::ResultSet> rs) {
					EXPECT_TRUE(rs.hasException());
					return conn->close();
				};
		};
	ev.spawn(std::move(f));
	ev.run();
}

TEST(MySQL, PrepareStmt) {
	EventExecutor ev;
	PrepareData(&ev);

	auto f = (mysql::Connection::connect(&ev, getConfig())
		>> [] (mysql::Connection::Ptr conn) {
			return conn->prepare("INSERT INTO ut_test_insert (my_name) VALUES (?)")
				>> [] (mysql::PreparedStatement::Ptr ps) {
					ps->set<std::string>(0, "TESTXXX");
					return ps->exec()
						>> [ps] (mysql::ResultSet rs) {
							EXPECT_EQ(rs.getInsertId(), 4);
							return ps->close();
						};
				}
				>> [conn] (Unit) {
					return conn->close();
				};
		})
		.error([] (folly::exception_wrapper ex) { ex.throwException(); });
	ev.spawn(std::move(f));
	ev.run();

}

TEST(MySQL, PrepareStmtBad) {
	EventExecutor ev;
	PrepareData(&ev);

	auto f = mysql::Connection::connect(&ev, getConfig())
		>> [] (mysql::Connection::Ptr conn) {
			return conn->prepare("INSERT INTO ut_test_insert1 (my_name) VALUES (?)")
				>> [] (mysql::PreparedStatement::Ptr ps) {
					EXPECT_TRUE(false);
					return makeOk();
				}
				<< [conn] (Try<Unit>) {
					return conn->close();
				};
		};
	ev.spawn(std::move(f));
	ev.run();

}

TEST(MySQL, ConnectionPool) {
	EventExecutor ev;
	auto pool = mysql::Pool::create(&ev, getConfig(), 1, 2.0);

	auto f = pool->getConnection()
		>> [pool] (mysql::Connection::Ptr conn) {
			return pool->checkin(conn);
		};
	ev.spawn(std::move(f));
	ev.run();
	EXPECT_EQ(pool->getIdleCount(), 1);

	auto f1 = pool->getConnection()
		>> [pool] (mysql::Connection::Ptr conn) {
			return conn->close();
		};
	ev.spawn(std::move(f1));
	ev.run();
	EXPECT_EQ(pool->getIdleCount(), 0);

	auto f2 = pool->getConnection()
		>> [pool] (mysql::Connection::Ptr conn) {
			return pool->checkin(conn);
		}
		>> [pool] (Unit) {
			EXPECT_EQ(pool->getIdleCount(), 1);
			// wait until connection being recycled
			return delay(EventExecutor::current(), 3.0);
		};
	ev.spawn(std::move(f2));
	ev.run();
	EXPECT_EQ(pool->getIdleCount(), 0);


}

int main(int argc, char* argv[]) {
	testing::InitGoogleTest(&argc, argv);
	mysql::InitOnce::init();
	return RUN_ALL_TESTS();
}

