#include "duel_succession_transfer.h"
#include "duel_succession.h"
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
void check(bool value, const char* message) {
	if(value)
		return;
	std::cerr << "FAIL: " << message << '\n';
	std::exit(1);
}
using Zone = SuccessionCardZone;
using Card = SuccessionCardIdentity;

void test_kaiba_yugi_noah_continuity() {
	// Persistent Kaiba/Noah cards are a read-only inventory for preflight.
	const std::vector<Card> existing{
		{100, 89631139, 0, Zone::GRAVE},  // Kaiba Blue-Eyes in GY
		{101, 47986555, 0, Zone::DECK_MASTER}, // Kaiser Sea Horse
		{102, 333, 0, Zone::HAND},
		{200, 111, 1, Zone::DECK_MASTER},  // Noah Shinato
		{201, 222, 1, Zone::BANISHED},
		{202, 333, 1, Zone::DECK}
	};
	const std::vector<Card> successor{
		{300, 46986414, 0, Zone::DECK}, // Yugi Dark Magician
		{301, 89631139, 0, Zone::DECK}, // identical code, DIFFERENT card
		{302, 23995346, 0, Zone::EXTRA}
	};
	auto plan = DuelSuccessionTransfer::preflight(0, existing, successor);
	check(plan.valid(), "valid merge must pass preflight");
	check(plan.imported_count() == 3, "only three undeployed successor cards imported");
	check(plan.imported_deck.size() == 2 && plan.imported_extra.size() == 1,
		"deck and extra must not mix");
	check(plan.inherited_cards_retained == 3 && plan.opponent_cards_retained == 3,
		"all original cards survive unmodified");
	check(existing[0].zone == Zone::GRAVE
		&& existing[1].zone == Zone::DECK_MASTER
		&& existing[3].owner_side == 1,
		"Kaiba GY/Deck Master and Noah ownership are untouched");
	check(successor[1].instance_id != existing[0].instance_id,
		"same Blue-Eyes code may appear as distinct genuine card instances");
}

void test_reject_duplicate_instances_and_illegal_zones() {
	const std::vector<Card> existing{{9, 1, 0, Zone::GRAVE}};
	auto duplicate = DuelSuccessionTransfer::preflight(0, existing,
		{{9, 999, 0, Zone::DECK}});
	check(duplicate.error == SuccessionPreflightError::DUPLICATE_INSTANCE
		&& duplicate.imported_count() == 0, "never duplicate actual card instances");
	auto grave_import = DuelSuccessionTransfer::preflight(0, existing,
		{{10, 999, 0, Zone::GRAVE}});
	check(grave_import.error == SuccessionPreflightError::INVALID_SUCCESSOR_ZONE,
		"cannot clone successor grave cards into inherited deck");
	auto opponent_import = DuelSuccessionTransfer::preflight(0, existing,
		{{11, 999, 1, Zone::DECK}});
	check(opponent_import.error == SuccessionPreflightError::FOREIGN_SIDE,
		"cannot absorb Noah's cards");
	auto missing = DuelSuccessionTransfer::preflight(0, existing,
		{{0, 999, 0, Zone::DECK}});
	check(missing.error == SuccessionPreflightError::INVALID_IDENTITY,
		"every transferred card must have stable instance identity");
}

void test_atomic_validation_and_bounded_import() {
	const std::vector<Card> existing{};
	std::vector<Card> many;
	for(uint64_t i = 1; i <= DuelSuccessionTransfer::MAX_IMPORTED_CARDS + 1; ++i)
		many.push_back({i, 1, 0, Zone::DECK});
	auto too_large = DuelSuccessionTransfer::preflight(0, existing, many);
	check(too_large.error == SuccessionPreflightError::CAPACITY_EXCEEDED
		&& too_large.imported_count() == 0, "oversize plan must fail atomically");

	auto mixed = DuelSuccessionTransfer::preflight(0, existing,
		{{1, 1, 0, Zone::DECK}, {2, 1, 1, Zone::DECK}});
	check(!mixed.valid() && mixed.imported_count() == 0,
		"partial valid imports may not escape an invalid plan");
	check(!DuelSuccessionTransfer::preflight(7, existing, {}).valid(),
		"cannot inherit a non-existent side");

	DuelSuccessionState transition;
	transition.configure(true, 0);
	check(transition.request(0, 1, 18), "preliminary loss must be authorized");
	check(!transition.commit(19, true, mixed.valid()), "invalid deck plan blocks commit");
	check(transition.phase() == DuelSuccessionPhase::AWAITING_SUCCESSOR,
		"failed merge cannot mark succession complete");
}
} // namespace

int main() {
	test_kaiba_yugi_noah_continuity();
	test_reject_duplicate_instances_and_illegal_zones();
	test_atomic_validation_and_bounded_import();
	std::cout << "Duel Succession transfer preflight tests: PASS\n";
}
