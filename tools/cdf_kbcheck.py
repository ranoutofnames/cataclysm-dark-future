#!/usr/bin/env python3
"""Check a user's keybindings.json against the shipped defaults.

AT2a: find stale bindings before they hide the DDA inventory actions.

The merge rules below mirror ``input_manager::load`` in ``src/input.cpp``
(around lines 255-290): a user entry for an existing action id replaces that
action's keys and clears ``is_deleted``; a user ``is_deleted`` entry in a
non-default context clears the keys and marks the action deleted; a user entry
with an empty binding list for an action the context does not already contain
is ignored (the backwards-compatibility rule for old experimental builds).

Output is tab-separated on stdout (one row per finding, header first) so it can
be paged, diffed, or loaded into a spreadsheet. Summary counts go to stderr.

The required action list must stay in sync with ``required_inventory_actions``
in ``src/input.h``.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

DEFAULT_CONTEXT = "default"
REQUIRED_CONTEXT = "INVENTORY"

# Keep in sync with required_inventory_actions in src/input.h.
# SHOW_HIDE_CONTENTS_ALL has no default key in the shipped data (DDA has none
# either), so "NO KEY" is a normal result for it. SHOW_HIDE_CONTENTS is the
# row/container collapse action (">" in the INVENTORY context).
REQUIRED_INVENTORY_ACTIONS = [
    "NEXT_COLUMN",
    "PREV_COLUMN",
    "TOGGLE_ENTRY",
    "MARK_WITH_COUNT",
    "TOGGLE_NON_FAVORITE",
    "EXAMINE_CONTENTS",
    "SHOW_HIDE_CONTENTS_ALL",
    "RESET_FILTER",
    "SHOW_HIDE_CONTENTS",
]

TSV_HEADER = "kind\tcontext\taction\tdefault_keys\tuser_keys\teffective_keys\tdetail"


def _key_name_to_code() -> dict[str, int]:
    """Mirror input_manager::init_keycode_mapping (src/input.cpp:365-442)."""
    names: dict[str, int] = {}
    # Printable ASCII maps to itself.
    for code in range(32, 127):
        names[chr(code)] = code
    names.update(
        {
            "TAB": 9,
            "BACKTAB": 0x161,
            "SPACE": 32,
            "UP": 0x103,
            "DOWN": 0x102,
            "LEFT": 0x104,
            "RIGHT": 0x105,
            "NPAGE": 0x152,
            "PPAGE": 0x153,
            "ESC": 27,
            "BACKSPACE": 0x107,
            "HOME": 0x106,
            "BREAK": 0x101,
            "END": 0x168,
            "RETURN": 10,
            "DELETE": 0x151,
            "NUMPAD_0": 0x40A,
            "NUMPAD_1": 0x401,
            "NUMPAD_2": 0x402,
            "NUMPAD_3": 0x403,
            "NUMPAD_4": 0x404,
            "NUMPAD_5": 0x405,
            "NUMPAD_6": 0x406,
            "NUMPAD_7": 0x407,
            "NUMPAD_8": 0x408,
            "NUMPAD_9": 0x409,
        }
    )
    # Control characters that are not named keep the CTRL+<letter> spelling.
    for code in range(0, 32):
        if code in (9, 10, 27):
            continue
        names[f"CTRL+{chr(code + 64)}"] = code
    for n in range(1, 64):
        names[f"F{n}"] = 0x108 + n
    for n in range(8):
        names[f"JOY_{n}"] = n
    names.update(
        {
            "JOY_LEFT": 257,
            "JOY_RIGHT": 258,
            "JOY_UP": 259,
            "JOY_DOWN": 260,
            "JOY_RIGHTUP": 261,
            "JOY_RIGHTDOWN": 262,
            "JOY_LEFTUP": 263,
            "JOY_LEFTDOWN": 264,
            "MOUSE_LEFT": 1,
            "MOUSE_RIGHT": 2,
            "SCROLL_UP": 3,
            "SCROLL_DOWN": 4,
            "MOUSE_MOVE": 5,
            "MOUSE_LEFT_PRESSED": 6,
        }
    )
    return names


KEY_CODE = _key_name_to_code()


def key_code(name: str) -> int:
    """Return the game's keycode for a portable key name, or 0 if invalid."""
    if name in KEY_CODE:
        return KEY_CODE[name]
    if name.startswith("UNKNOWN_"):
        try:
            return int(name[8:])
        except ValueError:
            return 0
    return 0


