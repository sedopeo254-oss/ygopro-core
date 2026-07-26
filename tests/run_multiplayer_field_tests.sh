#!/usr/bin/env sh
set -eu

root_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir="${TMPDIR:-/tmp}/ygopro-core-multiplayer-field-tests"
extra_cxxflags=${EXTRA_CXXFLAGS:-}
lua_src_dir=${LUA_SRC_DIR:-"$root_dir/lua/src"}

mkdir -p "$build_dir"

lua_objects=""
for source in "$lua_src_dir"/*.c; do
	case "$(basename "$source")" in
		lua.c|ltests.c|onelua.c) continue ;;
	esac
	object="$build_dir/$(basename "${source%.c}").o"
	# The core intentionally compiles Lua as C++ so longjmp unwinding remains safe.
	"${CXX:-c++}" -x c++ -std=c++20 $extra_cxxflags -I"$lua_src_dir" -c "$source" -o "$object"
	lua_objects="$lua_objects $object"
done

"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -Wno-missing-field-initializers $extra_cxxflags \
	-I"$root_dir" -I"$lua_src_dir" \
	"$root_dir/card.cpp" \
	"$root_dir/duel.cpp" \
	"$root_dir/effect.cpp" \
	"$root_dir/field.cpp" \
	"$root_dir/interpreter.cpp" \
	"$root_dir/libcard.cpp" \
	"$root_dir/libdebug.cpp" \
	"$root_dir/libduel.cpp" \
	"$root_dir/libeffect.cpp" \
	"$root_dir/libgroup.cpp" \
	"$root_dir/multiplayer.cpp" \
	"$root_dir/ocgapi.cpp" \
	"$root_dir/operations.cpp" \
	"$root_dir/playerop.cpp" \
	"$root_dir/processor.cpp" \
	"$root_dir/processor_visit.cpp" \
	"$root_dir/scriptlib.cpp" \
	"$root_dir/tests/multiplayer_field_tests.cpp" \
	$lua_objects -ldl -lm -o "$build_dir/multiplayer-field-tests"

"$build_dir/multiplayer-field-tests"
