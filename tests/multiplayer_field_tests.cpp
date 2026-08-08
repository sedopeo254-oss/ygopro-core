#include "card.h"
#include "duel.h"
#include "effect.h"
#include "field.h"
#include "ocgapi.h"

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
void expect(bool condition, const char* message) {
	if(condition)
		return;
	std::cerr << "FAILED: " << message << '\n';
	std::exit(1);
}

void read_card(void*, uint32_t code, OCG_CardData* data) {
	*data = {};
	data->code = code;
	data->type = TYPE_MONSTER;
}

void read_card_done(void*, OCG_CardData*) {
}

int read_script(void*, OCG_Duel, const char*) {
	return 0;
}

void log_message(void*, const char*, int) {
}

void initialize_extra_duelist(duel& game, uint8_t duelist_index, uint32_t code) {
	const OCG_NewCardInfo info{
		0,
		duelist_index,
		code,
		0,
		LOCATION_DECK,
		0,
		POS_FACEDOWN_DEFENSE
	};
	OCG_DuelNewCard(&game, &info);
}

std::vector<std::vector<uint8_t>> take_messages(duel& game) {
	game.clear_buffer();
	game.generate_buffer();
	std::vector<std::vector<uint8_t>> messages;
	size_t offset = 0;
	while(offset < game.buff.size()) {
		expect(offset + sizeof(uint32_t) <= game.buff.size(),
			"the generated message stream must contain a complete size prefix");
		uint32_t size = 0;
		std::memcpy(&size, game.buff.data() + offset, sizeof(size));
		offset += sizeof(size);
		expect(size > 0 && offset + size <= game.buff.size(),
			"the generated message stream must contain a complete payload");
		messages.emplace_back(game.buff.begin() + offset,
			game.buff.begin() + offset + size);
		offset += size;
	}
	game.clear_buffer();
	return messages;
}

uint32_t read_u32(const std::vector<uint8_t>& message, size_t offset) {
	expect(offset + sizeof(uint32_t) <= message.size(),
		"the message must contain the requested uint32 value");
	uint32_t value = 0;
	std::memcpy(&value, message.data() + offset, sizeof(value));
	return value;
}
}

