#pragma once

namespace futures {
namespace test {

class MoveOnlyType {
public:
	MoveOnlyType(int v = 42): v_(v) {}
	MoveOnlyType(const MoveOnlyType&) = delete;
	MoveOnlyType& operator=(const MoveOnlyType&) = delete;

	MoveOnlyType(MoveOnlyType&&) = default;
	MoveOnlyType& operator=(MoveOnlyType&&) = default;

	int GetV() const { return v_; }
private:
	int v_;
};

}
}