def _parse_bindings(bindings: object) -> list[dict]:
    """Turn one JSON ``bindings`` array into input_event-like dicts."""
    events = []
    if not isinstance(bindings, list):
        return events
    for binding in bindings:
        if not isinstance(binding, dict):
            continue
        method = binding.get("input_method", "")
        raw_key = binding.get("key", [])
        names = raw_key if isinstance(raw_key, list) else [raw_key]
        names = [str(n) for n in names]
        codes = [key_code(n) for n in names]
        # load() keeps the event even when a key name is invalid; the invalid
        # name is dropped from the sequence, so do the same here.
        codes = [c for c in codes if c != 0]
        events.append({"method": method, "names": names, "codes": codes})
    return events


def _event_label(event: dict) -> str:
    return "+".join(event["names"]) if event["names"] else "?"


def events_label(events: list[dict]) -> str:
    if not events:
        return "NO KEY"
    return ";".join(_event_label(e) for e in events)


def _event_signature(event: dict) -> tuple:
    return (event["method"], tuple(event["codes"]))


def _new_action(name: str = "") -> dict:
    return {"name": name, "events": [], "is_deleted": False, "is_user_created": False}


def load_defaults(directory: Path) -> dict[str, dict[str, dict]]:
    """Load every *.json under ``directory`` like input_manager::init does."""
    contexts: dict[str, dict[str, dict]] = {}
    files = sorted(p for p in directory.rglob("*.json") if p.is_file())
    for path in files:
        with path.open("r", encoding="utf-8") as handle:
            data = json.load(handle)
        for obj in data:
            if obj.get("type", "keybinding") != "keybinding":
                print(
                    f"warning: {path}: skipping object of type "
                    f"{obj.get('type')!r}",
                    file=sys.stderr,
                )
                continue
            action_id = obj.get("id")
            if not action_id:
                continue
            context = obj.get("category", DEFAULT_CONTEXT)
            actions = contexts.setdefault(context, {})
            action = actions.setdefault(action_id, _new_action())
            if "name" in obj:
                action["name"] = obj["name"]
            action["events"] = _parse_bindings(obj.get("bindings", []))
    return contexts


def load_user(path: Path) -> list[dict]:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    return [obj for obj in data if obj.get("type", "keybinding") == "keybinding"]


def merge_user(
    defaults: dict[str, dict[str, dict]], user_objects: list[dict]
) -> tuple[dict[str, dict[str, dict]], dict[tuple[str, str], str]]:
    """Apply the user file on top of the defaults (input.cpp:255-290).

    Returns the merged contexts and a per (context, action) user state map:
    ``overridden`` (with empty or non-empty events), ``deleted``, or ``ignored``.
    """
    merged = {
        context: {action_id: dict(action) for action_id, action in actions.items()}
        for context, actions in defaults.items()
    }
    states: dict[tuple[str, str], str] = {}

    for obj in user_objects:
        action_id = obj.get("id")
        if not action_id:
            continue
        context = obj.get("category", DEFAULT_CONTEXT)
        actions = merged.setdefault(context, {})
        events = _parse_bindings(obj.get("bindings", []))

        if context != DEFAULT_CONTEXT and obj.get("is_deleted", False):
            if action_id in actions:
                action = actions[action_id]
                action["events"] = []
                action["is_user_created"] = False
                action["is_deleted"] = True
            states[(context, action_id)] = "deleted"
            continue

        if (
            events
            or context == DEFAULT_CONTEXT
            or action_id in actions
        ):
            action = actions.setdefault(action_id, _new_action())
            action["events"] = events
            action["is_deleted"] = False
            if "is_user_created" in obj:
                action["is_user_created"] = bool(obj["is_user_created"])
            states[(context, action_id)] = "overridden"
        else:
            states[(context, action_id)] = "ignored"

    return merged, states


def effective_events(
    merged: dict[str, dict[str, dict]], context: str, action_id: str
) -> list[dict]:
    """Resolve an action in a context the way get_action_attributes does."""
    local = merged.get(context, {}).get(action_id)
    if local is not None and not local["is_deleted"]:
        return local["events"]
    return merged.get(DEFAULT_CONTEXT, {}).get(action_id, {}).get("events", [])


