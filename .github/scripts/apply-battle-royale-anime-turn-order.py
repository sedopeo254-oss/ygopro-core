from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{path}: expected exactly one replacement site, found {count}')
    path.write_text(text.replace(old, new, 1), encoding='utf-8')


# 1) Battle Royale turn-order API. It is deliberately rejected by 3v1.
multiplayer_h = ROOT / 'multiplayer.h'
replace_once(multiplayer_h,
'''\tuint8_t current_player() const;\n\tuint8_t advance_turn();\n\tuint8_t next_active_player(uint8_t player) const;\n''',
'''\tuint8_t current_player() const;\n\tuint8_t advance_turn();\n\tuint8_t next_active_player(uint8_t player) const;\n\tbool set_turn_order_by_attack(const std::array<int32_t, MAX_PLAYERS>& attack_values);\n''')

multiplayer_cpp = ROOT / 'multiplayer.cpp'
replace_once(multiplayer_cpp,
'''#include "multiplayer.h"\n''',
'''#include <algorithm>\n#include "multiplayer.h"\n''')
insert_marker = '''uint8_t MultiplayerState::current_player() const {\n'''
text = multiplayer_cpp.read_text(encoding='utf-8')
pos = text.find(insert_marker)
if pos < 0:
    raise SystemExit('multiplayer.cpp: current_player marker not found')
method = r'''bool MultiplayerState::set_turn_order_by_attack(
        const std::array<int32_t, MAX_PLAYERS>& attack_values) {
    if(duel_mode != MultiplayerMode::BATTLE_ROYALE)
        return false;
    std::array<uint8_t, MAX_PLAYERS> ordered{ 0, 2, 1, 3 };
    std::stable_sort(ordered.begin(), ordered.end(), [&](uint8_t lhs, uint8_t rhs) {
        return attack_values[lhs] > attack_values[rhs];
    });
    turn_order = ordered;
    turn_player = turn_order[0];
    return true;
}

'''
multiplayer_cpp.write_text(text[:pos] + method + text[pos:], encoding='utf-8')

unit_h = ROOT / 'processor_unit.h'
replace_once(unit_h, '#include <cstdint>\n', '#include <array>\n#include <cstdint>\n')
replace_once(unit_h,
'''struct Startup : public Process<false> {\n\tStartup(uint16_t step_) : Process(step_) {}\n};\n''',
'''struct Startup : public Process<false> {\n\tStartup(uint16_t step_) : Process(step_) {}\n};\nstruct BattleRoyaleTurnOrder : public Process<false> {\n\tuint8_t logical_player{ 0 };\n\tstd::array<card*, 4> selected_cards{};\n\tstd::array<int32_t, 4> selected_attack{};\n\tBattleRoyaleTurnOrder(uint16_t step_) : Process(step_) {}\n};\n''')
replace_once(unit_h,
'''struct SelectCard : public Process<true> {\n\tuint8_t playerid;\n\tbool cancelable;\n\tuint8_t min;\n\tuint8_t max;\n\tSelectCard(uint16_t step_, uint8_t playerid_, bool cancelable_,\n\t\t\t\t\t uint8_t min_, uint8_t max_) :\n\t\tProcess(step_), playerid(playerid_), cancelable(cancelable_), min(min_), max(max_) {}\n};\n''',
'''struct SelectCard : public Process<true> {\n\tuint8_t playerid;\n\tuint8_t logical_player;\n\tbool cancelable;\n\tuint8_t min;\n\tuint8_t max;\n\tSelectCard(uint16_t step_, uint8_t playerid_, bool cancelable_,\n\t\t\t\t\t uint8_t min_, uint8_t max_, uint8_t logical_player_ = 0xff) :\n\t\tProcess(step_), playerid(playerid_), logical_player(logical_player_),\n\t\tcancelable(cancelable_), min(min_), max(max_) {}\n};\n''')
replace_once(unit_h,
'''using processors = std::variant<Adjust, Turn, RefreshLoc, Startup,\n''',
'''using processors = std::variant<Adjust, Turn, RefreshLoc, Startup, BattleRoyaleTurnOrder,\n''')

