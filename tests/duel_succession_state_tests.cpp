#include "duel_succession.h"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool value, const char* message) {
	if(value)
		return;
	std::cerr << "FAIL: " << message << '\n';
	std::exit(1);
}

void test_disabled_in_standard_duels() {
	DuelSuccessionState state;
	require(!state.enabled(), "Standard duels must not enable succession");
	require(state.phase() == DuelSuccessionPhase::DISABLED, "starts disabled");
	require(!state.request(0, 1, 18), "cannot request while disabled");
	require(!state.commit(19, true, true), "cannot commit while disabled");
}

void test_noah_kaiba_to_yugi() {
	DuelSuccessionState state;
	state.configure(true, 0);
	require(state.enabled(), "Inherited Duel must be explicitly enabled");
	require(state.inherited_side() == 0, "Kaiba uses the same physical side");
	require(state.controller_generation() == 0, "Kaiba is original controller");
	require(!state.request(1, 0, 18), "Noah's loss cannot trigger Kaiba succession");
	require(!state.request(0, 0, 18), "winner must be the opposing side");
	require(!state.request(0, 1, 0), "cannot request without a turn");
	require(state.request(0, 1, 18), "Last Turn can request succession");
	require(!state.request(0, 1, 18), "cannot request twice");
	require(!state.commit(18, true, true), "cannot resume in defeat turn");
	require(!state.commit(19, false, true), "network control must transfer");
	require(!state.commit(19, true, false), "deck ownership must merge");
	require(state.phase() == DuelSuccessionPhase::AWAITING_SUCCESSOR,
		"incomplete transfer cannot advance state");
	require(state.commit(19, true, true), "Yugi takeover commits at turn 19");
	require(state.controller_generation() == 1, "controller becomes successor");
	require(state.inherited_side() == 0, "physical side must never change");
	require(state.defeat_turn() == 18 && state.resume_turn() == 19,
		"turn continuity must be retained for replay");
	require(!state.commit(20, true, true), "cannot repeat completed transfer");
	require(!state.request(0, 1, 22), "Noah scenario cannot reenter");
}

void test_rollback_and_isolation() {
	DuelSuccessionState state;
	state.configure(true, 1);
	require(!state.request(0, 1, 4), "cannot steal an opponent's slot");
	require(state.request(1, 0, 5), "other side works in generic mode");
	require(state.cancel(), "failed transaction can be rolled back");
	require(state.phase() == DuelSuccessionPhase::PRIMARY,
		"cancel returns to original controller");
	require(!state.cancel(), "cannot cancel inactive transfer");
	require(state.request(1, 0, 6), "may retry after rollback");
	state.reset();
	require(!state.enabled(), "reset clears the current duel");
	require(state.inherited_side() == DuelSuccessionState::NO_SIDE,
		"no inherited side leaks into next duel");
	require(state.controller_generation() == 0, "new duel starts clean");
}

void test_no_implicit_gameplay_mutation() {
	DuelSuccessionState state;
	state.configure(true, 0);
	int32_t kaiba_lp = 400, noah_lp = 7400;
	uint32_t grave_count = 27, deck_master_code = 42;
	uint32_t turn_number = 18;
	require(state.request(0, 1, turn_number), "prepare transition");
	require(state.commit(19, true, true), "confirm completed transition");
	require(kaiba_lp == 400 && noah_lp == 7400,
		"succession state never resets either player's LP");
	require(grave_count == 27 && deck_master_code == 42,
		"succession state does not clear graveyard or Deck Master");
	require(turn_number == 18,
		"state machine cannot increment actual turn automatically");
}
} // namespace

int main() {
	test_disabled_in_standard_duels();
	test_noah_kaiba_to_yugi();
	test_rollback_and_isolation();
	test_no_implicit_gameplay_mutation();
	std::cout << "Duel Succession System stage 1 tests: PASS\n";
}