def find_collisions(merged: dict[str, dict[str, dict]]) -> list[dict]:
    """Find one key bound to two actions in the same context after merging."""
    rows = []

    def collisions_for(context: str) -> dict[tuple, list[str]]:
        by_key: dict[tuple, list[str]] = defaultdict(list)
        action_ids = set(merged.get(DEFAULT_CONTEXT, {}))
        action_ids.update(merged.get(context, {}))
        for action_id in sorted(action_ids):
            for event in effective_events(merged, context, action_id):
                if event["codes"]:
                    by_key[_event_signature(event)].append(action_id)
        return {
            key: sorted(set(ids))
            for key, ids in by_key.items()
            if len(set(ids)) > 1
        }

    default_collisions = collisions_for(DEFAULT_CONTEXT)
    for key, ids in sorted(default_collisions.items()):
        rows.append(
            {
                "context": DEFAULT_CONTEXT,
                "actions": ids,
                "events": _label_for_signature(key),
                "detail": "inherited by every context",
            }
        )

    for context in sorted(merged):
        if context == DEFAULT_CONTEXT:
            continue
        for key, ids in sorted(collisions_for(context).items()):
            if key in default_collisions and all(
                action_id not in merged.get(context, {}) or
                merged[context][action_id]["is_deleted"]
                for action_id in ids
            ):
                # Purely inherited collision, already reported for "default".
                continue
            rows.append(
                {
                    "context": context,
                    "actions": ids,
                    "events": _label_for_signature(key),
                    "detail": "context-local binding shadows another action",
                }
            )
    return rows


def _label_for_signature(signature: tuple) -> str:
    method, codes = signature
    name_by_code: dict[int, str] = {}
    for name, code in KEY_CODE.items():
        current = name_by_code.get(code)
        # Prefer a named key ("SPACE") over its one-character spelling (" ").
        if current is None or (len(current) == 1 and len(name) > 1):
            name_by_code[code] = name
    label = "+".join(name_by_code.get(code, f"UNKNOWN_{code}") for code in codes)
    if method and method != "keyboard":
        return f"{method}:{label}"
    return label


def analyze(
    defaults_dir: Path, user_path: Path | None, collision_contexts: set[str] | None = None
) -> dict:
    """Run every check and return structured results."""
    defaults = load_defaults(defaults_dir)
    if user_path is None:
        merged = {
            context: {action_id: dict(action) for action_id, action in actions.items()}
            for context, actions in defaults.items()
        }
        states = {}
        user_objects = []
    else:
        user_objects = load_user(user_path)
        merged, states = merge_user(defaults, user_objects)

    overrides = []
    deleted = []
    for (context, action_id), state in sorted(states.items()):
        default_keys = events_label(
            defaults.get(context, {}).get(action_id, {}).get("events", [])
        )
        if state == "deleted":
            deleted.append(
                {
                    "context": context,
                    "action": action_id,
                    "default_keys": default_keys,
                }
            )
        elif state == "overridden":
            overrides.append(
                {
                    "context": context,
                    "action": action_id,
                    "default_keys": default_keys,
                    "user_keys": events_label(
                        merged.get(context, {}).get(action_id, {}).get("events", [])
                    ),
                }
            )

    collisions = find_collisions(merged)
    if collision_contexts is not None:
        collisions = [row for row in collisions if row["context"] in collision_contexts]

    required = []
    for action_id in REQUIRED_INVENTORY_ACTIONS:
        keys = events_label(effective_events(merged, REQUIRED_CONTEXT, action_id))
        required.append(
            {
                "context": REQUIRED_CONTEXT,
                "action": action_id,
                "keys": keys,
                "missing": keys == "NO KEY",
            }
        )

    def user_unbound(context: str, action_id: str) -> bool:
        if states.get((context, action_id)) == "deleted":
            return True
        if states.get((context, action_id)) == "overridden":
            action = merged.get(context, {}).get(action_id)
            return action is not None and not action["events"]
        return False

    missing_required = []
    for entry in required:
        action_id = entry["action"]
        present = action_id in merged.get(REQUIRED_CONTEXT, {}) or action_id in merged.get(
            DEFAULT_CONTEXT, {}
        )
        removed = user_unbound(REQUIRED_CONTEXT, action_id) or user_unbound(
            DEFAULT_CONTEXT, action_id
        )
        if not present or (entry["missing"] and removed):
            missing_required.append(action_id)

    return {
        "merged": merged,
        "states": states,
        "user_objects": user_objects,
        "overrides": overrides,
        "deleted": deleted,
        "collisions": collisions,
        "required": required,
        "missing_required": missing_required,
    }


def _row(*fields: str) -> str:
    return "\t".join(fields)