playerop = ROOT / 'playerop.cpp'
replace_once(playerop,
'''bool field::process(Processors::SelectCard& arg) {\n\tauto playerid = arg.playerid;\n\tconst auto selecting_player = get_response_player(playerid);\n''',
'''bool field::process(Processors::SelectCard& arg) {\n\tauto playerid = arg.playerid;\n\tconst auto selecting_player = multiplayer.enabled()\n\t\t\t&& arg.logical_player < MultiplayerState::MAX_PLAYERS\n\t\t? multiplayer.prompt_player_of(arg.logical_player)\n\t\t: get_response_player(playerid);\n''')

field_h = ROOT / 'field.h'
replace_once(field_h,
'''\tbool process(Processors::Startup& arg);\n\tbool process(Processors::RefreshRelay& arg);\n''',
'''\tbool process(Processors::Startup& arg);\n\tbool process(Processors::BattleRoyaleTurnOrder& arg);\n\tbool process(Processors::RefreshRelay& arg);\n''')

preduel_cpp = ROOT / 'battle_royale_turn_order.cpp'
preduel_cpp.write_text(r'''#include <algorithm>
#include "card.h"
#include "duel.h"
#include "field.h"

bool field::process(Processors::BattleRoyaleTurnOrder& arg) {
    if(multiplayer.mode() != MultiplayerMode::BATTLE_ROYALE)
        return TRUE;
    switch(arg.step) {
    case 0: {
        while(arg.logical_player < MultiplayerState::MAX_PLAYERS
                && !multiplayer.is_active(arg.logical_player))
            ++arg.logical_player;
        if(arg.logical_player >= MultiplayerState::MAX_PLAYERS) {
            multiplayer.set_turn_order_by_attack(arg.selected_attack);
            return TRUE;
        }
        const auto side = multiplayer.field_side_of(arg.logical_player);
        const auto duelist = multiplayer.duelist_index_of(arg.logical_player);
        auto& deck = get_logical_list(side, LOCATION_DECK, duelist);
        core.select_cards.clear();
        for(auto* pcard : deck) {
            if(pcard && (pcard->data.type & TYPE_MONSTER))
                core.select_cards.push_back(pcard);
        }
        if(core.select_cards.empty()) {
            arg.selected_cards[arg.logical_player] = nullptr;
            arg.selected_attack[arg.logical_player] = 0;
            ++arg.logical_player;
            arg.step = Processors::restart;
            return FALSE;
        }
        emplace_process<Processors::SelectCard>(side, false, 1, 1,
            arg.logical_player);
        return FALSE;
    }
    case 1: {
        const auto logical = arg.logical_player;
        if(return_cards.list.size() == 1 && return_cards.list.front()) {
            auto* selected = return_cards.list.front();
            arg.selected_cards[logical] = selected;
            arg.selected_attack[logical] = std::max<int32_t>(0, selected->data.attack);
        } else {
            arg.selected_cards[logical] = nullptr;
            arg.selected_attack[logical] = 0;
        }
        ++arg.logical_player;
        if(arg.logical_player < MultiplayerState::MAX_PLAYERS) {
            arg.step = Processors::restart;
            return FALSE;
        }
        for(uint8_t player = 0; player < MultiplayerState::MAX_PLAYERS; ++player) {
            auto* selected = arg.selected_cards[player];
            if(!selected)
                continue;
            const auto side = multiplayer.field_side_of(player);
            const auto duelist = multiplayer.duelist_index_of(player);
            const auto from = selected->get_info_location();
            selected->current.reason = REASON_RULE;
            selected->current.reason_effect = nullptr;
            selected->current.reason_player = PLAYER_NONE;
            remove_card(selected);
            selected->sendto_param.position = POS_FACEUP;
            selected->current.position = POS_FACEUP;
            add_card(side, selected, LOCATION_REMOVED, 0, false, duelist);
            auto message = pduel->new_message(MSG_MOVE);
            message->write<uint32_t>(selected->data.code);
            message->write(from);
            message->write(selected->get_info_location());
            message->write<uint32_t>(REASON_RULE);
        }
        multiplayer.set_turn_order_by_attack(arg.selected_attack);
        publish_all_multiplayer_private_piles();
        return TRUE;
    }
    }
    return TRUE;
}
''', encoding='utf-8')

