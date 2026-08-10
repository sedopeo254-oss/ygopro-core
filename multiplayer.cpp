/*
 * Copyright (c) 2026 The EDOPro multiplayer modes contributors
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "multiplayer.h"

#include <algorithm>

void MultiplayerState::configure(MultiplayerMode new_mode) {
	reset();
	duel_mode = new_mode;
	if(new_mode == MultiplayerMode::BATTLE_ROYALE) {
		configured_players = 4;
		players_on_side[0] = 2;
		players_on_side[1] = 2;
		players_mask = 0x0f;
		for(uint8_t player = 0; player < configured_players; ++player)
			teams[player] = player;
		// Network seats are A1, A2, B1, B2. The requested round order is
		// A1 -> B1 -> A2 -> B2.
		turn_order[0] = 0;
		turn_order[1] = 2;
		turn_order[2] = 1;
		turn_order[3] = 3;
		turn_player = 0;
	} else if(new_mode == MultiplayerMode::THREE_V_ONE) {
		configured_players = 4;
		players_on_side[0] = 3;
		players_on_side[1] = 1;
		players_mask = 0x0f;
		// Anime order: Serenity -> Tristan -> Duke -> Nezbitt. The first
		// three network seats form the allied team and the final seat is solo.
		teams[0] = 0;
		teams[1] = 0;
		teams[2] = 0;
		teams[3] = 1;
		turn_player = 0;
	}
}

bool MultiplayerState::configure_universal(const UniversalConfig& config) {
	reset();
	if(config.format != UniversalMultiplayerFormat::SOLO
			&& config.format != UniversalMultiplayerFormat::TEAMS
			&& config.format != UniversalMultiplayerFormat::BATTLE_ROYALE)
		return false;
	if(config.side_one_players == 0 || config.side_two_players == 0
			|| config.side_one_players > MAX_PLAYERS_PER_SIDE
			|| config.side_two_players > MAX_PLAYERS_PER_SIDE)
		return false;

	const auto total = static_cast<uint8_t>(config.side_one_players + config.side_two_players);
	const auto available_mask = mask_for_players(total);
	const auto initial_mask = config.initial_active_mask
		? config.initial_active_mask : available_mask;
	if((initial_mask & ~available_mask) != 0 || count_bits(initial_mask) < 2)
		return false;

	if(config.format == UniversalMultiplayerFormat::TEAMS) {
		player_mask_t team_mask = 0;
		for(uint8_t player = 0; player < total; ++player) {
			if(config.teams[player] >= MAX_TEAMS)
				return false;
			team_mask |= static_cast<player_mask_t>(1u) << config.teams[player];
		}
		if(count_bits(team_mask) < 2)
			return false;
	}

	duel_mode = MultiplayerMode::UNIVERSAL;
	universal_duel_format = config.format;
	configured_players = total;
	players_on_side[0] = config.side_one_players;
	players_on_side[1] = config.side_two_players;
	players_mask = initial_mask;
	arc_v_rules = config.arc_v_first_turn_rules;
	intrusion_enabled = config.allow_intrusion;
	for(uint8_t player = 0; player < configured_players; ++player) {
		teams[player] = config.format == UniversalMultiplayerFormat::TEAMS
			? config.teams[player] : player;
	}
	configure_turn_order();
	for(uint8_t player = 0; player < configured_players; ++player) {
		if(is_active(turn_order[player])) {
			turn_player = turn_order[player];
			break;
		}
	}
	return turn_player != NO_PLAYER;
}

void MultiplayerState::reset() {
	duel_mode = MultiplayerMode::NONE;
	universal_duel_format = UniversalMultiplayerFormat::SOLO;
	players_mask = 0;
	completed_first_turn_mask = 0;
	intrusion_mask = 0;
	teams.fill(NO_TEAM);
	for(uint8_t player = 0; player < MAX_PLAYERS; ++player)
		turn_order[player] = player;
	reasons.fill(PlayerEliminationReason::LP);
	configured_players = 0;
	players_on_side[0] = 0;
	players_on_side[1] = 0;
	arc_v_rules = false;
	intrusion_enabled = false;
	winning_player = NO_PLAYER;
	winning_team = NO_TEAM;
	turn_player = NO_PLAYER;
}

MultiplayerMode MultiplayerState::mode() const {
	return duel_mode;
}

UniversalMultiplayerFormat MultiplayerState::universal_format() const {
	return universal_duel_format;
}

bool MultiplayerState::uses_arc_v_first_turn_rules() const {
	return arc_v_rules;
}

bool MultiplayerState::allows_intrusion() const {
	return intrusion_enabled;
}

bool MultiplayerState::enabled() const {
	return duel_mode != MultiplayerMode::NONE;
}

bool MultiplayerState::uses_independent_fields() const {
	return duel_mode == MultiplayerMode::BATTLE_ROYALE
		|| duel_mode == MultiplayerMode::UNIVERSAL;
}

bool MultiplayerState::are_opponents(uint8_t first, uint8_t second) const {
	return first < configured_players && second < configured_players
		&& first != second && teams[first] != NO_TEAM
		&& teams[second] != NO_TEAM && teams[first] != teams[second];
}

bool MultiplayerState::can_intercept(uint8_t source, uint8_t victim,
		uint8_t candidate) const {
	if(!is_active(candidate) || candidate == source || candidate == victim
			|| victim >= configured_players)
		return false;
	if(duel_mode == MultiplayerMode::BATTLE_ROYALE)
		return true;
	if(duel_mode == MultiplayerMode::THREE_V_ONE)
		return teams[candidate] != NO_TEAM && teams[candidate] == teams[victim];
	if(duel_mode != MultiplayerMode::UNIVERSAL)
		return false;
	if(universal_duel_format == UniversalMultiplayerFormat::BATTLE_ROYALE)
		return true;
	if(universal_duel_format == UniversalMultiplayerFormat::TEAMS)
		return teams[candidate] != NO_TEAM && teams[candidate] == teams[victim];
	return false;
}

uint8_t MultiplayerState::player_count() const {
	return configured_players;
}

MultiplayerState::player_mask_t MultiplayerState::active_mask() const {
	return players_mask;
}

uint8_t MultiplayerState::active_count() const {
	return count_bits(players_mask);
}

bool MultiplayerState::is_active(uint8_t player) const {
	return player < MAX_PLAYERS && (players_mask & (1u << player));
}

uint8_t MultiplayerState::team_of(uint8_t player) const {
	return player < configured_players ? teams[player] : NO_TEAM;
}

uint8_t MultiplayerState::field_side_of(uint8_t player) const {
	if(player >= configured_players || !enabled())
		return NO_PLAYER;
	return player < players_on_side[0] ? 0 : 1;
}

uint8_t MultiplayerState::field_count(uint8_t field_side) const {
	if(!enabled() || field_side > 1)
		return 0;
	return players_on_side[field_side];
}

uint8_t MultiplayerState::duelist_index_of(uint8_t player) const {
	if(player >= configured_players || !enabled())
		return NO_PLAYER;
	return player < players_on_side[0]
		? player : static_cast<uint8_t>(player - players_on_side[0]);
}

uint8_t MultiplayerState::logical_player(uint8_t field_side, uint8_t duelist_index) const {
	if(!enabled() || field_side > 1)
		return NO_PLAYER;
	if(duelist_index >= players_on_side[field_side])
		return NO_PLAYER;
	return field_side == 0 ? duelist_index
		: static_cast<uint8_t>(players_on_side[0] + duelist_index);
}

uint8_t MultiplayerState::prompt_player_of(uint8_t player) const {
	if(player >= configured_players || !enabled())
		return NO_PLAYER;
	if(duel_mode == MultiplayerMode::BATTLE_ROYALE || duel_mode == MultiplayerMode::UNIVERSAL)
		return static_cast<uint8_t>(player + 2);
	if(duel_mode == MultiplayerMode::THREE_V_ONE && player < 3)
		return static_cast<uint8_t>(player + 2);
	return field_side_of(player);
}

uint32_t MultiplayerState::encode_zone_sequence(uint8_t field_side, uint8_t duelist_index, uint8_t stride, uint32_t local_sequence) const {
	if(!enabled() || field_count(field_side) <= 1 || duelist_index >= field_count(field_side)
			|| !stride || local_sequence >= stride)
		return local_sequence;
	return static_cast<uint32_t>(duelist_index) * stride + local_sequence;
}

uint32_t MultiplayerState::local_zone_sequence(uint8_t field_side, uint8_t stride, uint32_t sequence) const {
	if(!enabled() || field_count(field_side) <= 1 || !stride)
		return sequence;
	return sequence % stride;
}

uint8_t MultiplayerState::zone_duelist_index(uint8_t field_side, uint8_t stride, uint32_t sequence) const {
	if(!enabled() || field_count(field_side) <= 1 || !stride)
		return 0;
	const auto duelist = static_cast<uint8_t>(sequence / stride);
	return duelist < field_count(field_side) ? duelist : 0;
}

uint8_t MultiplayerState::current_player() const {
	return turn_player;
}

uint8_t MultiplayerState::advance_turn() {
	if(!enabled() || has_winner())
		return NO_PLAYER;
	complete_turn(turn_player);
	turn_player = next_active_player(turn_player);
	return turn_player;
}

uint8_t MultiplayerState::next_active_player(uint8_t player) const {
	if(!enabled() || players_mask == 0)
		return NO_PLAYER;

	std::size_t current_index = configured_players;
	for(std::size_t index = 0; index < configured_players; ++index) {
		if(turn_order[index] == player) {
			current_index = index;
			break;
		}
	}
	if(current_index == configured_players)
		return NO_PLAYER;

	for(std::size_t offset = 1; offset <= configured_players; ++offset) {
		const uint8_t candidate = turn_order[(current_index + offset) % configured_players];
		if(is_active(candidate))
			return candidate;
	}
	return NO_PLAYER;
}

bool MultiplayerState::complete_turn(uint8_t player) {
	if(player >= configured_players || !is_active(player))
		return false;
	completed_first_turn_mask |= static_cast<player_mask_t>(1u) << player;
	return true;
}

bool MultiplayerState::can_attack_on_current_turn(uint8_t player) const {
	if(player >= configured_players || !is_active(player))
		return false;
	if(duel_mode != MultiplayerMode::UNIVERSAL
			|| universal_duel_format != UniversalMultiplayerFormat::BATTLE_ROYALE)
		return true;
	return (completed_first_turn_mask & (static_cast<player_mask_t>(1u) << player)) != 0;
}

bool MultiplayerState::can_draw_on_current_turn(uint8_t player) const {
	if(player >= configured_players || !is_active(player))
		return false;
	if(duel_mode != MultiplayerMode::UNIVERSAL
			|| universal_duel_format != UniversalMultiplayerFormat::BATTLE_ROYALE
			|| !arc_v_rules)
		return true;
	return (completed_first_turn_mask & (static_cast<player_mask_t>(1u) << player)) != 0;
}

bool MultiplayerState::activate_intruder(uint8_t player) {
	if(duel_mode != MultiplayerMode::UNIVERSAL
			|| universal_duel_format != UniversalMultiplayerFormat::BATTLE_ROYALE
			|| !intrusion_enabled || player >= configured_players || is_active(player)
			|| is_finished())
		return false;
	const auto bit = static_cast<player_mask_t>(1u) << player;
	players_mask |= bit;
	intrusion_mask |= bit;
	return true;
}

bool MultiplayerState::is_intruder(uint8_t player) const {
	return player < configured_players
		&& (intrusion_mask & (static_cast<player_mask_t>(1u) << player)) != 0;
}

uint32_t MultiplayerState::starting_lp_for(uint8_t player, uint32_t normal_starting_lp) const {
	return is_intruder(player) ? normal_starting_lp / 2u : normal_starting_lp;
}

bool MultiplayerState::eliminate(uint8_t player, PlayerEliminationReason reason) {
	if(player >= configured_players)
		return false;
	auto player_reasons = reasons;
	player_reasons[player] = reason;
	return eliminate_many(static_cast<player_mask_t>(1u) << player, player_reasons) != 0;
}

MultiplayerState::player_mask_t MultiplayerState::eliminate_many(player_mask_t player_mask,
		const std::array<PlayerEliminationReason, MAX_PLAYERS>& player_reasons) {
	if(!enabled() || is_finished())
		return 0;
	const player_mask_t eliminated = player_mask & players_mask & mask_for_players(configured_players);
	if(!eliminated)
		return 0;
	players_mask &= ~eliminated;
	for(uint8_t player = 0; player < configured_players; ++player) {
		if(eliminated & (static_cast<player_mask_t>(1u) << player))
			reasons[player] = player_reasons[player];
	}
	update_winner();
	return eliminated;
}

PlayerEliminationReason MultiplayerState::elimination_reason(uint8_t player) const {
	return player < configured_players ? reasons[player] : PlayerEliminationReason::EFFECT;
}

bool MultiplayerState::has_winner() const {
	return winning_player != NO_PLAYER || winning_team != NO_TEAM;
}

bool MultiplayerState::is_draw() const {
	return enabled() && players_mask == 0;
}

bool MultiplayerState::is_finished() const {
	return has_winner() || is_draw();
}

uint8_t MultiplayerState::winner_player() const {
	return winning_player;
}

uint8_t MultiplayerState::winner_team() const {
	return winning_team;
}

uint8_t MultiplayerState::count_bits(player_mask_t value) {
	uint8_t count = 0;
	while(value) {
		count += value & 1u;
		value >>= 1u;
	}
	return count;
}

MultiplayerState::player_mask_t MultiplayerState::mask_for_players(uint8_t count) {
	return count == 0 ? 0 : (static_cast<player_mask_t>(1u) << count) - 1u;
}

MultiplayerState::player_mask_t MultiplayerState::active_teams_mask() const {
	player_mask_t mask = 0;
	for(uint8_t player = 0; player < configured_players; ++player) {
		if(is_active(player) && teams[player] != NO_TEAM)
			mask |= static_cast<player_mask_t>(1u) << teams[player];
	}
	return mask;
}

void MultiplayerState::update_winner() {
	winning_player = NO_PLAYER;
	winning_team = NO_TEAM;
	if(duel_mode == MultiplayerMode::BATTLE_ROYALE
			|| (duel_mode == MultiplayerMode::UNIVERSAL
				&& universal_duel_format != UniversalMultiplayerFormat::TEAMS)) {
		if(active_count() != 1)
			return;
		for(uint8_t player = 0; player < configured_players; ++player) {
			if(is_active(player)) {
				winning_player = player;
				winning_team = teams[player];
				return;
			}
		}
		return;
	}
	if(duel_mode == MultiplayerMode::THREE_V_ONE
			|| (duel_mode == MultiplayerMode::UNIVERSAL
				&& universal_duel_format == UniversalMultiplayerFormat::TEAMS)) {
		const player_mask_t team_mask = active_teams_mask();
		if(team_mask == 0 || count_bits(team_mask) != 1)
			return;
		for(uint8_t team = 0; team < MAX_TEAMS; ++team) {
			if(team_mask & (static_cast<player_mask_t>(1u) << team)) {
				winning_team = team;
				if(duel_mode == MultiplayerMode::THREE_V_ONE && team == 1 && is_active(3))
					winning_player = 3;
				return;
			}
		}
	}
}

void MultiplayerState::configure_turn_order() {
	uint8_t index = 0;
	const auto longest_side = std::max(players_on_side[0], players_on_side[1]);
	for(uint8_t duelist = 0; duelist < longest_side; ++duelist) {
		if(duelist < players_on_side[0])
			turn_order[index++] = duelist;
		if(duelist < players_on_side[1])
			turn_order[index++] = static_cast<uint8_t>(players_on_side[0] + duelist);
	}
}
