/*
 * Copyright (c) 2026 The EDOPro multiplayer modes contributors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef MULTIPLAYER_H
#define MULTIPLAYER_H

#include <array>
#include <cstddef>
#include <cstdint>

enum class MultiplayerMode : uint8_t {
	NONE = 0,
	BATTLE_ROYALE,
	THREE_V_ONE,
	UNIVERSAL
};

enum class UniversalMultiplayerFormat : uint8_t {
	SOLO = 0,
	TEAMS,
	BATTLE_ROYALE
};

enum class PlayerEliminationReason : uint8_t {
	LP = 1,
	DECK = 2,
	SURRENDER = 3,
	EFFECT = 4
};

class MultiplayerState {
public:
	using player_mask_t = uint32_t;
	static constexpr uint8_t MAX_PLAYERS = 26;
	static constexpr uint8_t MAX_PLAYERS_PER_SIDE = 13;
	static constexpr uint8_t MAX_TEAMS = 26;
	static constexpr uint8_t NO_PLAYER = 0xff;
	static constexpr uint8_t NO_TEAM = 0xff;

	struct UniversalConfig {
		uint8_t side_one_players{ 1 };
		uint8_t side_two_players{ 1 };
		UniversalMultiplayerFormat format{ UniversalMultiplayerFormat::SOLO };
		bool arc_v_first_turn_rules{ false };
		bool allow_intrusion{ false };
		// Zero means every configured seat starts active. A non-zero mask can
		// reserve inactive seats for players that intrude into an ongoing Duel.
		player_mask_t initial_active_mask{ 0 };
		std::array<uint8_t, MAX_PLAYERS> teams{};
	};

	void configure(MultiplayerMode new_mode);
	bool configure_universal(const UniversalConfig& config);
	void reset();

	MultiplayerMode mode() const;
	UniversalMultiplayerFormat universal_format() const;
	bool uses_arc_v_first_turn_rules() const;
	bool allows_intrusion() const;
	bool enabled() const;
	bool uses_independent_fields() const;
	// Legacy 3-vs-1 stores several independently owned fields on the same
	// physical core side. Card queries still need a logical-player scope there,
	// just like Battle Royale and Universal Multiplayer.
	bool uses_logical_effect_scopes() const;
	// "Your" cards always belong to the exact logical duelist. This remains
	// true even when several 3-vs-1 duelists share core side 0.
	bool shares_card_effect_scope(uint8_t first, uint8_t second) const;
	// Eliminated 3-vs-1 duelists leave their cards on the shared anime field,
	// so those cards remain addressable even though the duelist no longer takes
	// turns. Other multiplayer formats expose only active logical duelists.
	bool is_card_effect_player_available(uint8_t player) const;
	// Returns whether a card's normal "other player" scope reaches the target.
	// In anime 3-vs-1, legacy `1-tp` card queries reach every other logical
	// duelist (for example Block Attack can choose a teammate or Nezbitt). Other
	// modes keep true opponent semantics, and team/win relations stay unchanged.
	bool is_other_card_effect_player(uint8_t source, uint8_t target) const;
	bool are_opponents(uint8_t first, uint8_t second) const;
	bool can_intercept(uint8_t source, uint8_t victim, uint8_t candidate) const;
	uint8_t player_count() const;
	player_mask_t active_mask() const;
	uint8_t active_count() const;
	bool is_active(uint8_t player) const;
	uint8_t team_of(uint8_t player) const;
	uint8_t field_side_of(uint8_t player) const;
	uint8_t field_count(uint8_t field_side) const;
	uint8_t duelist_index_of(uint8_t player) const;
	uint8_t logical_player(uint8_t field_side, uint8_t duelist_index) const;
	uint8_t prompt_player_of(uint8_t player) const;
	uint32_t encode_zone_sequence(uint8_t field_side, uint8_t duelist_index, uint8_t stride, uint32_t local_sequence) const;
	uint32_t local_zone_sequence(uint8_t field_side, uint8_t stride, uint32_t sequence) const;
	uint8_t zone_duelist_index(uint8_t field_side, uint8_t stride, uint32_t sequence) const;
	uint8_t current_player() const;
	uint8_t advance_turn();
	uint8_t next_active_player(uint8_t player) const;
	bool complete_turn(uint8_t player);
	bool can_attack_on_current_turn(uint8_t player) const;
	bool can_draw_on_current_turn(uint8_t player) const;
	bool activate_intruder(uint8_t player);
	bool is_intruder(uint8_t player) const;
	uint32_t starting_lp_for(uint8_t player, uint32_t normal_starting_lp) const;

	bool eliminate(uint8_t player, PlayerEliminationReason reason);
	player_mask_t eliminate_many(player_mask_t player_mask,
		const std::array<PlayerEliminationReason, MAX_PLAYERS>& player_reasons);
	PlayerEliminationReason elimination_reason(uint8_t player) const;

	bool has_winner() const;
	bool is_draw() const;
	bool is_finished() const;
	uint8_t winner_player() const;
	uint8_t winner_team() const;

private:
	static uint8_t count_bits(player_mask_t value);
	static player_mask_t mask_for_players(uint8_t count);
	player_mask_t active_teams_mask() const;
	void update_winner();
	void configure_turn_order();

	MultiplayerMode duel_mode{ MultiplayerMode::NONE };
	UniversalMultiplayerFormat universal_duel_format{ UniversalMultiplayerFormat::SOLO };
	player_mask_t players_mask{ 0 };
	player_mask_t completed_first_turn_mask{ 0 };
	player_mask_t intrusion_mask{ 0 };
	std::array<uint8_t, MAX_PLAYERS> teams{};
	std::array<uint8_t, MAX_PLAYERS> turn_order{};
	std::array<PlayerEliminationReason, MAX_PLAYERS> reasons{};
	uint8_t configured_players{ 0 };
	uint8_t players_on_side[2]{ 0, 0 };
	bool arc_v_rules{ false };
	bool intrusion_enabled{ false };
	uint8_t winning_player{ NO_PLAYER };
	uint8_t winning_team{ NO_TEAM };
	uint8_t turn_player{ NO_PLAYER };
};

#endif // MULTIPLAYER_H
