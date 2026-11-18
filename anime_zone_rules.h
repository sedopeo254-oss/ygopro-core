/*
 * Native Anime Field Zone Policy - independent from all duel modes.
 * Copyright (C) 2026, EDOPro custom anime extensions
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This model is pure and side-effect-free. It does not alter private piles,
 * card locations, attack counts, turn progression or network messages.
 * The server may build one profile for an active Field Spell, including
 * expanded custom mechanics, without replacing OCGCore's battle engine.
 */
#ifndef ANIME_ZONE_RULES_H
#define ANIME_ZONE_RULES_H

#include <cstdint>

namespace anime_zone_rules {

enum class Row : uint8_t {
	OUTSIDE_FIELD = 0,
	FRONT_MONSTER = 1,
	REAR_SPELL_TRAP = 2
};

// Optional modular mechanics; individual Field Spells opt in independently.
enum Feature : uint32_t {
	NONE = 0u,
	REAR_MONSTER = 1u << 0,        // S/T card may operate as a monster
	FRONT_GUARDS_REAR = 1u << 1,   // rear monsters hidden while front occupied
	CROSS_ROW_MOVEMENT = 1u << 2,  // movement between monster and S/T rows
	CROSS_ROW_SUMMON = 1u << 3,    // summon into a free rear row slot
	MODIFY_MONSTER_STATS = 1u << 4,
	CUSTOM_ANIME_EFFECT = 1u << 5
};
struct Policy {
	uint32_t features{NONE};
	uint8_t rear_slot_limit{5};
	int32_t attack_bonus{0};
	constexpr bool has(uint32_t feature) const {
		return (features & feature) == feature;
	}
	constexpr bool valid() const {
		return rear_slot_limit <= 5 && attack_bonus >= -999999
			&& attack_bonus <= 999999;
	}
};
constexpr Row physical_row(uint8_t loc) {
	return loc == 0x04 ? Row::FRONT_MONSTER
		: loc == 0x08 ? Row::REAR_SPELL_TRAP : Row::OUTSIDE_FIELD;
}
constexpr bool counts_as_monster(Row row, bool is_real_monster,
		bool carries_rear_monster_effect, bool rear_is_enabled) {
	return is_real_monster &&
		(row == Row::FRONT_MONSTER
			|| (row == Row::REAR_SPELL_TRAP
				&& carries_rear_monster_effect && rear_is_enabled));
}
constexpr bool rear_is_attackable(bool attacker_is_enemy,
		uint32_t front_monster_count, bool rear_guard_active) {
	return attacker_is_enemy && (!rear_guard_active || front_monster_count == 0);
}
constexpr bool can_declare_attack(Row row, bool marked_rear_monster,
		bool rear_enabled) {
	return row == Row::FRONT_MONSTER
		|| (row == Row::REAR_SPELL_TRAP && marked_rear_monster && rear_enabled);
}
constexpr bool can_place_in_rear(uint8_t physical_slot, bool slot_empty,
		bool marker_allowed, const Policy& policy) {
	return policy.valid() && policy.has(REAR_MONSTER)
		&& physical_slot < policy.rear_slot_limit
		&& slot_empty && marker_allowed;
}
constexpr uint8_t physical_location_from_virtual(uint16_t location) {
	return location == 0x2000 ? 0x04
		: location == 0x4000 ? 0x08
		: location == 0x04 || location == 0x08
			? static_cast<uint8_t>(location) : 0;
}
} // namespace anime_zone_rules
#endif // ANIME_ZONE_RULES_H
