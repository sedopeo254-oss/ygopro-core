/*
 * Inherited Duel Mode - immutable transfer preflight.
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * This is a dry-run planner, not a card mover. Card instance identity is
 * authoritative; repeated CARD CODES are legal, repeated INSTANCES are not.
 * Never mutate the original duel when preflight fails.
 */
#ifndef DUEL_SUCCESSION_TRANSFER_H
#define DUEL_SUCCESSION_TRANSFER_H

#include <cstdint>
#include <unordered_set>
#include <vector>

enum class SuccessionCardZone : uint8_t {
	DECK = 1, EXTRA = 2, HAND = 3, GRAVE = 4, BANISHED = 5,
	MONSTER = 6, SPELL_TRAP = 7, DECK_MASTER = 8
};

struct SuccessionCardIdentity {
	uint64_t instance_id{};
	uint32_t card_code{};
	uint8_t owner_side{};
	SuccessionCardZone zone{ SuccessionCardZone::DECK };
};

enum class SuccessionPreflightError : uint8_t {
	NONE = 0,
	INVALID_SIDE,
	INVALID_IDENTITY,
	DUPLICATE_INSTANCE,
	FOREIGN_SIDE,
	INVALID_SUCCESSOR_ZONE,
	CAPACITY_EXCEEDED
};

struct SuccessionTransferPlan {
	SuccessionPreflightError error{ SuccessionPreflightError::NONE };
	std::vector<SuccessionCardIdentity> imported_deck;
	std::vector<SuccessionCardIdentity> imported_extra;
	uint32_t inherited_cards_retained{};
	uint32_t opponent_cards_retained{};

	bool valid() const { return error == SuccessionPreflightError::NONE; }
	uint32_t imported_count() const {
		return static_cast<uint32_t>(imported_deck.size() + imported_extra.size());
	}
};

class DuelSuccessionTransfer {
public:
	static constexpr uint32_t MAX_IMPORTED_CARDS = 512;

	static SuccessionTransferPlan preflight(
			uint8_t inherited_side,
			const std::vector<SuccessionCardIdentity>& existing_duel,
			const std::vector<SuccessionCardIdentity>& successor_undeployed) {
		SuccessionTransferPlan plan;
		if(inherited_side > 1)
			return reject(SuccessionPreflightError::INVALID_SIDE);
		if(successor_undeployed.size() > MAX_IMPORTED_CARDS)
			return reject(SuccessionPreflightError::CAPACITY_EXCEEDED);
		std::unordered_set<uint64_t> seen;
		for(const auto& card : existing_duel) {
			if(!card.instance_id || !card.card_code)
				return reject(SuccessionPreflightError::INVALID_IDENTITY);
			if(card.owner_side > 1)
				return reject(SuccessionPreflightError::FOREIGN_SIDE);
			if(!seen.insert(card.instance_id).second)
				return reject(SuccessionPreflightError::DUPLICATE_INSTANCE);
			if(card.owner_side == inherited_side)
				++plan.inherited_cards_retained;
			else
				++plan.opponent_cards_retained;
		}
		for(const auto& card : successor_undeployed) {
			if(!card.instance_id || !card.card_code)
				return reject(SuccessionPreflightError::INVALID_IDENTITY);
			if(card.owner_side != inherited_side)
				return reject(SuccessionPreflightError::FOREIGN_SIDE);
			if(card.zone != SuccessionCardZone::DECK
					&& card.zone != SuccessionCardZone::EXTRA)
				return reject(SuccessionPreflightError::INVALID_SUCCESSOR_ZONE);
			if(!seen.insert(card.instance_id).second)
				return reject(SuccessionPreflightError::DUPLICATE_INSTANCE);
			if(card.zone == SuccessionCardZone::DECK)
				plan.imported_deck.push_back(card);
			else
				plan.imported_extra.push_back(card);
		}
		return plan;
	}

private:
	static SuccessionTransferPlan reject(SuccessionPreflightError error) {
		SuccessionTransferPlan plan;
		plan.error = error;
		return plan; // No partial import may escape from a failed plan.
	}
};

#endif // DUEL_SUCCESSION_TRANSFER_H