int main() {
	OCG_DuelOptions options{};
	options.seed[0] = 1;
	options.flags = DUEL_3_V_1;
	options.team1 = { 4000, 5, 1 };
	options.team2 = { 4000, 5, 1 };
	options.cardReader = read_card;
	options.cardReaderDone = read_card_done;
	options.scriptReader = read_script;
	options.logHandler = log_message;

	bool valid_lua = true;
	duel game(options, valid_lua);
	expect(valid_lua, "the embedded Lua runtime must initialize");
	auto& field = *game.game_field;
	expect(field.player[0].list_mzone.size() == 21, "the allied monster fields must expose 21 internal slots");
	expect(field.player[0].list_szone.size() == 24, "the allied spell/trap fields must expose 24 internal slots");

	initialize_extra_duelist(game, 1, 1001);
	initialize_extra_duelist(game, 2, 1002);

	auto* serenity = game.new_card(2000);
	serenity->owner = 0;
	serenity->owner_duelist = 0;
	field.add_card(0, serenity, LOCATION_MZONE, 0);
	expect(serenity->current.sequence == 0, "Serenity's first monster zone must map to slot 0");

	expect(field.tag_swap_to(0, 1), "the active allied resources must switch to Tristan");
	auto* tristan = game.new_card(2001);
	tristan->owner = 0;
	tristan->owner_duelist = 1;
	field.add_card(0, tristan, LOCATION_MZONE, 0);
	expect(tristan->current.sequence == 7, "Tristan's first monster zone must map to slot 7");

	expect(field.tag_swap_to(0, 2), "the active allied resources must switch to Duke");
	auto* duke = game.new_card(2002);
	duke->owner = 0;
	duke->owner_duelist = 2;
	field.add_card(0, duke, LOCATION_MZONE, 0);
	expect(duke->current.sequence == 14, "Duke's first monster zone must map to slot 14");
	expect(field.player[0].list_mzone[0] == serenity
		&& field.player[0].list_mzone[7] == tristan
		&& field.player[0].list_mzone[14] == duke,
		"all three allied fields must remain simultaneously present");
	auto* duke_effect = game.new_effect();
	duke_effect->owner = duke;
	duke_effect->handler = duke;
	field.core.reason_effect = duke_effect;
	expect(field.get_effect_duelist(0) == 2 && field.get_response_player(0) == 4,
		"a Duke Deck Master ability must route its prompts to Duke even outside his turn");
	field.core.reason_effect = nullptr;
	expect(field.get_response_player(0) == 4,
		"the current allied field must remain the response fallback without an effect handler");

	// Virtual World selects a Deck Master from a code-only list. These cards
	// have no field location, so their synthetic location must still identify
	// the exact logical duelist. Otherwise the server's private-card filter
	// exposes player 1's choices but replaces players 2 and 3 with card backs.
	const std::array<uint8_t, 4> deck_master_selectors{ 2, 3, 4, 1 };
	const std::array<uint8_t, 4> deck_master_sides{ 0, 0, 0, 1 };
	const std::array<uint8_t, 4> deck_master_duelists{ 0, 1, 2, 0 };
	take_messages(game);
	for(uint8_t logical = 0; logical < 4; ++logical) {
		field.core.select_cards_codes = { { 9000u + logical, 1u } };
		Processors::SelectCardCodes deck_master_select(
			0, deck_master_selectors[logical], false, 1, 1,
			deck_master_sides[logical], deck_master_duelists[logical]);
		expect(!field.process(deck_master_select),
			"a Deck Master code selection must wait for its logical player");
		const auto deck_master_messages = take_messages(game);
		expect(deck_master_messages.size() == 1,
			"a Deck Master code selection must emit exactly one prompt");
		const auto& prompt = deck_master_messages.front();
		expect(prompt.size() == 29 && prompt[0] == MSG_SELECT_CARD
				&& prompt[1] == deck_master_selectors[logical]
				&& read_u32(prompt, 15) == 9000u + logical
				&& prompt[19] == deck_master_sides[logical]
				&& (read_u32(prompt, 25) >> 24) == deck_master_duelists[logical],
			"every 3v1 player must receive face-up Deck Master choices tagged with their own logical seat");
	}

	// 3-vs-1 replays use the same authoritative two-player camera packet as
	// Battle Royale. Live Duels may ignore it, but recording it prevents replay
	// seeks from inheriting the previous ally's field or private piles.
	take_messages(game);
	field.publish_multiplayer_replay_view(0, 3);
	const auto three_v_one_replay_messages = take_messages(game);
	expect(three_v_one_replay_messages.size() == 3
			&& three_v_one_replay_messages[0].size() == 3
			&& three_v_one_replay_messages[0][0] == MSG_MULTIPLAYER_REPLAY_VIEW
			&& three_v_one_replay_messages[0][1] == 0
			&& three_v_one_replay_messages[0][2] == 3
			&& three_v_one_replay_messages[1][0] == MSG_MULTIPLAYER_PRIVATE_PILES
			&& three_v_one_replay_messages[1][1] == 0
			&& three_v_one_replay_messages[2][0] == MSG_MULTIPLAYER_PRIVATE_PILES
			&& three_v_one_replay_messages[2][1] == 3,
		"3v1 replay camera changes must carry exact private-pile snapshots for both displayed players");

	auto* tristan_hand = game.new_card(2003);
	tristan_hand->owner = 0;
	tristan_hand->owner_duelist = 1;
	field.add_card(0, tristan_hand, LOCATION_HAND, 0);
	auto* tristan_extra = game.new_card(2004);
	tristan_extra->data.type = TYPE_MONSTER | TYPE_FUSION;
	tristan_extra->owner = 0;
	tristan_extra->owner_duelist = 1;
	field.add_card(0, tristan_extra, LOCATION_EXTRA, 0);
	auto* tristan_banished = game.new_card(2005);
	tristan_banished->owner = 0;
	tristan_banished->owner_duelist = 1;
	field.add_card(0, tristan_banished, LOCATION_REMOVED, 0);
	expect(field.get_logical_list(0, LOCATION_DECK, 1).size() == 1
		&& field.get_logical_list(0, LOCATION_HAND, 1).size() == 1
		&& field.get_logical_list(0, LOCATION_EXTRA, 1).size() == 1
		&& field.get_logical_list(0, LOCATION_REMOVED, 1).size() == 1,
		"Tristan's deck, hand, extra deck and banished pile must stay independent while Duke is active");

	expect(field.move_card(0, tristan, LOCATION_GRAVE, 0), "Tristan's card must move to its owner's graveyard");
	expect(field.player[0].list_grave.empty(), "Duke's active graveyard must stay separate");
	expect(field.tag_swap_to(0, 1), "the active resources must switch back to Tristan");
	expect(field.player[0].list_grave.size() == 1 && field.player[0].list_grave.front() == tristan,
		"Tristan's graveyard must be restored with his card");

	// The TAG_SWAP snapshot itself must name the exact logical owner. This is
	// consumed by the server, client and replay instead of guessing from the
	// mutable current-turn seat.
	take_messages(game);
	expect(field.tag_swap_to(0, 2), "the private resources must switch to Duke for an owner-tag test");
	const auto duke_swap_messages = take_messages(game);
	auto duke_swap = std::find_if(duke_swap_messages.begin(), duke_swap_messages.end(),
		[](const auto& message) { return !message.empty() && message[0] == MSG_TAG_SWAP; });
	expect(duke_swap != duke_swap_messages.end() && duke_swap->back() == 2,
		"Duke's TAG_SWAP snapshot must end with logical player 2");
	expect(field.tag_swap_to(0, 1), "the private resources must switch back to Tristan");
	const auto tristan_swap_messages = take_messages(game);
	auto tristan_swap = std::find_if(tristan_swap_messages.begin(), tristan_swap_messages.end(),
		[](const auto& message) { return !message.empty() && message[0] == MSG_TAG_SWAP; });
	expect(tristan_swap != tristan_swap_messages.end() && tristan_swap->back() == 1,
		"Tristan's TAG_SWAP snapshot must end with logical player 1");
	if(tristan_swap != tristan_swap_messages.end()) {
		const auto ecount = read_u32(*tristan_swap, 6);
		const auto hcount = read_u32(*tristan_swap, 14);
		const auto grave_offset = 22u + static_cast<size_t>(ecount + hcount) * 8u;
		expect(read_u32(*tristan_swap, grave_offset) == 1
				&& read_u32(*tristan_swap, grave_offset + 8) == 2001,
			"Tristan's exact snapshot must contain his own Graveyard card");
	}

	// A card temporarily controlled by Nezbitt still belongs in its exact
	// owner's logical Graveyard, not in Nezbitt's or the currently displayed
	// ally's pile.
	auto* captured_tristan_card = game.new_card(2006);
	captured_tristan_card->owner = 0;
	captured_tristan_card->owner_duelist = 1;
	field.add_card(1, captured_tristan_card, LOCATION_MZONE, 1, false, 0);
	expect(field.move_card(1, captured_tristan_card, LOCATION_GRAVE, 0),
		"an opponent-controlled allied card must be sent to its owner's Graveyard");
	const auto& tristan_grave = field.get_logical_list(0, LOCATION_GRAVE, 1);
	expect(std::find(tristan_grave.begin(), tristan_grave.end(), captured_tristan_card)
			!= tristan_grave.end()
			&& field.get_logical_list(1, LOCATION_GRAVE, 0).empty(),
		"the captured card must be visible only in Tristan's Graveyard");

	const auto tristan_hand_count = field.get_logical_list(0, LOCATION_HAND, 1).size();
	const auto duke_hand_count = field.get_logical_list(0, LOCATION_HAND, 2).size();
	Processors::Draw duke_draw(0, nullptr, REASON_EFFECT, 0, 0, 1, 2);
	expect(!field.process(duke_draw), "drawing for an inactive allied duelist must complete its first processing step");
	duke_draw.step = 1;
	expect(field.process(duke_draw), "drawing for an inactive allied duelist must complete");
	expect(field.get_logical_list(0, LOCATION_DECK, 2).empty()
		&& field.get_logical_list(0, LOCATION_HAND, 2).size() == duke_hand_count + 1,
		"the drawn card must move from Duke's deck to Duke's hand");
	expect(field.get_logical_list(0, LOCATION_HAND, 1).size() == tristan_hand_count,
		"drawing for Duke must not alter Tristan's currently visible hand");
	Processors::Draw serenity_overdraw(0, nullptr, REASON_EFFECT, 0, 0, 1, 0);
	expect(!field.process(serenity_overdraw), "Serenity's failed draw must be processed");
	Processors::Draw duke_overdraw(0, nullptr, REASON_EFFECT, 0, 0, 1, 2);
	expect(!field.process(duke_overdraw), "Duke's failed draw must be processed");
	expect((field.core.multiplayer_overdraw_mask & 0x05) == 0x05,
		"failed draws for multiple allied duelists must be tracked independently");
	field.core.multiplayer_overdraw_mask = 0;
	field.core.overdraw[0] = false;

	field.get_logical_lp(0, 0) = 3100;
	field.get_logical_lp(0, 1) = 2200;
	field.get_logical_lp(0, 2) = 1300;
	expect(field.get_logical_lp(0, 0) == 3100
		&& field.get_logical_lp(0, 1) == 2200
		&& field.get_logical_lp(0, 2) == 1300,
		"each allied duelist must retain independent life points");
	Processors::Recover duke_recover(0, nullptr, REASON_EFFECT, 0, 0, 200, false, 2);
	expect(!field.process(duke_recover), "recovering an inactive allied duelist must complete its first step");
	duke_recover.step = 1;
	expect(!field.process(duke_recover), "recovering an inactive allied duelist must apply to life points");
	expect(field.get_logical_lp(0, 2) == 1500
		&& field.get_logical_lp(0, 0) == 3100
		&& field.get_logical_lp(0, 1) == 2200,
		"LP recovery must affect only the selected logical player");

	auto* nezbitt = game.new_card(3000);
	nezbitt->owner = 1;
	field.add_card(1, nezbitt, LOCATION_MZONE, 0);
	serenity->current.position = POS_FACEUP_ATTACK;
	duke->current.position = POS_FACEUP_ATTACK;
	nezbitt->current.position = POS_FACEUP_ATTACK;
	auto* tristan_board = game.new_card(3001);
	tristan_board->owner = 0;
	tristan_board->owner_duelist = 1;
	tristan_board->current.position = POS_FACEUP_ATTACK;
	field.add_card(0, tristan_board, LOCATION_MZONE, 1, false, 1);

	// In anime 3-vs-1, normal `tp` still means the exact effect owner, while
	// `1-tp` means every other logical duelist. This lets stock cards such as
	// Block Attack affect a teammate without changing team/win relationships.
	field.core.reason_effect = duke_effect;
	expect(field.filter_field_card(0, LOCATION_MZONE, 0, nullptr) == 1,
		"a 3v1 tp field query must contain only Duke's exact logical field");
	const auto other_field_count = field.filter_field_card(
		0, 0, LOCATION_MZONE, nullptr);
	expect(other_field_count == 3,
		"a 3v1 1-tp query must contain both teammates and Nezbitt");
	expect(field.matches_script_controller(duke, 0)
			&& !field.matches_script_controller(serenity, 0)
			&& field.matches_script_controller(serenity, 1)
			&& field.matches_script_controller(tristan_board, 1)
			&& field.matches_script_controller(nezbitt, 1),
		"Card.IsControler(tp/1-tp) must resolve exact-self versus every other 3v1 field");
	take_messages(game);
	field.publish_multiplayer_effect_view(duke_effect, serenity);
	const auto teammate_target_view = take_messages(game);
	expect(teammate_target_view.size() == 3
			&& teammate_target_view[0].size() == 3
			&& teammate_target_view[0][0] == MSG_MULTIPLAYER_REPLAY_VIEW
			&& teammate_target_view[0][1] == 2
			&& teammate_target_view[0][2] == 0,
		"a Duke effect targeting Serenity must serialize the exact 2 -> 0 replay view");
	const uint32_t block_attack_mask = (1u << 0) | (1u << 1) | (1u << 3);
	auto block_attack_targets = game.new_group();
	field.filter_matching_card(0, 0, 0, 0, block_attack_targets, nullptr,
		nullptr, 0, nullptr, 0, true, block_attack_mask, LOCATION_MZONE);
	expect(block_attack_targets->container.size() == 3
			&& block_attack_targets->container.count(serenity) == 1
			&& block_attack_targets->container.count(tristan_board) == 1
			&& block_attack_targets->container.count(nezbitt) == 1
			&& block_attack_targets->container.count(duke) == 0,
		"the anime Block Attack mask must reach both allies and Nezbitt while excluding Duke");
	const uint32_t teammate_grave_mask = (1u << 0) | (1u << 1);
	auto teammate_grave_targets = game.new_group();
	field.filter_matching_card(0, 0, 0, 0, teammate_grave_targets, nullptr,
		nullptr, 0, nullptr, 0, false, teammate_grave_mask, LOCATION_GRAVE);
	expect(teammate_grave_targets->container.count(tristan) == 1
			&& teammate_grave_targets->container.count(captured_tristan_card) == 1,
		"explicit ally Graveyard scope must expose Tristan's exact owner pile");
	auto* duke_self_aura = game.new_effect();
	duke_self_aura->owner = duke;
	duke_self_aura->handler = duke;
	duke_self_aura->type = EFFECT_TYPE_FIELD;
	duke_self_aura->s_range = LOCATION_MZONE;
	card_set duke_self_targets;
	field.filter_affected_cards(duke_self_aura, &duke_self_targets);
	expect(duke_self_targets.size() == 1 && duke_self_targets.count(duke) == 1,
		"self-range card effects must remain isolated to their exact 3v1 owner");
	auto* duke_other_aura = game.new_effect();
	duke_other_aura->owner = duke;
	duke_other_aura->handler = duke;
	duke_other_aura->type = EFFECT_TYPE_FIELD;
	duke_other_aura->o_range = LOCATION_MZONE;
	card_set duke_other_targets;
	field.filter_affected_cards(duke_other_aura, &duke_other_targets);
	expect(duke_other_targets.size() == 3
			&& duke_other_targets.count(serenity) == 1
			&& duke_other_targets.count(tristan_board) == 1
			&& duke_other_targets.count(nezbitt) == 1,
		"3v1 other-player auras must reach teammates and Nezbitt without reaching Duke");
	auto configure_three_v_one_hopt = [](effect* peffect, card* handler) {
		peffect->owner = handler;
		peffect->handler = handler;
		peffect->flag[0] = EFFECT_FLAG_COUNT_LIMIT;
		peffect->count_limit = 1;
		peffect->count_limit_max = 1;
		peffect->count_code = 25880422;
	};
	auto* duke_hopt = game.new_effect();
	auto* serenity_hopt = game.new_effect();
	configure_three_v_one_hopt(duke_hopt, duke);
	configure_three_v_one_hopt(serenity_hopt, serenity);
	duke_hopt->dec_count(0);
	expect(!duke_hopt->check_count_limit(0)
			&& serenity_hopt->check_count_limit(0),
		"once-per-player usage must never be shared between 3v1 teammates");
	field.core.reason_effect = nullptr;
	field.core.attacker = nezbitt;
	field.core.attack_target_duelist = 0;
	card_vector attack_targets;
	field.get_attack_target(nezbitt, &attack_targets);
	expect(attack_targets.size() == 1 && attack_targets.front() == serenity,
		"Nezbitt must only see Serenity's monsters after selecting her as the attack target");
	field.core.attack_target_duelist = 2;
	attack_targets.clear();
	field.get_attack_target(nezbitt, &attack_targets);
	expect(attack_targets.size() == 1 && attack_targets.front() == duke,
		"changing the selected attack target must project Duke's monster field");
	field.core.subunits.clear();
	field.core.attack_target = nullptr;
	field.damage(nullptr, REASON_BATTLE, 1, nezbitt, 0, 500);
	auto* direct_damage = Processors::get_opt_variant<Processors::Damage>(field.core.subunits.back());
	expect(direct_damage && direct_damage->duelist == 2,
		"a direct attack must queue damage for the selected allied duelist only");
	field.core.subunits.clear();
	field.infos.turn_player = 1;
	field.core.attacker = nezbitt;
	field.core.attack_target = nullptr;
	field.core.attack_target_logical = 0;
	field.core.attack_target_duelist = 0;
	Processors::BattleCommand team_attack(4);
	expect(!field.process(team_attack),
		"Nezbitt's attack must pause for the allied Let me take it prompt");
	auto* team_attack_prompt =
		Processors::get_opt_variant<Processors::SelectYesNo>(field.core.subunits.back());
	expect(team_attack_prompt && team_attack_prompt->playerid == 3,
		"Nezbitt attacking Serenity must route Let me take it to Tristan's network seat");
	field.returns.set<int32_t>(0, 1);
	team_attack.step = 44;
	expect(!field.process(team_attack)
			&& field.core.attack_target_logical == 1
			&& field.core.attack_target_duelist == 1,
		"accepting the attack must redirect it to Tristan's exact logical field");
	take_messages(game);
	team_attack.step = 4;
	expect(!field.process(team_attack),
		"the redirected 3v1 attack must resume against Tristan");
	const auto team_attack_view = take_messages(game);
	expect(team_attack_view.size() >= 3
			&& team_attack_view[0][0] == MSG_MULTIPLAYER_REPLAY_VIEW
			&& team_attack_view[0][1] == 3
			&& team_attack_view[0][2] == 1,
		"the redirected attack replay must immediately switch from Nezbitt to Tristan");

	// A replay attack is self-contained: the authoritative Nezbitt -> Duke
	// camera is serialized immediately before MSG_ATTACK, and the packet ends
	// with canonical logical seats. This prevents a stale Swap-the-Team view
	// from reversing the arrow or renaming P1 as P4.
	field.core.subunits.clear();
	field.core.attacker = nezbitt;
	field.core.attack_target = duke;
	field.core.attack_target_logical = 2;
	field.core.attack_target_duelist = 2;
	take_messages(game);
	Processors::BattleCommand deterministic_team_attack(8);
	expect(!field.process(deterministic_team_attack),
		"a 3v1 attack must emit its final replay camera and attack messages");
	const auto deterministic_team_messages = take_messages(game);
	auto deterministic_team_attack_message = std::find_if(
		deterministic_team_messages.begin(), deterministic_team_messages.end(),
		[](const auto& message) {
			return !message.empty() && message[0] == MSG_ATTACK;
		});
	expect(deterministic_team_attack_message != deterministic_team_messages.end()
			&& deterministic_team_attack_message->size() >= 23
			&& (*deterministic_team_attack_message)
				[deterministic_team_attack_message->size() - 2] == 3
			&& deterministic_team_attack_message->back() == 2,
		"Nezbitt attacking Duke must append attacker 3 and target 2 in canonical order");
	if(deterministic_team_attack_message != deterministic_team_messages.end()) {
		const auto attack_index = static_cast<size_t>(std::distance(
			deterministic_team_messages.begin(), deterministic_team_attack_message));
		expect(attack_index >= 3
				&& deterministic_team_messages[attack_index - 3].size() == 3
				&& deterministic_team_messages[attack_index - 3][0]
					== MSG_MULTIPLAYER_REPLAY_VIEW
				&& deterministic_team_messages[attack_index - 3][1] == 3
				&& deterministic_team_messages[attack_index - 3][2] == 2,
			"Nezbitt attacking Duke must serialize the 3 -> 2 camera immediately before MSG_ATTACK");
	}

	field.core.subunits.clear();
	Processors::Damage effect_damage(0, nullptr, REASON_EFFECT, 1, nezbitt, 0, 700, false, 0, true);
	expect(!field.process(effect_damage), "effect damage must pause for teammate interception");
	auto* intercept_prompt = Processors::get_opt_variant<Processors::SelectYesNo>(field.core.subunits.back());
	expect(intercept_prompt && intercept_prompt->playerid == 3,
		"the first Let me take it prompt must be routed to Tristan's logical seat");
	field.returns.set<int32_t>(0, 1);
	effect_damage.step = 20;
	expect(!field.process(effect_damage) && effect_damage.duelist == 1,
		"accepting Let me take it must redirect effect damage to Tristan's life points");
	field.core.subunits.clear();
	const auto serenity_lp = field.get_logical_lp(0, 0);
	Processors::Damage batch_damage(0, nullptr, REASON_EFFECT, 1, nezbitt, 0, 100, false, 0, false);
	expect(!field.process(batch_damage), "non-interceptable batch damage must resolve its first processing step");
	expect(field.core.subunits.empty(),
		"non-interceptable batch damage must not enqueue a Let me take it prompt");
	batch_damage.step = 1;
	expect(!field.process(batch_damage), "non-interceptable batch damage must apply to life points");
	expect(field.get_logical_lp(0, 0) == serenity_lp - 100,
		"non-interceptable batch damage must affect the selected logical player");
	field.get_logical_lp(0, 0) = 50;
	Processors::Damage lethal_damage(0, nullptr, REASON_EFFECT, 1, nezbitt,
		0, 1000, false, 0, false);
	expect(!field.process(lethal_damage),
		"lethal non-interceptable damage must complete its preparation step");
	lethal_damage.step = 1;
	expect(!field.process(lethal_damage) && field.get_logical_lp(0, 0) == 0,
		"multiplayer LP must saturate at zero instead of serializing a negative value");
	field.core.reason_effect = duke_effect;
	expect(field.multiplayer.eliminate(0, PlayerEliminationReason::LP),
		"Serenity must be eliminable for the persistent anime-field regression test");
	auto after_elimination_fields = game.new_group();
	field.filter_matching_card(0, 0, 0, 0, after_elimination_fields, nullptr,
		nullptr, 0, nullptr, 0, false, 1u << 0, LOCATION_MZONE);
	expect(after_elimination_fields->container.count(serenity) == 1,
		"Serenity's remaining field card must stay targetable after her elimination");
	take_messages(game);
	field.publish_multiplayer_effect_view(duke_effect, serenity);
	const auto eliminated_target_view = take_messages(game);
	expect(eliminated_target_view.size() == 3
			&& eliminated_target_view[0][0] == MSG_MULTIPLAYER_REPLAY_VIEW
			&& eliminated_target_view[0][1] == 2
			&& eliminated_target_view[0][2] == 0,
		"an eliminated teammate's persistent card must remain focusable in replay");
	field.core.reason_effect = nullptr;

	OCG_DuelOptions royale_options = options;
	royale_options.flags = DUEL_BATTLE_ROYALE;
	bool royale_valid_lua = true;
	duel royale(royale_options, royale_valid_lua);
	expect(royale_valid_lua, "the Battle Royale Lua runtime must initialize");
	auto& royale_field = *royale.game_field;
	expect(royale_field.player[0].list_mzone.size() == 14
			&& royale_field.player[1].list_mzone.size() == 14,
		"Battle Royale must allocate two monster fields on both core sides");
	expect(royale_field.player[0].list_szone.size() == 16
			&& royale_field.player[1].list_szone.size() == 16,
		"Battle Royale must allocate two spell/trap fields on both core sides");
	initialize_extra_duelist(royale, 1, 4001);
	const OCG_NewCardInfo side1_extra{
		1,
		1,
		4002,
		0,
		LOCATION_DECK,
		0,
		POS_FACEDOWN_DEFENSE
	};
	OCG_DuelNewCard(&royale, &side1_extra);

	auto* kaiba = royale.new_card(4100);
	kaiba->owner = 0;
	kaiba->owner_duelist = 0;
	royale_field.add_card(0, kaiba, LOCATION_MZONE, 0, false, 0);
	auto* yugi = royale.new_card(4101);
	yugi->owner = 0;
	yugi->owner_duelist = 1;
	expect(royale_field.tag_swap_to(0, 1), "side 0 must switch to Yugi before he uses his field");
	royale_field.add_card(0, yugi, LOCATION_MZONE, 0);
	auto* marik = royale.new_card(4102);
	marik->owner = 1;
	marik->owner_duelist = 0;
	royale_field.add_card(1, marik, LOCATION_MZONE, 0, false, 0);
	auto* joey = royale.new_card(4103);
	joey->owner = 1;
	joey->owner_duelist = 1;
	expect(royale_field.tag_swap_to(1, 1), "side 1 must switch to Joey before he uses his field");
	royale_field.add_card(1, joey, LOCATION_MZONE, 0);
	expect(kaiba->current.sequence == 0 && yugi->current.sequence == 7
			&& marik->current.sequence == 0 && joey->current.sequence == 7,
		"all four Battle Royale players must keep zone 0 on their own saved field");
	expect(royale_field.player[0].list_mzone[0] == kaiba
			&& royale_field.player[0].list_mzone[7] == yugi
			&& royale_field.player[1].list_mzone[0] == marik
			&& royale_field.player[1].list_mzone[7] == joey,
		"switching turns must not merge a Battle Royale player's field with another player");
	take_messages(royale);
	royale_field.publish_multiplayer_replay_view(0, 2);
	const auto replay_view_messages = take_messages(royale);
	expect(replay_view_messages.size() == 3
			&& replay_view_messages[0].size() == 3
			&& replay_view_messages[0][0] == MSG_MULTIPLAYER_REPLAY_VIEW
			&& replay_view_messages[0][1] == 0
			&& replay_view_messages[0][2] == 2,
		"a Battle Royale replay view must identify its primary player and displayed opponent");
	expect(replay_view_messages[1].size() > 1
			&& replay_view_messages[1][0] == MSG_MULTIPLAYER_PRIVATE_PILES
			&& replay_view_messages[1][1] == 0
			&& replay_view_messages[2].size() > 1
			&& replay_view_messages[2][0] == MSG_MULTIPLAYER_PRIVATE_PILES
			&& replay_view_messages[2][1] == 2,
		"a Battle Royale replay view must be followed by complete private-pile snapshots for both displayed players");
	royale_field.publish_multiplayer_replay_view(0, 0);
	expect(take_messages(royale).empty(),
		"a Battle Royale replay view must reject an invalid self-opponent pairing");
	auto* yugi_effect = royale.new_effect();
	yugi_effect->owner = yugi;
	yugi_effect->handler = yugi;
	royale_field.core.reason_effect = yugi_effect;
	expect(royale_field.get_response_player(0) == 3,
		"an effect on Yugi's saved field must prompt Yugi rather than Kaiba");
	auto* joey_effect = royale.new_effect();
	joey_effect->owner = joey;
	joey_effect->handler = joey;
	royale_field.core.reason_effect = joey_effect;
	expect(royale_field.get_response_player(1) == 5,
		"an effect on Joey's saved field must prompt Joey rather than Marik");
	royale_field.core.reason_effect = nullptr;
	expect(royale_field.tag_swap_to(0, 0), "the visible side-0 resources must switch back to Kaiba");
	royale_field.core.attacker = kaiba;
	royale_field.core.attack_target_logical = 1;
	card_vector royale_targets;
	royale_field.get_attack_target(kaiba, &royale_targets);
	expect(royale_targets.size() == 1 && royale_targets.front() == yugi,
		"Kaiba must be able to select Yugi even though both share core side 0");
	royale_field.core.attack_target_logical = 3;
	royale_targets.clear();
	royale_field.get_attack_target(kaiba, &royale_targets);
	expect(royale_targets.size() == 1 && royale_targets.front() == joey,
		"Kaiba must be able to switch the projected opponent field to Joey");
	royale_field.get_logical_lp(0, 0) = 4000;
	royale_field.get_logical_lp(0, 1) = 3000;
	royale_field.get_logical_lp(1, 0) = 2000;
	royale_field.get_logical_lp(1, 1) = 1000;
	expect(royale_field.get_logical_lp(0, 0) == 4000
				&& royale_field.get_logical_lp(0, 1) == 3000
				&& royale_field.get_logical_lp(1, 0) == 2000
				&& royale_field.get_logical_lp(1, 1) == 1000,
			"Battle Royale life points must remain independent for all four players");
	royale_field.core.subunits.clear();
	royale_field.infos.turn_player = 0;
	royale_field.core.attacker = kaiba;
	royale_field.core.attack_target_logical = 2;
	royale_field.core.attack_target_duelist = 0;
	Processors::BattleCommand royale_attack(4);
	expect(!royale_field.process(royale_attack),
		"Battle Royale attack selection must pause for Let me take it");
	auto* royale_attack_prompt =
		Processors::get_opt_variant<Processors::SelectYesNo>(royale_field.core.subunits.back());
	expect(royale_attack_prompt && royale_attack_prompt->playerid == 3,
		"Battle Royale attack interception must prompt the first eligible logical seat");
	royale_field.returns.set<int32_t>(0, 1);
	royale_attack.step = 44;
	expect(!royale_field.process(royale_attack)
			&& royale_field.core.attack_target_logical == 1
			&& royale_field.core.attack_target_duelist == 1,
		"Let me take it must redirect the attack to the accepting Battle Royale player");
	take_messages(royale);
	royale_attack.step = 4;
	expect(!royale_field.process(royale_attack),
		"the redirected Battle Royale attack must resume against its interceptor");
	const auto attack_intercept_view_messages = take_messages(royale);
	expect(attack_intercept_view_messages.size() >= 3
			&& attack_intercept_view_messages[0].size() == 3
			&& attack_intercept_view_messages[0][0] == MSG_MULTIPLAYER_REPLAY_VIEW
			&& attack_intercept_view_messages[0][1] == 0
			&& attack_intercept_view_messages[0][2] == 1,
		"accepting Let me take it for an attack must switch the replay view to the interceptor");
	expect(attack_intercept_view_messages[1][0] == MSG_MULTIPLAYER_PRIVATE_PILES
			&& attack_intercept_view_messages[1][1] == 0
			&& attack_intercept_view_messages[2][0] == MSG_MULTIPLAYER_PRIVATE_PILES
			&& attack_intercept_view_messages[2][1] == 1,
		"the redirected attack replay view must include both complete private-pile snapshots");
	// The final attack message must carry an adjacent authoritative view pair.
	// This is what prevents a replay seek from briefly drawing the arrow from
	// the target's stale field transform back toward the attacker.
	royale_field.core.subunits.clear();
	royale_field.core.attacker = kaiba;
	royale_field.core.attack_target = marik;
	royale_field.core.attack_target_logical = 2;
	royale_field.core.attack_target_duelist = 0;
	take_messages(royale);
	Processors::BattleCommand deterministic_replay_attack(8);
	expect(!royale_field.process(deterministic_replay_attack),
		"a Battle Royale attack must emit its final replay camera and attack messages");
	const auto deterministic_attack_messages = take_messages(royale);
	auto attack_message = std::find_if(deterministic_attack_messages.begin(),
		deterministic_attack_messages.end(), [](const auto& message) {
			return !message.empty() && message[0] == MSG_ATTACK;
		});
	expect(attack_message != deterministic_attack_messages.end()
			&& std::distance(deterministic_attack_messages.begin(), attack_message) >= 3,
		"the final Battle Royale attack must be preceded by a replay-view snapshot pair");
	if(attack_message != deterministic_attack_messages.end()
			&& std::distance(deterministic_attack_messages.begin(), attack_message) >= 3) {
		const auto attack_index = static_cast<size_t>(std::distance(
			deterministic_attack_messages.begin(), attack_message));
		expect(deterministic_attack_messages[attack_index - 3].size() == 3
				&& deterministic_attack_messages[attack_index - 3][0]
					== MSG_MULTIPLAYER_REPLAY_VIEW
				&& deterministic_attack_messages[attack_index - 3][1] == 0
				&& deterministic_attack_messages[attack_index - 3][2] == 2
				&& deterministic_attack_messages[attack_index - 2][0]
					== MSG_MULTIPLAYER_PRIVATE_PILES
				&& deterministic_attack_messages[attack_index - 2][1] == 0
				&& deterministic_attack_messages[attack_index - 1][0]
					== MSG_MULTIPLAYER_PRIVATE_PILES
				&& deterministic_attack_messages[attack_index - 1][1] == 2,
			"P1 attacking P3 must serialize the P1 -> P3 camera immediately before MSG_ATTACK");
		expect(attack_message->size() >= 23
				&& (*attack_message)[attack_message->size() - 2] == 0
				&& (*attack_message)[attack_message->size() - 1] == 2,
			"P1 attacking P3 must append attacker 0 and target 2 in that order");
	}
	royale_field.core.subunits.clear();
	Processors::Damage royale_effect_damage(
		0, nullptr, REASON_EFFECT, 0, kaiba, 1, 600, false, 0, true);
	expect(!royale_field.process(royale_effect_damage),
		"Battle Royale effect damage must pause for Let me take it");
	auto* royale_intercept_prompt =
		Processors::get_opt_variant<Processors::SelectYesNo>(royale_field.core.subunits.back());
	expect(royale_intercept_prompt && royale_intercept_prompt->playerid == 3,
		"Battle Royale must offer interception to the first eligible logical player");
	royale_field.returns.set<int32_t>(0, 1);
	take_messages(royale);
	royale_effect_damage.step = 20;
	expect(!royale_field.process(royale_effect_damage)
			&& royale_effect_damage.playerid == 0
			&& royale_effect_damage.duelist == 1,
		"Battle Royale interception must redirect damage to the accepting player's own LP");
	const auto intercept_view_messages = take_messages(royale);
	expect(intercept_view_messages.size() == 3
			&& intercept_view_messages[0].size() == 3
			&& intercept_view_messages[0][0] == MSG_MULTIPLAYER_REPLAY_VIEW
			&& intercept_view_messages[0][1] == 0
			&& intercept_view_messages[0][2] == 1,
		"accepting Let me take it must switch the replay view to the interceptor");
	expect(intercept_view_messages[1][0] == MSG_MULTIPLAYER_PRIVATE_PILES
			&& intercept_view_messages[1][1] == 0
			&& intercept_view_messages[2][0] == MSG_MULTIPLAYER_PRIVATE_PILES
			&& intercept_view_messages[2][1] == 1,
		"the accepted interceptor replay view must include both complete private-pile snapshots");
	const auto yugi_lp = royale_field.get_logical_lp(0, 1);
	royale_effect_damage.step = 0;
	expect(!royale_field.process(royale_effect_damage),
		"redirected Battle Royale damage must resume without another prompt");
	royale_effect_damage.step = 1;
	expect(!royale_field.process(royale_effect_damage)
			&& royale_field.get_logical_lp(0, 1) == yugi_lp - 600,
		"accepted Battle Royale interception must reduce only the interceptor's LP");
	expect(royale_field.tag_swap_to(0, 1), "the visible side-0 resources must switch to Yugi again");
	expect(royale_field.player[0].list_mzone[0] == kaiba
			&& royale_field.player[0].list_mzone[7] == yugi,
		"a Battle Royale tag swap must preserve both saved monster fields");
	kaiba->current.position = POS_FACEUP_ATTACK;
	yugi->current.position = POS_FACEUP_ATTACK;
	marik->current.position = POS_FACEUP_ATTACK;
	joey->current.position = POS_FACEUP_ATTACK;
	auto* yugi_self_aura = royale.new_effect();
	yugi_self_aura->owner = yugi;
	yugi_self_aura->handler = yugi;
	yugi_self_aura->type = EFFECT_TYPE_FIELD;
	yugi_self_aura->s_range = LOCATION_MZONE;
	expect(yugi_self_aura->is_target(yugi) && !yugi_self_aura->is_target(kaiba),
		"a Battle Royale self-range effect must not affect a same-core opponent");
	card_set self_aura_targets;
	royale_field.filter_affected_cards(yugi_self_aura, &self_aura_targets);
	expect(self_aura_targets.size() == 1 && self_aura_targets.count(yugi) == 1,
		"a self-range aura must contain only its logical owner's field");
	auto* yugi_opponent_aura = royale.new_effect();
	yugi_opponent_aura->owner = yugi;
	yugi_opponent_aura->handler = yugi;
	yugi_opponent_aura->type = EFFECT_TYPE_FIELD;
	yugi_opponent_aura->o_range = LOCATION_MZONE;
	card_set opponent_aura_targets;
	royale_field.filter_affected_cards(yugi_opponent_aura, &opponent_aura_targets);
	expect(opponent_aura_targets.size() == 3
			&& opponent_aura_targets.count(kaiba) == 1
			&& opponent_aura_targets.count(marik) == 1
			&& opponent_aura_targets.count(joey) == 1,
		"an opponent-range aura must affect all three Battle Royale opponents");
	auto configure_shared_hopt = [](effect* peffect, card* handler) {
		peffect->owner = handler;
		peffect->handler = handler;
		peffect->flag[0] = EFFECT_FLAG_COUNT_LIMIT;
		peffect->count_limit = 1;
		peffect->count_limit_max = 1;
		peffect->count_code = 420042;
	};
	auto* yugi_hopt = royale.new_effect();
	auto* kaiba_hopt = royale.new_effect();
	configure_shared_hopt(yugi_hopt, yugi);
	configure_shared_hopt(kaiba_hopt, kaiba);
	yugi_hopt->dec_count(0);
	expect(!yugi_hopt->check_count_limit(0) && kaiba_hopt->check_count_limit(0),
		"once-per-player effect counts must not be shared by same-core Battle Royale players");
	auto add_spell = [&](uint8_t side, uint8_t duelist, uint32_t code) {
		auto* spell = royale.new_card(code);
		spell->data.type = TYPE_SPELL;
		spell->owner = side;
		spell->owner_duelist = duelist;
		royale_field.add_card(side, spell, LOCATION_SZONE, 0, false, duelist);
		return spell;
	};
	auto* kaiba_message = add_spell(0, 0, 4200);
	auto* yugi_message = add_spell(0, 1, 4201);
	auto* marik_message = add_spell(1, 0, 4202);
	auto* joey_message = add_spell(1, 1, 4203);
	(void)kaiba_message;
	(void)yugi_message;
	(void)marik_message;
	(void)joey_message;
	royale_field.core.reason_effect = yugi_effect;
	expect(royale_field.filter_field_card(0, LOCATION_SZONE, 0, nullptr) == 1,
		"a Battle Royale win condition must count only the activating player's spell/trap field");
	auto own_matching_group = royale.new_group();
	royale_field.filter_matching_card(0, 0, LOCATION_SZONE, 0,
		own_matching_group, nullptr, nullptr, 0);
	expect(own_matching_group->container.size() == 1,
		"Battle Royale matching groups must not combine same-core spell/trap fields");
	expect(royale_field.filter_field_card(0, 0, LOCATION_SZONE, nullptr) == 3,
		"an opponent field query must include all three Battle Royale opponents");
	auto opposing_matching_group = royale.new_group();
	royale_field.filter_matching_card(0, 0, 0, LOCATION_SZONE,
		opposing_matching_group, nullptr, nullptr, 0);
	expect(opposing_matching_group->container.size() == 3,
		"Battle Royale matching groups must expose every legal opposing field");
	royale_field.core.reason_effect = nullptr;

	OCG_DuelOptions universal_options = options;
	universal_options.flags = DUEL_UNIVERSAL_MULTIPLAYER;
	universal_options.multiplayer.side1_players = 13;
	universal_options.multiplayer.side2_players = 13;
	universal_options.multiplayer.format = OCG_MULTIPLAYER_FORMAT_SOLO;
	duel universal(universal_options, valid_lua);
	expect(valid_lua, "the 26-player universal Duel must initialize");
	auto& universal_field = *universal.game_field;
	expect(universal_field.multiplayer.mode() == MultiplayerMode::UNIVERSAL
			&& universal_field.multiplayer.player_count() == 26,
		"the universal options must reach the core field");
	const auto first_player_lp = universal_field.get_logical_lp(0, 0);
	universal_field.get_logical_lp(0, 1) = first_player_lp - 500;
	expect(universal_field.get_logical_lp(0, 0) == first_player_lp
			&& universal_field.get_logical_lp(0, 1) == first_player_lp - 500,
		"an empty logical Deck must still receive independent life points");
	universal_field.get_logical_lp(0, 1) = first_player_lp;
	expect(&universal_field.get_logical_list(0, LOCATION_HAND, 0)
			!= &universal_field.get_logical_list(0, LOCATION_HAND, 1),
		"an empty logical Deck must still own an independent private resource set");
	expect(universal_field.player[0].list_mzone.size() == 91
			&& universal_field.player[1].list_mzone.size() == 91
			&& universal_field.player[0].list_szone.size() == 104
			&& universal_field.player[1].list_szone.size() == 104,
		"13 independent monster and spell/trap fields must exist on each core side");
	auto initialize_universal_duelist = [&](uint8_t side, uint8_t duelist, uint32_t code) {
		const OCG_NewCardInfo info{
			side,
			duelist,
			code,
			side,
			LOCATION_DECK,
			0,
			POS_FACEDOWN_DEFENSE
		};
		OCG_DuelNewCard(&universal, &info);
	};
	initialize_universal_duelist(0, 12, 5000);
	initialize_universal_duelist(1, 12, 5001);
	expect(universal_field.tag_swap_to(0, 12) && universal_field.tag_swap_to(1, 12),
		"the thirteenth private resource set on both sides must be selectable");
	auto* side_one_final = universal.new_card(5002);
	side_one_final->owner = 0;
	side_one_final->owner_duelist = 12;
	universal_field.add_card(0, side_one_final, LOCATION_MZONE, 6, false, 12);
	auto* side_two_final = universal.new_card(5003);
	side_two_final->owner = 1;
	side_two_final->owner_duelist = 12;
	universal_field.add_card(1, side_two_final, LOCATION_SZONE, 7, false, 12);
	expect(side_one_final->current.sequence == 90
			&& side_two_final->current.sequence == 103,
		"the final universal fields must keep globally unique internal zone sequences");
	take_messages(universal);
	universal_field.publish_all_multiplayer_private_piles();
	const auto universal_private_messages = take_messages(universal);
	expect(universal_private_messages.size() == 26,
		"the core must publish one independent private-pile snapshot for every universal player");
	for(uint8_t logical = 0; logical < 26; ++logical) {
		expect(universal_private_messages[logical].size() >= 2
				&& universal_private_messages[logical][0] == MSG_MULTIPLAYER_PRIVATE_PILES
				&& universal_private_messages[logical][1] == logical,
			"private-pile snapshots must retain their unique logical-player route");
	}

	OCG_DuelOptions universal_team_options = options;
	universal_team_options.flags = DUEL_UNIVERSAL_MULTIPLAYER;
	universal_team_options.multiplayer.side1_players = 2;
	universal_team_options.multiplayer.side2_players = 2;
	universal_team_options.multiplayer.format = OCG_MULTIPLAYER_FORMAT_TEAMS;
	// Logical players 0 and 2 are allies even though they use opposite core
	// sides. Players 1 and 3 form the other team.
	universal_team_options.multiplayer.teams[0] = 0;
	universal_team_options.multiplayer.teams[1] = 1;
	universal_team_options.multiplayer.teams[2] = 0;
	universal_team_options.multiplayer.teams[3] = 1;
	bool universal_team_valid_lua = true;
	duel universal_team(universal_team_options, universal_team_valid_lua);
	expect(universal_team_valid_lua, "the Universal Teams Lua runtime must initialize");
	auto& universal_team_field = *universal_team.game_field;
	initialize_extra_duelist(universal_team, 1, 6001);
	const OCG_NewCardInfo universal_team_side_two_extra{
		1,
		1,
		6003,
		1,
		LOCATION_DECK,
		0,
		POS_FACEDOWN_DEFENSE
	};
	OCG_DuelNewCard(&universal_team, &universal_team_side_two_extra);
	auto add_universal_team_monster = [&](uint8_t side, uint8_t duelist, uint32_t code) {
		auto* monster = universal_team.new_card(code);
		monster->owner = side;
		monster->owner_duelist = duelist;
		monster->current.position = POS_FACEUP_ATTACK;
		universal_team_field.add_card(side, monster, LOCATION_MZONE, 0, false, duelist);
		return monster;
	};
	auto* team_zero_player_zero = add_universal_team_monster(0, 0, 6100);
	auto* team_one_player_one = add_universal_team_monster(0, 1, 6101);
	auto* team_zero_player_two = add_universal_team_monster(1, 0, 6102);
	auto* team_one_player_three = add_universal_team_monster(1, 1, 6103);
	expect(team_zero_player_zero->current.sequence == 0
			&& team_one_player_one->current.sequence == 7
			&& team_zero_player_two->current.sequence == 0
			&& team_one_player_three->current.sequence == 7,
		"Universal Teams must preserve every teammate's independent field");
	auto* team_zero_opponent_aura = universal_team.new_effect();
	team_zero_opponent_aura->owner = team_zero_player_zero;
	team_zero_opponent_aura->handler = team_zero_player_zero;
	team_zero_opponent_aura->type = EFFECT_TYPE_FIELD;
	team_zero_opponent_aura->o_range = LOCATION_MZONE;
	card_set universal_team_opponents;
	universal_team_field.filter_affected_cards(team_zero_opponent_aura,
		&universal_team_opponents);
	expect(universal_team_opponents.size() == 2
			&& universal_team_opponents.count(team_one_player_one) == 1
			&& universal_team_opponents.count(team_one_player_three) == 1
			&& universal_team_opponents.count(team_zero_player_two) == 0,
		"opponent-range effects in Universal Teams must exclude allies on either core side");
	universal_team_field.core.attacker = team_zero_player_zero;
	universal_team_field.core.attack_target_logical = 2;
	card_vector universal_team_targets;
	universal_team_field.get_attack_target(team_zero_player_zero, &universal_team_targets);
	expect(universal_team_targets.empty(),
		"a Universal Teams player must not be allowed to attack a cross-side ally");
	universal_team_field.core.attack_target_logical = 3;
	universal_team_targets.clear();
	universal_team_field.get_attack_target(team_zero_player_zero, &universal_team_targets);
	expect(universal_team_targets.size() == 1
			&& universal_team_targets.front() == team_one_player_three,
		"a Universal Teams attack must project the selected enemy's independent field");
	auto* team_zero_lp_protection = universal_team.new_effect();
	team_zero_player_zero->set_status(STATUS_EFFECT_ENABLED, TRUE);
	team_zero_lp_protection->owner = team_zero_player_zero;
	team_zero_lp_protection->type = EFFECT_TYPE_FIELD;
	team_zero_lp_protection->code = EFFECT_CANNOT_LOSE_LP;
	team_zero_lp_protection->flag[0] = EFFECT_FLAG_PLAYER_TARGET;
	team_zero_lp_protection->range = LOCATION_MZONE;
	team_zero_lp_protection->s_range = 1;
	team_zero_player_zero->add_effect(team_zero_lp_protection);
	expect(universal_team_field.is_logical_player_affected_by_effect(0, 0,
			EFFECT_CANNOT_LOSE_LP) == team_zero_lp_protection,
		"player protection must apply to the exact logical player that owns it");
	expect(!universal_team_field.is_logical_player_affected_by_effect(0, 1,
			EFFECT_CANNOT_LOSE_LP),
		"player protection must not leak to another player on the same core side");
	expect(!universal_team_field.is_logical_player_affected_by_effect(1, 0,
			EFFECT_CANNOT_LOSE_LP),
		"player protection must not leak to a cross-side teammate");
	auto* team_zero_cannot_draw = universal_team.new_effect();
	team_zero_cannot_draw->owner = team_zero_player_zero;
	team_zero_cannot_draw->type = EFFECT_TYPE_FIELD;
	team_zero_cannot_draw->code = EFFECT_CANNOT_DRAW;
	team_zero_cannot_draw->flag[0] = EFFECT_FLAG_PLAYER_TARGET;
	team_zero_cannot_draw->range = LOCATION_MZONE;
	team_zero_cannot_draw->s_range = 1;
	team_zero_player_zero->add_effect(team_zero_cannot_draw);
	expect(!universal_team_field.is_player_can_draw(0, 0),
		"a logical player's cannot-draw effect must apply to that player");
	expect(universal_team_field.is_player_can_draw(0, 1),
		"a logical player's cannot-draw effect must not block a same-side player");
	expect(universal_team_field.is_player_can_draw(1, 0),
		"a logical player's cannot-draw effect must not block a cross-side teammate");
	universal_team_field.core.subunits.clear();
	Processors::Damage team_effect_damage(0, nullptr, REASON_EFFECT, 0,
		team_zero_player_zero, 0, 500, false, 1, true);
	expect(!universal_team_field.process(team_effect_damage),
		"Universal Teams effect damage must pause for a teammate interception");
	auto* team_intercept_prompt =
		Processors::get_opt_variant<Processors::SelectYesNo>(
			universal_team_field.core.subunits.back());
	expect(team_intercept_prompt && team_intercept_prompt->playerid == 5,
		"only the victim's cross-side teammate must receive the interception prompt");
	universal_team_field.core.subunits.clear();

	take_messages(universal_team);
	const auto first_team_elimination = OCG_DuelEliminatePlayer(&universal_team,
		1, static_cast<uint8_t>(PlayerEliminationReason::SURRENDER));
	expect((first_team_elimination & OCG_MULTIPLAYER_ELIMINATION_APPLIED)
			&& !(first_team_elimination & OCG_MULTIPLAYER_ELIMINATION_FINISHED),
		"eliminating one Universal Teams member must leave their teammate active");
	const auto final_team_elimination = OCG_DuelEliminatePlayer(&universal_team,
		3, static_cast<uint8_t>(PlayerEliminationReason::SURRENDER));
	expect(final_team_elimination & OCG_MULTIPLAYER_ELIMINATION_FINISHED,
		"eliminating the final member of a team must finish Universal Teams");
	const auto universal_team_win_messages = take_messages(universal_team);
	const auto win_message = std::find_if(universal_team_win_messages.begin(),
		universal_team_win_messages.end(), [](const auto& message) {
			return !message.empty() && message[0] == MSG_WIN;
		});
	expect(win_message != universal_team_win_messages.end()
			&& win_message->size() == 5
			&& (*win_message)[1] == PLAYER_NONE
			&& (*win_message)[3] == MultiplayerState::NO_PLAYER
			&& (*win_message)[4] == 0,
		"Universal Teams must encode the winning team without inventing an invalid physical-side winner");

	std::cout << "All multiplayer field tests passed.\n";
	return 0;
}
