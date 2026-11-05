/*
 * Inherited Duel Mode - Duel Succession System (foundation).
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * The physical field side stays fixed while the controlling person changes.
 * This state machine NEVER moves cards, changes LP, or interrupts a win.
 * The server and engine must finish a verified takeover before commit().
 */
#ifndef DUEL_SUCCESSION_H
#define DUEL_SUCCESSION_H

#include <cstdint>

enum class DuelSuccessionPhase : uint8_t {
	DISABLED = 0,
	PRIMARY = 1,
	AWAITING_SUCCESSOR = 2,
	SUCCESSOR = 3
};

class DuelSuccessionState {
public:
	static constexpr uint8_t NO_SIDE = 0xff;

	void configure(bool enable, uint8_t inherited_side = 0) {
		reset();
		if(!enable || inherited_side > 1)
			return;
		side_ = inherited_side;
		phase_ = DuelSuccessionPhase::PRIMARY;
	}

	void reset() {
		phase_ = DuelSuccessionPhase::DISABLED;
		side_ = NO_SIDE;
		defeat_turn_ = 0;
		resume_turn_ = 0;
		controller_generation_ = 0;
	}

	bool enabled() const { return phase_ != DuelSuccessionPhase::DISABLED; }
	DuelSuccessionPhase phase() const { return phase_; }
	uint8_t inherited_side() const { return side_; }
	uint32_t defeat_turn() const { return defeat_turn_; }
	uint32_t resume_turn() const { return resume_turn_; }
	uint8_t controller_generation() const { return controller_generation_; }

	// Invoke only after a preliminary loss is proven and its chain resolves.
	// This does not suppress MSG_WIN: that behavior comes in a later stage.
	bool request(uint8_t defeated_side, uint8_t winner_side, uint32_t turn) {
		if(phase_ != DuelSuccessionPhase::PRIMARY || !turn
			|| defeated_side != side_ || winner_side > 1
			|| winner_side == defeated_side)
			return false;
		defeat_turn_ = turn;
		phase_ = DuelSuccessionPhase::AWAITING_SUCCESSOR;
		return true;
	}

	// Invoke after BOTH network-control transfer and deck/zone migration.
	// Never call this method to simulate the transfers themselves.
	bool commit(uint32_t next_turn, bool control_transferred,
			bool deck_merge_completed) {
		if(phase_ != DuelSuccessionPhase::AWAITING_SUCCESSOR
			|| !control_transferred || !deck_merge_completed
			|| next_turn <= defeat_turn_)
			return false;
		resume_turn_ = next_turn;
		controller_generation_ = 1;
		phase_ = DuelSuccessionPhase::SUCCESSOR;
		return true;
	}

	bool cancel() {
		if(phase_ != DuelSuccessionPhase::AWAITING_SUCCESSOR)
			return false;
		defeat_turn_ = 0;
		phase_ = DuelSuccessionPhase::PRIMARY;
		return true;
	}

private:
	DuelSuccessionPhase phase_{ DuelSuccessionPhase::DISABLED };
	uint8_t side_{ NO_SIDE };
	uint8_t controller_generation_{ 0 };
	uint32_t defeat_turn_{ 0 };
	uint32_t resume_turn_{ 0 };
};

#endif // DUEL_SUCCESSION_H
