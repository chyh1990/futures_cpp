#pragma once

namespace futures {
namespace test {

class MoveOnlyType {
public:
	MoveOnlyType(int v = 42): v_(v) {}
	MoveOnlyType(const MoveOnlyType&) = delete;
	MoveOnlyType& operator=(const MoveOnlyType&) = delete;

	MoveOnlyType(MoveOnlyType&& o) noexcept
		: v_(o.v_), moved_(o.moved_ + 1) {
		// FUTURES_DLOG(INFO) << "moving: " << moved_;
	}
	MoveOnlyType& operator=(MoveOnlyType&& o) {
		if (&o == this) return *this;
		v_ = o.v_;
		moved_ = o.moved_ + 1;
		return *this;
	}

	int GetV() const { return v_; }
	unsigned int GetMoved() const { return moved_; }
private:
	int v_;
	unsigned int moved_ = 0;
};

}
}