processor_cpp = ROOT / 'processor.cpp'
old_startup = '''\tcase 1: {\n\t\tfor(int p = 0; p < 2; p++) {\n\t\t\tcore.shuffle_hand_check[p] = false;\n\t\t\tcore.shuffle_deck_check[p] = false;\n\t\t\tif(player[p].start_count > 0)\n\t\t\t\tdraw(nullptr, REASON_RULE, PLAYER_NONE, p, player[p].start_count);\n\t\t\tauto list_size = player[p].extra_lists_main.size();\n\t\t\tfor(size_t l = 0; l < list_size; l++) {\n\t\t\t\tauto& main = player[p].extra_lists_main[l];\n\t\t\t\tauto& hand = player[p].extra_lists_hand[l];\n\t\t\t\tfor(int i = 0; i < player[p].start_count && !main.empty(); ++i) {\n\t\t\t\t\tcard* pcard = main.back();\n\t\t\t\t\tmain.pop_back();\n\t\t\t\t\thand.push_back(pcard);\n\t\t\t\t\tpcard->current.controler = p;\n\t\t\t\t\tpcard->current.location = LOCATION_HAND;\n\t\t\t\t\tpcard->current.sequence = static_cast<uint32_t>(hand.size() - 1);\n\t\t\t\t\tpcard->current.position = POS_FACEDOWN;\n\t\t\t\t}\n\n\t\t\t}\n\t\t}\n\t\templace_process<Processors::Turn>(0);\n\t\treturn TRUE;\n\t}\n'''
new_startup = '''\tcase 1: {\n\t\tif(multiplayer.mode() == MultiplayerMode::BATTLE_ROYALE) {\n\t\t\templace_process<Processors::BattleRoyaleTurnOrder>();\n\t\t\treturn FALSE;\n\t\t}\n\t\tfor(int p = 0; p < 2; p++) {\n\t\t\tcore.shuffle_hand_check[p] = false;\n\t\t\tcore.shuffle_deck_check[p] = false;\n\t\t\tif(player[p].start_count > 0)\n\t\t\t\tdraw(nullptr, REASON_RULE, PLAYER_NONE, p, player[p].start_count);\n\t\t\tauto list_size = player[p].extra_lists_main.size();\n\t\t\tfor(size_t l = 0; l < list_size; l++) {\n\t\t\t\tauto& main = player[p].extra_lists_main[l];\n\t\t\t\tauto& hand = player[p].extra_lists_hand[l];\n\t\t\t\tfor(int i = 0; i < player[p].start_count && !main.empty(); ++i) {\n\t\t\t\t\tcard* pcard = main.back();\n\t\t\t\t\tmain.pop_back();\n\t\t\t\t\thand.push_back(pcard);\n\t\t\t\t\tpcard->current.controler = p;\n\t\t\t\t\tpcard->current.location = LOCATION_HAND;\n\t\t\t\t\tpcard->current.sequence = static_cast<uint32_t>(hand.size() - 1);\n\t\t\t\t\tpcard->current.position = POS_FACEDOWN;\n\t\t\t\t}\n\n\t\t\t}\n\t\t}\n\t\templace_process<Processors::Turn>(0);\n\t\treturn TRUE;\n\t}\n\tcase 2: {\n\t\tfor(int p = 0; p < 2; p++) {\n\t\t\tcore.shuffle_hand_check[p] = false;\n\t\t\tcore.shuffle_deck_check[p] = false;\n\t\t\tif(player[p].start_count > 0)\n\t\t\t\tdraw(nullptr, REASON_RULE, PLAYER_NONE, p, player[p].start_count);\n\t\t\tauto list_size = player[p].extra_lists_main.size();\n\t\t\tfor(size_t l = 0; l < list_size; l++) {\n\t\t\t\tauto& main = player[p].extra_lists_main[l];\n\t\t\t\tauto& hand = player[p].extra_lists_hand[l];\n\t\t\t\tfor(int i = 0; i < player[p].start_count && !main.empty(); ++i) {\n\t\t\t\t\tcard* pcard = main.back();\n\t\t\t\t\tmain.pop_back();\n\t\t\t\t\thand.push_back(pcard);\n\t\t\t\t\tpcard->current.controler = p;\n\t\t\t\t\tpcard->current.location = LOCATION_HAND;\n\t\t\t\t\tpcard->current.sequence = static_cast<uint32_t>(hand.size() - 1);\n\t\t\t\t\tpcard->current.position = POS_FACEDOWN;\n\t\t\t\t}\n\t\t\t}\n\t\t}\n\t\tconst auto first_logical = multiplayer.current_player();\n\t\templace_process<Processors::Turn>(multiplayer.field_side_of(first_logical));\n\t\treturn TRUE;\n\t}\n'''
replace_once(processor_cpp, old_startup, new_startup)