def print_tsv(result: dict) -> None:
    print(TSV_HEADER)
    for row in result["overrides"]:
        print(
            _row(
                "override",
                row["context"],
                row["action"],
                row["default_keys"],
                row["user_keys"],
                row["user_keys"],
                "user file replaces the default keys"
                if row["user_keys"] != "NO KEY"
                else "user file leaves the action unbound",
            )
        )
    for row in result["deleted"]:
        print(
            _row(
                "deleted",
                row["context"],
                row["action"],
                row["default_keys"],
                "NO KEY",
                "NO KEY",
                "is_deleted: removed from this context",
            )
        )
    for row in result["collisions"]:
        print(
            _row(
                "collision",
                row["context"],
                ",".join(row["actions"]),
                "",
                "",
                row["events"],
                row["detail"],
            )
        )
    for row in result["required"]:
        print(
            _row(
                "required",
                row["context"],
                row["action"],
                "",
                "",
                row["keys"],
                "NO KEY" if row["missing"] else "",
            )
        )


def print_summary(result: dict) -> None:
    contexts = len(result["merged"])
    print(f"contexts: {contexts}", file=sys.stderr)
    print(
        f"overrides: {len(result['overrides'])} action entries in "
        f"{len({row['context'] for row in result['overrides']})} contexts",
        file=sys.stderr,
    )
    print(f"deleted by the user file: {len(result['deleted'])}", file=sys.stderr)
    counts: dict[str, int] = defaultdict(int)
    for row in result["collisions"]:
        counts[row["context"]] += 1
    detail = ", ".join(f"{context}: {count}" for context, count in sorted(counts.items()))
    print(
        f"collisions: {len(result['collisions'])}"
        + (f" ({detail})" if detail else ""),
        file=sys.stderr,
    )
    no_key = sum(1 for row in result["required"] if row["missing"])
    print(
        f"required actions: {len(result['required'])} checked, {no_key} without a key",
        file=sys.stderr,
    )
    if result["missing_required"]:
        print(
            "missing required actions: " + ", ".join(result["missing_required"]),
            file=sys.stderr,
        )
    else:
        print("missing required actions: none", file=sys.stderr)


def default_user_candidates() -> list[Path]:
    """Config locations from src/utils/path_info.cpp::init_user_dir/set_standard_filenames."""
    candidates = []
    if os.name == "nt":
        local = os.environ.get("LOCALAPPDATA")
        if local:
            candidates.append(Path(local) / "cataclysm-bn" / "config" / "keybindings.json")
        roaming = os.environ.get("APPDATA")
        if roaming:
            candidates.append(Path(roaming) / "cataclysm-bn" / "config" / "keybindings.json")
    else:
        xdg_config = os.environ.get("XDG_CONFIG_HOME")
        home = os.environ.get("HOME")
        if xdg_config:
            candidates.append(Path(xdg_config) / "cataclysm-bn" / "keybindings.json")
        if home:
            candidates.append(Path(home) / ".config" / "cataclysm-bn" / "keybindings.json")
            candidates.append(
                Path(home) / ".local" / "share" / "cataclysm-bn" / "config" / "keybindings.json"
            )
            candidates.append(Path(home) / ".cataclysm-bn" / "config" / "keybindings.json")
            candidates.append(
                Path(home)
                / "Library"
                / "Application Support"
                / "Cataclysm-BN"
                / "config"
                / "keybindings.json"
            )
    candidates.append(Path("config") / "keybindings.json")
    return candidates


def find_default_user() -> Path | None:
    for candidate in default_user_candidates():
        if candidate.is_file():
            return candidate
    return None


