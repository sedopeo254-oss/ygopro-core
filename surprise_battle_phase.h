/*
 * Extra Battle Phase requested during End Phase by anime cards such as
 * Surprise Attack From Beyond. Does not advance the turn or skip an opponent.
 * Kept outside MultiplayerState and independent of all duel variants.
 */
#ifndef SURPRISE_BATTLE_PHASE_H
#define SURPRISE_BATTLE_PHASE_H
#include <cstdint>

namespace surprise_battle_phase {
constexpr uint16_t END_PHASE = 0x200;
class State {
public:
	void reset() { requested_ = false; consumed_ = false; in_extra_ = false; }
	bool can_request(uint8_t player, uint8_t turn_player,
			uint16_t phase, uint32_t completed_battles, bool force_end) const {
		return player <= 1 && player == turn_player && phase == END_PHASE
			&& completed_battles != 0 && !force_end
			&& !requested_ && !consumed_ && !in_extra_;
	}
	bool request(uint8_t player, uint8_t turn_player,
			uint16_t phase, uint32_t completed_battles, bool force_end) {
		if(!can_request(player, turn_player, phase, completed_battles, force_end))
			return false;
		requested_ = true;
		return true;
	}
	bool enter_extra_battle() {
		if(!requested_ || consumed_ || in_extra_)
			return false;
		requested_ = false;
		consumed_ = true;
		in_extra_ = true;
		return true;
	}
	bool finish_extra_battle() {
		if(!in_extra_) return false;
		in_extra_ = false;
		return true;
	}
	bool pending() const { return requested_; }
	bool in_extra_battle() const { return in_extra_; }
	bool used_this_turn() const { return consumed_; }
private:
	bool requested_{false};
	bool consumed_{false};
	bool in_extra_{false};
};
}
#endif