tests = ROOT / 'tests' / 'multiplayer_state_tests.cpp'
insert_before = '''void test_battle_royale_multi_elimination() {\n'''
text = tests.read_text(encoding='utf-8')
pos = text.find(insert_before)
if pos < 0:
    raise SystemExit('multiplayer_state_tests.cpp: insertion marker not found')
test_code = r'''void test_battle_royale_anime_attack_turn_order() {
    MultiplayerState state;
    state.configure(MultiplayerMode::BATTLE_ROYALE);
    const std::array<int32_t, MultiplayerState::MAX_PLAYERS> anime_atk{
        3300, 1300, 1700, 500
    };
    expect(state.set_turn_order_by_attack(anime_atk),
        "Battle Royale must accept the anime ATK turn-order rule");
    expect(state.current_player() == 0, "Kaiba 3300 must go first");
    expect(state.advance_turn() == 2, "Marik 1700 must go second");
    expect(state.advance_turn() == 1, "Yugi 1300 must go third");
    expect(state.advance_turn() == 3, "Joey 500 must go last");

    MultiplayerState changed;
    changed.configure(MultiplayerMode::BATTLE_ROYALE);
    const std::array<int32_t, MultiplayerState::MAX_PLAYERS> changed_atk{
        500, 3300, 1300, 1700
    };
    expect(changed.set_turn_order_by_attack(changed_atk),
        "Battle Royale must support any ATK-derived order");
    expect(changed.current_player() == 1 && changed.advance_turn() == 3
            && changed.advance_turn() == 2 && changed.advance_turn() == 0,
        "turn order must be sorted highest ATK to lowest ATK");

    MultiplayerState tied;
    tied.configure(MultiplayerMode::BATTLE_ROYALE);
    const std::array<int32_t, MultiplayerState::MAX_PLAYERS> tied_atk{
        1000, 1000, 1000, 1000
    };
    expect(tied.set_turn_order_by_attack(tied_atk),
        "Battle Royale ties must remain deterministic");
    expect(tied.current_player() == 0 && tied.advance_turn() == 2
            && tied.advance_turn() == 1 && tied.advance_turn() == 3,
        "equal ATK must preserve A1-B1-A2-B2");

    MultiplayerState protected_three_vs_one;
    protected_three_vs_one.configure(MultiplayerMode::THREE_V_ONE);
    expect(!protected_three_vs_one.set_turn_order_by_attack(anime_atk),
        "3v1 must reject Battle Royale turn-order changes");
    expect(protected_three_vs_one.current_player() == 0
            && protected_three_vs_one.advance_turn() == 1
            && protected_three_vs_one.advance_turn() == 2
            && protected_three_vs_one.advance_turn() == 3,
        "3v1 order must remain Serenity-Tristan-Duke-Nezbitt");
}

'''
tests.write_text(text[:pos] + test_code + text[pos:], encoding='utf-8')
replace_once(tests,
'''\ttest_battle_royale_turn_order_and_skip();\n\ttest_battle_royale_multi_elimination();\n''',
'''\ttest_battle_royale_turn_order_and_skip();\n\ttest_battle_royale_anime_attack_turn_order();\n\ttest_battle_royale_multi_elimination();\n''')

print('Applied anime Battle Royale pre-duel monster-selection turn order.')