def run_selftest() -> int:
    """Exercise the merge, collision, deletion, and required-action paths."""
    failures = []

    def check(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)

    with tempfile.TemporaryDirectory(prefix="cdf_kbcheck_") as tmp:
        root = Path(tmp)
        defaults_dir = root / "defaults"
        defaults_dir.mkdir()

        def kb(
            action_id: str,
            keys: list[str],
            category: str | None = None,
            deleted: bool = False,
        ) -> dict:
            obj = {
                "type": "keybinding",
                "id": action_id,
                "bindings": [{"input_method": "keyboard", "key": key} for key in keys],
            }
            if category is not None:
                obj["category"] = category
            if deleted:
                obj["is_deleted"] = True
            return obj

        defaults = [
            kb("RESET_FILTER", ["r", "R"]),
            kb("NEXT_COLUMN", ["h", "LEFT", "4"], "INVENTORY"),
            kb("PREV_COLUMN", ["TAB"], "INVENTORY"),
            kb("TOGGLE_ENTRY", ["l"], "INVENTORY"),
            kb("MARK_WITH_COUNT", ["!"], "INVENTORY"),
            kb("TOGGLE_NON_FAVORITE", [","], "INVENTORY"),
            kb("EXAMINE_CONTENTS", ["o"], "INVENTORY"),
            kb("SHOW_HIDE_CONTENTS", [">"], "INVENTORY"),
            kb("SHOW_HIDE_CONTENTS_ALL", [], "INVENTORY"),
            kb("OTHER_ACTION", ["t"], "INVENTORY"),
        ]
        (defaults_dir / "keybindings.json").write_text(
            json.dumps(defaults), encoding="utf-8"
        )
        user = [
            kb("NEXT_COLUMN", ["j"], "INVENTORY"),
            kb("TOGGLE_ENTRY", [], "INVENTORY", deleted=True),
            kb("PREV_COLUMN", ["t"], "INVENTORY"),
            kb("IGNORED_EMPTY", [], "DEFAULTMODE"),
        ]
        user_path = root / "user.json"
        user_path.write_text(json.dumps(user), encoding="utf-8")

        result = analyze(defaults_dir, user_path)

        next_column = [r for r in result["overrides"] if r["action"] == "NEXT_COLUMN"]
        check(len(next_column) == 1, "NEXT_COLUMN override is reported")
        if next_column:
            check(
                next_column[0]["default_keys"] == "h;LEFT;4",
                "NEXT_COLUMN default keys are the shipped keys",
            )
            check(next_column[0]["user_keys"] == "j", "NEXT_COLUMN user key is reported")

        deleted = [r for r in result["deleted"] if r["action"] == "TOGGLE_ENTRY"]
        check(len(deleted) == 1, "TOGGLE_ENTRY deletion is reported")
        check(
            ("DEFAULTMODE", "IGNORED_EMPTY") in result["states"]
            and result["states"][("DEFAULTMODE", "IGNORED_EMPTY")] == "ignored",
            "an empty user entry for an unknown local action is ignored (input.cpp:277-280)",
        )

        collisions = result["collisions"]
        check(
            any(
                row["context"] == "INVENTORY"
                and set(row["actions"]) == {"OTHER_ACTION", "PREV_COLUMN"}
                and row["events"] == "t"
                for row in collisions
            ),
            "the t collision in INVENTORY is reported",
        )

        required = {row["action"]: row["keys"] for row in result["required"]}
        check(required.get("NEXT_COLUMN") == "j", "required NEXT_COLUMN uses the user key")
        check(required.get("TOGGLE_ENTRY") == "NO KEY", "deleted TOGGLE_ENTRY is NO KEY")
        check(
            required.get("SHOW_HIDE_CONTENTS_ALL") == "NO KEY",
            "SHOW_HIDE_CONTENTS_ALL is NO KEY in the defaults",
        )
        check(
            result["missing_required"] == ["TOGGLE_ENTRY"],
            "only the deleted TOGGLE_ENTRY is reported missing",
        )

    if failures:
        for failure in failures:
            print(f"selftest FAIL: {failure}", file=sys.stderr)
        return 1
    print("selftest: PASS (merge, override, deletion, collision, required list)")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Report user keybindings that override, delete, or collide with the defaults."
    )
    parser.add_argument(
        "--defaults",
        type=Path,
        default=Path(__file__).resolve().parent.parent / "data" / "raw" / "keybindings",
        help="directory with the shipped keybindings (*.json); default: data/raw/keybindings",
    )
    parser.add_argument(
        "--user",
        type=Path,
        default=None,
        help="the user's config/keybindings.json; default: the usual config directories",
    )
    parser.add_argument(
        "--contexts",
        default="INVENTORY,ADVANCED_INVENTORY",
        help="comma-separated contexts to check for key collisions, or 'all' "
        "(default: INVENTORY,ADVANCED_INVENTORY, the contexts the DDA inventory keys live in)",
    )
    parser.add_argument(
        "--selftest",
        action="store_true",
        help="run the built-in merge/override/deletion/collision checks and exit",
    )
    args = parser.parse_args(argv)

    if args.selftest:
        return run_selftest()

    if not args.defaults.is_dir():
        print(f"error: defaults directory not found: {args.defaults}", file=sys.stderr)
        return 2

    user_path = args.user
    if user_path is not None:
        if not user_path.is_file():
            print(f"error: user keybindings file not found: {user_path}", file=sys.stderr)
            return 2
    else:
        user_path = find_default_user()
        if user_path is None:
            print(
                "note: no user keybindings.json found; checking the defaults only",
                file=sys.stderr,
            )
        else:
            print(f"note: using user keybindings {user_path}", file=sys.stderr)

    try:
        collision_contexts = None
        if args.contexts.strip().lower() != "all":
            collision_contexts = {
                context.strip() for context in args.contexts.split(",") if context.strip()
            }
        result = analyze(args.defaults, user_path, collision_contexts)
    except (OSError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    print_tsv(result)
    print_summary(result)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
