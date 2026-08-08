#include "multiplayer.h"

#include <cstdlib>
#include <iostream>

namespace {
void expect(bool condition, const char* message) {
	if(condition)
		return;
	std::cerr << "FAILED: " << message << '\n';
	std::exit(1);
}

void test_battle_royale_turn_order_and_skip() {
	MultiplayerState state;
	state.configure(MultiplayerMode::BATTLE_ROYALE);
	expect(state.active_mask() == 0x0f, "Battle Royale must start with four active players");
	expect(state.next_active_player(0) == 2, "A1 must pass to B1");
	expect(state.next_active_player(2) == 1, "B1 must pass to A2");
	expect(state.next_active_player(1) == 3, "A2 must pass to B2");
	expect(state.next_active_player(3) == 0, "B2 must pass to A1");
	expect(state.current_player() == 0, "Battle Royale must start with A1");
	expect(state.advance_turn() == 2, "the logical turn must advance from A1 to B1");
	expect(state.advance_turn() == 1, "the logical turn must advance from B1 to A2");
	expect(state.field_side_of(0) == 0 && state.field_side_of(1) == 0,
		"A1 and A2 must use field side 0");
	expect(state.field_side_of(2) == 1 && state.field_side_of(3) == 1,
		"B1 and B2 must use field side 1");
	expect(state.field_count(0) == 2 && state.field_count(1) == 2,
		"Battle Royale must preserve two independent fields on each core side");
	expect(state.encode_zone_sequence(0, 0, 7, 3) == 3
		&& state.encode_zone_sequence(0, 1, 7, 3) == 10
		&& state.encode_zone_sequence(1, 0, 8, 4) == 4
		&& state.encode_zone_sequence(1, 1, 8, 4) == 12,
		"Battle Royale players sharing a core side must receive distinct internal zones");
	expect(state.local_zone_sequence(1, 8, 12) == 4
			&& state.zone_duelist_index(1, 8, 12) == 1,
		"Battle Royale internal zones must decode to the correct local field");
	expect(state.prompt_player_of(0) == 2 && state.prompt_player_of(1) == 3
			&& state.prompt_player_of(2) == 4 && state.prompt_player_of(3) == 5,
		"Battle Royale prompts must route to each logical player's own client");
	expect(state.logical_player(1, 0) == 2 && state.logical_player(1, 1) == 3,
		"Battle Royale side-1 duelist mapping must be stable");
	expect(state.can_intercept(0, 2, 1),
		"a third Battle Royale player must be allowed to take an attack");
	expect(!state.can_intercept(0, 2, 0) && !state.can_intercept(0, 2, 2),
		"an attacker or victim cannot intercept their own attack");

	expect(state.eliminate(2, PlayerEliminationReason::LP), "player 2 should be eliminated");
	expect(state.active_mask() == 0x0b, "eliminating player 2 must produce active mask 0x0B");
	expect(state.next_active_player(0) == 1, "the eliminated B1 seat must be skipped");
	expect(state.next_active_player(1) == 3, "the remaining order must continue to B2");
	expect(state.next_active_player(3) == 0, "the remaining order must wrap to A1");
	expect(!state.has_winner(), "three active players must not end Battle Royale");
}

void test_battle_royale_multi_elimination() {
	MultiplayerState state;
	state.configure(MultiplayerMode::BATTLE_ROYALE);
	expect(state.eliminate(1, PlayerEliminationReason::LP), "player 1 elimination must succeed");
	expect(state.eliminate(2, PlayerEliminationReason::DECK), "player 2 elimination must succeed");
	expect(!state.has_winner(), "two active players must not end Battle Royale");
	expect(state.eliminate(3, PlayerEliminationReason::SURRENDER), "player 3 elimination must succeed");
	expect(state.has_winner(), "one remaining player must end Battle Royale");
	expect(state.winner_player() == 0, "player 0 must be the final winner");
	expect(state.active_mask() == 0x01, "only player 0 must remain active");
	expect(!state.eliminate(0, PlayerEliminationReason::EFFECT), "the winner cannot be eliminated after completion");
}

void test_three_vs_one_team_winner() {
	MultiplayerState state;
	state.configure(MultiplayerMode::THREE_V_ONE);
	expect(state.team_of(0) == 0 && state.team_of(1) == 0 && state.team_of(2) == 0,
		"players 0, 1 and 2 must form the three-player team");
	expect(state.team_of(3) == 1, "player 3 must be the solo team");
	expect(state.field_side_of(0) == 0 && state.field_side_of(3) == 1,
		"the three-player team and solo player must use different field sides");
	expect(state.field_count(0) == 3 && state.field_count(1) == 1,
		"the allied side must own three fields while Nezbitt owns one");
	expect(state.encode_zone_sequence(0, 0, 7, 2) == 2
		&& state.encode_zone_sequence(0, 1, 7, 2) == 9
		&& state.encode_zone_sequence(0, 2, 7, 2) == 16,
		"the same local monster zone must map to a distinct slot for each ally");
	expect(state.local_zone_sequence(0, 7, 16) == 2
		&& state.zone_duelist_index(0, 7, 16) == 2,
		"an internal allied slot must decode back to Duke's local field");
	expect(state.duelist_index_of(2) == 2 && state.logical_player(0, 2) == 2,
		"the allied duelist mapping must preserve Duke's seat");
	expect(state.duelist_index_of(3) == 0 && state.logical_player(1, 0) == 3,
		"the solo field must map to Nezbitt's seat");
	expect(state.prompt_player_of(0) == 2 && state.prompt_player_of(1) == 3
		&& state.prompt_player_of(2) == 4,
		"each allied Deck Master prompt must route to its own network seat");
	expect(state.prompt_player_of(3) == 1,
		"the solo Deck Master prompt must route to the opposing network side");
	expect(state.next_active_player(0) == 1 && state.next_active_player(1) == 2
		&& state.next_active_player(2) == 3 && state.next_active_player(3) == 0,
		"3 vs 1 must follow Serenity, Tristan, Duke, then Nezbitt");
	expect(state.can_intercept(3, 0, 1),
		"a surviving 3 vs 1 teammate must be able to protect the victim");
	expect(!state.can_intercept(0, 3, 1),
		"the allied trio must not intercept damage aimed at their opponent");
	expect(state.uses_logical_effect_scopes(),
		"3 vs 1 card effects must distinguish the three allied fields");
	expect(state.shares_card_effect_scope(1, 1)
			&& !state.shares_card_effect_scope(1, 0),
		"a script's own field must mean the exact logical duelist only");
	expect(state.is_other_card_effect_player(1, 0)
			&& state.is_other_card_effect_player(1, 2)
			&& state.is_other_card_effect_player(1, 3)
			&& !state.are_opponents(1, 0)
			&& state.are_opponents(1, 3),
		"3 vs 1 card scope must reach every other duelist without changing team relations");
	expect(state.eliminate(0, PlayerEliminationReason::LP), "first team member elimination must succeed");
	expect(!state.has_winner(), "one eliminated team member must not end 3 vs 1");
	expect(state.is_card_effect_player_available(0)
			&& state.is_other_card_effect_player(1, 0),
		"an eliminated anime teammate's persistent field must remain addressable by card effects");
	expect(state.eliminate(1, PlayerEliminationReason::LP), "second team member elimination must succeed");
	expect(!state.has_winner(), "two eliminated team members must not end 3 vs 1");
	expect(state.eliminate(2, PlayerEliminationReason::LP), "last team member elimination must succeed");
	expect(state.has_winner(), "eliminating the full team must end 3 vs 1");
	expect(state.winner_team() == 1 && state.winner_player() == 3, "Nezbitt must win as the solo team");

	MultiplayerState opposing_win;
	opposing_win.configure(MultiplayerMode::THREE_V_ONE);
	expect(opposing_win.eliminate(3, PlayerEliminationReason::LP), "solo player elimination must succeed");
	expect(opposing_win.has_winner(), "eliminating the solo player must end 3 vs 1");
	expect(opposing_win.winner_team() == 0, "the three-player team must win");
	expect(opposing_win.winner_player() == MultiplayerState::NO_PLAYER,
		"a team victory must not invent an individual winner");
}

void test_disabled_state_is_inert() {
	MultiplayerState state;
	expect(!state.enabled(), "the default state must be disabled");
	expect(state.active_mask() == 0, "the disabled state must not expose active players");
	expect(state.next_active_player(0) == MultiplayerState::NO_PLAYER,
		"the disabled state must not provide a turn player");
	expect(!state.eliminate(0, PlayerEliminationReason::LP),
		"the disabled state must reject eliminations");
}

void test_simultaneous_elimination_draw() {
	MultiplayerState state;
	state.configure(MultiplayerMode::BATTLE_ROYALE);
	auto reasons = std::array<PlayerEliminationReason, MultiplayerState::MAX_PLAYERS>{
		PlayerEliminationReason::LP,
		PlayerEliminationReason::LP,
		PlayerEliminationReason::DECK,
		PlayerEliminationReason::LP
	};
	expect(state.eliminate_many(0x0f, reasons) == 0x0f,
		"a simultaneous elimination must remove every active player");
	expect(state.is_draw(), "zero active players must be represented as a draw");
	expect(state.is_finished(), "a draw must finish the multiplayer duel");
	expect(!state.has_winner(), "a simultaneous draw must not invent a winner");
}

void test_universal_13_vs_13_solo() {
	MultiplayerState state;
	MultiplayerState::UniversalConfig config;
	config.side_one_players = 13;
	config.side_two_players = 13;
	config.format = UniversalMultiplayerFormat::SOLO;
	expect(state.configure_universal(config), "13 vs 13 Solo must be a valid universal configuration");
	expect(state.mode() == MultiplayerMode::UNIVERSAL, "the universal configuration must select its own mode");
	expect(state.player_count() == 26, "13 vs 13 must configure all 26 logical players");
	expect(state.active_count() == 26 && state.active_mask() == 0x03ffffffu,
		"all 26 Solo seats must start active");
	expect(state.field_count(0) == 13 && state.field_count(1) == 13,
		"each physical core side must own 13 independent fields");
	expect(state.field_side_of(12) == 0 && state.duelist_index_of(12) == 12,
		"the final first-side player must map to field 12 on side zero");
	expect(state.field_side_of(13) == 1 && state.duelist_index_of(13) == 0
			&& state.field_side_of(25) == 1 && state.duelist_index_of(25) == 12,
		"the second group must map to the 13 independent fields on side one");
	expect(state.logical_player(1, 12) == 25,
		"the final side-one field must decode to logical player 25");
	expect(state.next_active_player(0) == 13 && state.next_active_player(13) == 1
			&& state.next_active_player(12) == 25 && state.next_active_player(25) == 0,
		"the 26-player turn order must alternate between both physical sides");
	expect(state.encode_zone_sequence(0, 12, 7, 6) == 90
			&& state.local_zone_sequence(0, 7, 90) == 6
			&& state.zone_duelist_index(0, 7, 90) == 12,
		"the thirteenth field must keep its monster zones independent");
	expect(state.prompt_player_of(25) == 27,
		"logical player 25 must receive a unique extended prompt route");
	expect(!state.can_intercept(0, 13, 1),
		"Universal Single Duel players must fight independently without team interception");

	for(uint8_t player = 1; player < 26; ++player)
		expect(state.eliminate(player, PlayerEliminationReason::LP),
			"each universal Solo opponent must be independently eliminable");
	expect(state.has_winner() && state.winner_player() == 0,
		"the final remaining Solo player must win");
}

void test_universal_multiple_teams() {
	MultiplayerState state;
	MultiplayerState::UniversalConfig config;
	config.side_one_players = 3;
	config.side_two_players = 3;
	config.format = UniversalMultiplayerFormat::TEAMS;
	config.teams[0] = 0;
	config.teams[1] = 1;
	config.teams[2] = 2;
	config.teams[3] = 0;
	config.teams[4] = 1;
	config.teams[5] = 2;
	expect(state.configure_universal(config), "three independent teams must be accepted");
	expect(state.team_of(0) == 0 && state.team_of(3) == 0
			&& state.team_of(2) == 2 && state.team_of(5) == 2,
		"team assignments must not depend on the physical core side");
	expect(state.uses_independent_fields(),
		"universal team players must retain independent fields");
	expect(!state.are_opponents(0, 3),
		"members of the same universal team must be allies");
	expect(state.are_opponents(0, 1),
		"members of different universal teams must be opponents");
	expect(state.can_intercept(0, 1, 4),
		"a teammate on either core side must be able to protect the victim");
	expect(!state.can_intercept(0, 1, 2),
		"a third-team opponent must not receive a team interception prompt");
	expect(state.eliminate(0, PlayerEliminationReason::LP), "team zero player one must be eliminable");
	expect(state.eliminate(3, PlayerEliminationReason::LP), "team zero player two must be eliminable");
	expect(!state.has_winner(), "two active teams must keep the Duel running");
	expect(state.eliminate(1, PlayerEliminationReason::LP), "team one player one must be eliminable");
	expect(state.eliminate(4, PlayerEliminationReason::LP), "team one player two must be eliminable");
	expect(state.has_winner() && state.winner_team() == 2,
		"the last team with active players must win");
	expect(state.winner_player() == MultiplayerState::NO_PLAYER,
		"a multi-player team victory must not invent an individual winner");
}

void test_universal_battle_royal_first_turn_and_intrusion_rules() {
	MultiplayerState state;
	MultiplayerState::UniversalConfig config;
	config.side_one_players = 2;
	config.side_two_players = 2;
	config.format = UniversalMultiplayerFormat::BATTLE_ROYALE;
	config.arc_v_first_turn_rules = true;
	config.allow_intrusion = true;
	config.initial_active_mask = 0x05;
	expect(state.configure_universal(config), "a reserved four-seat Battle Royal must be valid");
	expect(!state.can_attack_on_current_turn(0), "a player cannot attack on their first personal turn");
	expect(!state.can_draw_on_current_turn(0), "ARC-V rules must prevent the first personal draw");
	expect(state.advance_turn() == 2, "the first active opponent must receive the next turn");
	expect(state.can_attack_on_current_turn(0) && state.can_draw_on_current_turn(0),
		"the restriction must end after that player completes their first turn");
	expect(!state.can_attack_on_current_turn(2) && !state.can_draw_on_current_turn(2),
		"every other player must retain their own first-turn restriction");
	expect(state.activate_intruder(1), "a reserved seat must be able to intrude into an ongoing Battle Royal");
	expect(state.is_active(1) && state.is_intruder(1), "the intruding seat must become independently active");
	expect(state.can_intercept(0, 2, 1),
		"an active Universal Battle Royal player may cooperate by taking an attack");
	expect(state.starting_lp_for(1, 4000) == 2000,
		"an ARC-V intrusion must halve the joining player's starting LP");
	expect(state.starting_lp_for(0, 4000) == 4000,
		"existing players must keep their original starting LP");
	expect(!state.activate_intruder(1), "an already active player cannot intrude twice");
}

void test_invalid_universal_configurations() {
	MultiplayerState state;
	MultiplayerState::UniversalConfig config;
	config.side_one_players = 14;
	expect(!state.configure_universal(config), "a physical side cannot exceed 13 players");
	config.side_one_players = 2;
	config.side_two_players = 2;
	config.format = UniversalMultiplayerFormat::TEAMS;
	expect(!state.configure_universal(config), "team mode must contain at least two different teams");
	config.format = UniversalMultiplayerFormat::SOLO;
	config.initial_active_mask = 1;
	expect(!state.configure_universal(config), "a Duel cannot start with fewer than two active players");
	config.initial_active_mask = 0;
	config.format = static_cast<UniversalMultiplayerFormat>(0xff);
	expect(!state.configure_universal(config), "an unknown universal format must be rejected");
}
}

int main() {
	test_battle_royale_turn_order_and_skip();
	test_battle_royale_multi_elimination();
	test_three_vs_one_team_winner();
	test_disabled_state_is_inert();
	test_simultaneous_elimination_draw();
	test_universal_13_vs_13_solo();
	test_universal_multiple_teams();
	test_universal_battle_royal_first_turn_and_intrusion_rules();
	test_invalid_universal_configurations();
	std::cout << "All multiplayer state tests passed.\n";
	return 0;
}
