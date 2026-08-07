# NR-047 — Search also matches the shortcut's resolved target name

Phase 3 · Depends on: NR-038

- Source: `docs/design-spec.md` §4.4（搜尋正規化）／§4.5（排名規則）／§FR-004（Start Menu 項目）／§FR-006（Store／封裝 App）／§10.1（資料位置與格式）

## Why

Today `SearchApps` matches one string per entry: `AppEntry::normalized_name`,
derived from `display_name`, which for a Start Menu item is just the shortcut's
file stem (§FR-004: 「顯示名稱預設採捷徑檔名，不含副檔名」). A user with a
localized Windows therefore has to type `計算機` to reach `計算機.lnk`, even
though the thing it launches is `calc.exe` and `calc` is what they would type.

The information needed to fix this is **already computed and then thrown away**:
`start_menu_catalog.cpp` resolves `link.target` and feeds it only into the stable
id (§10.3), and `appsfolder_catalog.cpp` already stores the AUMID in
`source_path`. This item keeps a small, searchable slice of that — the target's
file stem, or the AUMID's package-family part — as a **secondary** match key
that ranks strictly below every name match.

## Decisions already made — do not reopen

Confirmed by the user for this item:

1. **No user-defined aliases / nicknames.** The user considered a per-item
   "set a nickname" feature and chose target-name matching alone. A nickname
   feature needs an edit UI, a new `%LOCALAPPDATA%` store with its own
   retention rules for absent apps, and a settings-page entry point — a whole
   feature for a need this item already covers. It is **out of scope**; do not
   add a field, a store, a menu command, or a settings control for it.

Decided while writing this item (rationale in Non-goals):

2. The match key is the target's **file stem only**, never the full path.
3. A target match is a **single, lowest** ranking tier, below `subsequence`.
4. `catalog.cache` schema goes 1 → 2 and an older cache is simply rebuilt, not
   quarantined as corrupt.
5. UserFolder entries get no alias, because that enumerator deliberately does
   not resolve shortcut targets.

## Binding constraints — quoted, do not go looking for them

design-spec §4.4:

> 比對前應：
> - Unicode 大小寫不敏感。
> - 去除頭尾空白。
> - 將連續空白視為一個空白。
> - 同時保留原始名稱與正規化名稱。
> - MVP 不移除變音符號，不做中文拼音、注音或同義詞展開。

design-spec §4.5 (**this item extends it — see Scope §6**):

> 搜尋分數由高到低：
> 1. 完全相同。
> 2. 名稱前綴相同。
> 3. 任一單字前綴相同。
> 4. 連續子字串相同。
> 5. 字元依序匹配（subsequence）。
>
> 同分時依序比較：
> 1. 已釘選優先。
> 2. 使用分數較高者優先。
> 3. 名稱較短者優先。
> 4. 以不區分大小寫的名稱排序，確保結果穩定。

design-spec §FR-004:

> - 顯示名稱預設採捷徑檔名，不含副檔名。
> - `.lnk` 以 `IShellLinkW`／`IPersistFile` 解析，禁止自行解析二進位格式。
> - 無法解析但 Shell 可正常開啟的捷徑仍可保留，啟動時交給 Shell。

design-spec §4.3:

> - 每次輸入變更後同步計算；若未來 Catalog 超過 5,000 筆或量測超標，再改用背景工作執行緒。

design-spec §10.3 (why the target must stay out of identity work):

> - Start Menu 項目：以正規化 Shell parsing identity／resolved target 加必要參數產生 SHA-256 或穩定雜湊表示。
> - stable ID 不可依顯示名稱、圖示或目前排序產生。

AGENTS.md:

- Prefer the smallest working change. Reuse existing code before adding helpers
  or abstractions.
- Keep search, ranking, scoring, persistence formats, and other core logic
  independent of HWND and Shell COM objects where practical.
- Keep App Catalog data as ordinary copyable values.
- Do not overwrite user data in place. Use temporary files and atomic
  replacement for persistent writes.
- New non-trivial logic needs one focused runnable test or self-check.
- Keep changes scoped to the requested task and update the relevant
  documentation when behavior changes.

## Files to read and trace first

`docs/design-spec.md` and `src/app_host/main.cpp` had uncommitted changes when
this item was written, but **this item touches neither**. Line numbers below are
navigation hints; the code snippets and function names are the specification.

- `src/catalog/app_entry.h:15-24` — `AppEntry`. §1 appends **one** field.
  **Append it after `usage_score`, last in the struct.** `tests/unit/search_engine_test.cpp:16-20`
  uses designated initializers that name every field in declaration order; a
  field appended last leaves those lines compiling byte-identically, a field
  inserted anywhere else breaks them.
- `src/catalog/start_menu_catalog.cpp:88-117` — `ResolveShortcut`, which already
  produces `link.target`.
- `src/catalog/start_menu_catalog.cpp:119-160` — `ProcessFile`. `link.target` is
  currently consumed only by the `identity_key` block (lines 146-157). §2 adds
  one line **before** that block and must not change it: the stable id is
  §10.3's business and this item must not perturb it.
- `src/catalog/appsfolder_catalog.cpp:54-80` — `BuildAppsFolderEntry`.
  `parsing_name` is the AUMID; `source_path` already holds it verbatim and
  `launch_identity` is `shell:AppsFolder\` + it. §3 adds one line.
- `src/catalog/user_folder_catalog.cpp:76-94` — `ProcessFile`, including the
  existing `ponytail:` comment stating that `.lnk` targets are **not** resolved
  here because that needs Shell COM at scan time. This item does not change
  that, so UserFolder entries have an empty alias. Do not add COM here.
- `src/catalog/catalog_refresh.cpp:114-125` — `SetSnapshot`, "the sole place a
  published snapshot gets its normalized names". §4 adds one line here.
  **This is the only normalization point; do not add a second.**
- `src/catalog/catalog_cache.cpp:20-24,68-81,145-176` — schema constant, field
  count, serialize, parse. §5 changes all four.
- `src/catalog/dedup.cpp:49` — near-duplicate merge compares `display_name`.
  **The alias must not enter this comparison**; two different apps can share a
  target stem (a launcher and its `--config` variant) and merging them would
  lose an entry.
- `src/search/search_engine.cpp:37-77` — `MatchRank` and `Rank(name, query)`.
  §6 adds one enum value and calls the **unchanged** `Rank` a second time.
- `src/search/search_engine.cpp:121-178` — `SearchApps`, the per-keystroke scan
  and the tie-break comparator. The comparator does **not** change.
- `src/app_host/panel_model.cpp` — consumes `SearchApps` output as opaque
  `AppEntry` values. **No change needed anywhere in `src/app_host/`.**
- `tests/unit/start_menu_catalog_test.cpp:27,67-88,133-155,176-209` — `Expect`,
  the `CreateShortcut` helper, the fixture shortcuts (note the existing
  `計算機.lnk`) and the assertion block. §8 adds one fixture and assertions.
- `tests/unit/catalog_refresh_test.cpp` — the only test that exercises
  `SaveCatalogCache`/`LoadCatalogCache` and `SetSnapshot`. §8 adds assertions.
- `CMakeLists.txt:118-121` — `nimblerun_catalog` already links
  `nimblerun_search` PUBLIC for exactly this reason (NR-038 normalization).
  **No CMake change is needed in this item.**

## Scope

### 1. One new `AppEntry` field, appended last

In `src/catalog/app_entry.h`, after `usage_score`:

```cpp
    // NR-047: secondary search key -- the resolved target's file stem for a
    // Start Menu shortcut, or the AUMID's package-family part for a packaged
    // app. Empty when the source cannot supply one. Stored raw by the
    // enumerators and normalized once in CatalogRefreshCoordinator::SetSnapshot,
    // exactly like normalized_name. Never part of the stable id (design-spec
    // §10.3) and never part of dedup.
    std::wstring search_alias;
```

Appended last on purpose — see Files to read.

### 2. Start Menu: the target's file stem

In `start_menu_catalog.cpp::ProcessFile`, immediately after
`entry.launch_identity = path;` and **before** the `identity_key` block:

```cpp
    // NR-047: the resolved target's stem is a secondary search key, so a
    // localized shortcut name ("計算機.lnk") is still reachable by what it
    // actually launches ("calc"). Stem only, never the full path (see the item's
    // Non-goals). Empty for an unresolvable target, which stays in the catalog.
    if (!link.target.empty()) {
        entry.search_alias = FileStem(link.target);
    }
```

`FileStem` is already declared in `catalog/app_filter.h`, which this file already
includes. No new helper, and no branch to suppress an alias that duplicates the
display name — a duplicate can only ever match in the lowest tier, where a name
match already outranks it.

A bare `.exe` in a Programs folder has no `link`, so its alias stays empty; its
`display_name` is already the stem.

### 3. AppsFolder: the AUMID's package-family part

In `appsfolder_catalog.cpp::BuildAppsFolderEntry`, next to
`out.source_path = parsing_name;`:

```cpp
    // NR-047: the package-family part of the AUMID is a secondary search key
    // ("Microsoft.WindowsCalculator" reachable by "calc"). Cut at the first '_'
    // to drop the publisher hash, which is identical across every Store app and
    // would otherwise make queries like "8wekyb" return the whole Store.
    out.search_alias = parsing_name.substr(0, parsing_name.find(L'_'));
```

`find` returning `npos` yields the whole string, which is the correct behavior
for an AUMID without an underscore.

### 4. Normalize in the one existing place

In `CatalogRefreshCoordinator::SetSnapshot`, inside the existing loop:

```cpp
        // NR-047: unconditional, unlike normalized_name -- an empty alias is a
        // legitimate value (UserFolder, unresolvable target), so there is no
        // "only when empty" test to make. NormalizeName is idempotent, so
        // re-normalizing the already-normalized value the disk cache carries is
        // a no-op.
        entry.search_alias = NormalizeName(entry.search_alias);
```

### 5. `catalog.cache` schema 1 → 2

In `catalog_cache.cpp`:

- `kSchemaVersion` → `2`.
- `kFieldCount` → `7`, and extend its comment with `search_alias`.
- `SerializeEntry`: append `L'\t'` + `EscapeText(entry.search_alias)` **after**
  `SourceNumber(entry.source)`, so the new field is last and the existing five
  escaped fields keep their positions.
- The parse loop: `entry.search_alias = UnescapeText(fields[6]);`. An empty
  field is valid — do **not** add it to the `stable_id.empty()` validity test.
- The older-schema arm currently quarantines the file:

  ```cpp
  if (schema != kSchemaVersion) {
      PreserveCorrupt(directory, kFileName);   // <-- replace this
      return false;
  }
  ```

  Replace the body with a bare `return false;` plus:

  ```cpp
  // NR-047: an older schema is a valid file this build cannot read, not a
  // corrupt one. Leave it in place and rebuild over it, matching the
  // newer-schema arm above; quarantining every user's cache on a routine
  // schema bump produces confusing .corrupt files for a non-event.
  ```

No migration code: the cache is a rebuildable snapshot, and
`CatalogRefreshCoordinator` already re-enumerates when the load returns false.

### 6. Search: one lowest tier for alias matches

In `search_engine.cpp`, extend the enum — alias sits **below** `Subsequence`:

```cpp
enum class MatchRank : int {
    Exact = 0,
    NamePrefix = 1,
    WordPrefix = 2,
    Substring = 3,
    Subsequence = 4,
    Alias = 5,    // NR-047: matched the target/AUMID, not the name
    NoMatch = 6,
};
```

`Rank(name, query)` itself is **unchanged**. In the `SearchApps` scan loop, after
the existing `Rank` call:

```cpp
        MatchRank rank = Rank(name, normalized_query);
        // NR-047: the target name is a fallback, never a competitor. It is only
        // consulted when the display name does not match at all, and every hit
        // collapses to one tier below subsequence, so no target match can ever
        // outrank a name match and the §4.5 order among name matches is
        // untouched. How the alias matched is deliberately not preserved:
        // ranking target matches against each other by tier would promote a
        // vague name match to above a precise one.
        if (rank == MatchRank::NoMatch && !catalog[i].search_alias.empty() &&
            Rank(catalog[i].search_alias, normalized_query) != MatchRank::NoMatch) {
            rank = MatchRank::Alias;
        }
```

The tie-break comparator (pinned → usage → shorter name → normalized name →
stable id) is **not** touched: within the `Alias` tier it applies unchanged.

The three conditions are ordered cheapest-first on purpose: the `MatchRank`
comparison is free, `.empty()` is a load, and only then does the second `Rank`
run. Do not reorder them.

Adding `Alias = 5` shifts `NoMatch` from 5 to 6. `MatchRank` lives in
`search_engine.cpp`'s anonymous namespace and no code outside compares it to a
literal, so nothing else has to change — confirm with the `Select-String` check
in Agent checks before assuming it.

### 7. Performance: measure the path this item actually adds

The extra `Rank` call runs **only** for entries whose display name did not match
at all, so its cost is inversely proportional to the result-set size:

- Short/common query (`e`): subsequence matching accepts nearly everything, so
  `NoMatch` is rare and almost no entry pays the second call. Big result set,
  near-zero added cost.
- Long/specific query: most entries reach `NoMatch`, so most pay the second
  call — up to ~2× the scan. Tiny result set, and the second `Rank` runs over an
  alias (a file stem, typically under 16 characters) rather than a display name,
  so it is the cheaper of the two calls.

The worst case is therefore "no name matches, every entry has an alias", and
**the existing perf assertion does not cover it**: `search_engine_test.cpp`'s
5000-entry block queries `L"e"`, which matches all 5000, so `NoMatch` never
occurs and the new branch is never entered. Add a second timing block next to it
rather than editing the first one (the `L"e"` case is NR-038's regression guard
for per-keystroke normalization and must keep measuring exactly what it measures
today):

- Same 5000 entries, but give every one a `search_alias` (e.g.
  `L"target" + std::to_wstring(i)`).
- Query a string that cannot match any display name as a subsequence, so every
  entry falls through to the alias check — with names shaped `app <n> edition`,
  a query of `L"zzqx"` does it. **Assert the result set is empty**, otherwise the
  test silently stops measuring the worst path if the fixture names ever change.
- Same 50 ms ceiling as the existing block, and the same
  `std::wprintf` line so the number lands in `ctest --output-on-failure` output
  and a future reader can compare the two paths without re-deriving them.

If that block ever measures within 2× of the ceiling, the fix is **not** to make
the alias check conditional or to cache per-query state: it is the incremental
narrowing already described in `SearchApps`'s existing `ponytail:` comment, which
applies to the alias tier for the same reason it applies to the name tiers (a
longer query's match set is a subset of the shorter one's, per tier). Do not
implement narrowing in this item.

Memory: one `std::wstring` per catalog entry. A typical alias (`calc`,
`notepad`) fits inside the small-string buffer and costs no allocation; a long
AUMID stem allocates once per entry at enumeration. `SearchApps` already copies
whole `AppEntry` values into its result vector, so the per-result cost of this
item is at most one more string copy on a path that already copies five. Not
worth restructuring — and explicitly **not** a reason to hold the alias as a
`wstring_view` into another field, which would make `AppEntry` non-trivially
copyable in the "ordinary copyable values" sense AGENTS.md requires.

### 8. Tests

**`tests/unit/search_engine_test.cpp`** — first, a scoped cleanup that removes
the trap §1 has to work around. The four fixture entries at lines 16-20 name
*every* `AppEntry` member in a designated-initializer list, including the ones
they set to `L""` or `false`, which is why any field not appended last breaks
them. C++20 lets a designated-initializer list omit members (it may not reorder
them), and every omitted member here already has a default member initializer,
so drop the members each entry does not actually care about:

```cpp
{.stable_id = L"notepad", .display_name = L"Notepad", .usage_score = 100},
```

That is less code, asserts the same thing, and means the *next* field added to
`AppEntry` needs no coordination with this file at all. Do not change the
`display_name`/`usage_score`/`is_pinned` values — the `  CAL  ` ordering
assertion depends on them.

Then extend the fixture with an
entry whose `display_name` cannot match the query but whose `search_alias` can
(the item's motivating case: a Chinese display name over `calc.exe`), then
assert:

- Querying the alias finds the entry.
- With both an alias-matching entry and a name-matching entry present, **every**
  name match sorts before **every** alias match, including the deliberately
  awkward case: a name `subsequence` match must still beat an alias `exact`
  match.
- An entry with an empty `search_alias` is unaffected.
- A query matching the display name returns the same order as before this item
  (the existing `  CAL  ` and `3d` assertions must pass unchanged).
- `search_alias` is compared as given — the test supplies pre-normalized
  lowercase values, mirroring what `SetSnapshot` produces.

**`tests/unit/start_menu_catalog_test.cpp`** — add one fixture that is the
user's actual case, next to the existing `計算機.lnk`:

```cpp
    // NR-047: a localized shortcut name over an English target; "calc" must be
    // reachable through the target stem.
    Expect(CreateShortcut(root + L"\\小算盤.lnk", L"C:\\Windows\\System32\\calc.exe", L""),
           "create localized calculator shortcut");
```

and assertions:

- The new entry's `search_alias == L"calc"` and its `display_name == L"小算盤"`.
- `Notepad.lnk`'s `search_alias == L"notepad"`.
- The bare-`.exe` fixture entry's `search_alias` is empty.
- The existing `Expect(entries.size() == 6, ...)` becomes `== 7`. Check the
  current value in the working tree rather than trusting this number.
- `Notepad.lnk` and `Notepad Copy.lnk` still share a stable id — proof §2 did
  not disturb the §10.3 identity path.

**`tests/unit/catalog_refresh_test.cpp`** —

- `SetSnapshot` normalizes `search_alias` (feed `L"  CALC  "`, expect `L"calc"`)
  and leaves an empty one empty.
- A `SaveCatalogCache` → `LoadCatalogCache` round trip preserves
  `search_alias`, including an entry whose alias is empty and one whose alias
  contains a tab or backslash (proving it goes through `EscapeText`).
- Writing a `schema=1` file by hand and loading it returns `false` **and leaves
  no `.corrupt` file** in the directory (§5's last bullet).

No new test executable and no `tests/CMakeLists.txt` change: all three files are
already built and registered.

### 9. Update the spec

`docs/design-spec.md` §4.4, after the existing bullets, add:

> - 除名稱外另保留一組次要比對鍵：Start Menu 捷徑取其 resolved target 的檔名主體（不含路徑與副檔名），封裝 App 取 AUMID 的 package family 部分（第一個 `_` 之前）。使用者自訂資料夾項目不解析捷徑目標，故無次要鍵。次要鍵與名稱採同一套正規化。

§4.5, after tier 5（`字元依序匹配`）, add tier 6:

> 6. 名稱完全不匹配，但次要比對鍵（§4.4）匹配。

and immediately after the numbered list:

> 次要鍵命中一律歸為最低一層，不按其命中方式細分；任何名稱命中永遠優先於任何次要鍵命中。

`§10.1`: if it states the `catalog.cache` schema version or field list, update it
to schema 2 with the trailing `search_alias` field; if it does not, add nothing.

Keep Traditional Chinese and the surrounding bullet style. Do not touch any other
clause. `docs/design-spec.md` has an uncommitted change from other work — do not
revert it, and commit only your own hunks.

## How this stays maintainable

Read this before "improving" the shape above; each point is a deliberate choice
about where knowledge lives.

**One search key per concept, one normalization point.** `normalized_name` is the
primary key and `search_alias` is the secondary key, and both are normalized in
exactly one place: `CatalogRefreshCoordinator::SetSnapshot`. The failure mode
this prevents is silent: an un-normalized alias keeps its original casing, never
equals a lowercased query, and the item simply stops being findable with no error
anywhere. §8's `SetSnapshot` assertion is the guard. **If you add a catalog
source later, you fill `search_alias` in the enumerator and touch nothing else** —
that is the whole extension contract, and it is the reason §2/§3 store the raw
value instead of normalizing at the call site.

**No abstraction over the two keys.** It is tempting to introduce a
`SearchKeys(entry)` accessor or a `std::vector<std::wstring> search_keys` so a
third key is "free". Don't: two keys with genuinely different ranking semantics
(one drives §4.5's five tiers, the other collapses to one) are not a list, and a
vector would add a heap allocation per entry plus an inner loop in the hot scan
to model a generality nothing has asked for. A third key, if it ever exists,
should force this decision to be re-made with a real second example in hand.

**The alias is inert outside search.** It is not in the stable id (§10.3), not in
dedup, not in the UI, and not in any tie-breaker. That is what makes it safe to
change how it is derived later — widening §2 from "target stem" to something else
can only change what is findable, never what is merged, launched, or persisted
under an identity. The Agent checks assert this mechanically; keep them.

**Schema bumps stop being destructive.** §5's change to the older-schema arm is
the reusable half of this item: from here on, any future `catalog.cache` field
addition is `kSchemaVersion` + `kFieldCount` + one serialize line + one parse
line, and old caches are rebuilt rather than quarantined. Copy that pattern; do
not add a migration path.

**The struct-field trap is removed, not documented around.** §1 appends the field
last because `search_engine_test.cpp` currently requires it; §8's cleanup deletes
that requirement so the *next* field has no such constraint. Both halves are
in scope — landing §1 without §8's cleanup leaves the trap armed for the next
person.

## Non-goals

- **User-defined aliases / nicknames.** Explicitly declined by the user for this
  item (Decisions §1).
- **Matching the full target path.** `C:\Program Files\…` would make `pro`,
  `file` and `program` match nearly every installed app, turning short queries
  into noise. The stem is the part a user knows.
- **Matching `launch_identity` or `source_path` directly.** For Start Menu and
  UserFolder those are the shortcut's own path, which adds the same directory
  noise; for AppsFolder `source_path` is the raw AUMID including the publisher
  hash. §2/§3 store exactly the useful slice instead.
- **Ranking alias matches against each other by tier.** One flat tier — a
  precise alias hit and a vague one sort by the existing tie-breakers, which is
  good enough and costs no new comparator code.
- **Showing the user *why* a row matched** (no target-name row, no match
  highlighting). §4.3 already puts the source path on every search-result row,
  which is enough to disambiguate.
- **Resolving `.lnk` targets in the UserFolder enumerator.** That is a separate
  item; the existing `ponytail:` comment there names the reason (Shell COM at
  scan time) and this item does not change it.
- **拼音／注音／同義詞展開** — §4.4 excludes it for the MVP.
- **Changing dedup**, the stable id, the tie-break comparator, the panel model,
  or anything under `src/app_host/`.
- **A cache migration path**, a settings toggle for target matching, or a new
  CMake target.

## Interaction with the other open items

- **NR-045 / NR-046** touch `src/app_host/main.cpp` and the grid paint loop only.
  This item touches no file they touch, so it can land in either order.
- **NR-038** established `SetSnapshot` as the single normalization point and
  `catalog_cache` field 3 as the persisted normalized name; §4/§5 follow that
  shape exactly rather than inventing a second one.

## Acceptance

Automated: the three test files in §8 must pass, including the new worst-path
timing block from §7.

Manual (Release build):

1. `Alt+Space`, type `calc`: on a localized Windows, the localized Calculator
   appears (through the AUMID or the shortcut target), below any app whose
   display name matches `calc`.
2. Type a query that matches a display name exactly (e.g. `notepad`): the
   ordering is identical to before this item. Nothing that used to be first has
   been displaced by a target match.
3. Delete `%LOCALAPPDATA%\NimbleRun\catalog.cache`, launch, and confirm the
   catalog rebuilds and target matching works on the rebuilt snapshot.
4. Keep a `schema=1` `catalog.cache` from a previous build in place, launch, and
   confirm the catalog rebuilds silently: no `.corrupt` file appears next to it
   and no error dialog is shown.
5. Type a single common letter (e.g. `e`) and confirm no visible input lag —
   this is the query that exercises the extra `Rank` call most often, since
   almost nothing reaches `NoMatch` on the name.

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "search_engine|start_menu_catalog|catalog_refresh" --output-on-failure
```

Build must produce no new warnings and the whole suite must stay green.

```powershell
# No CMake change, and no UI-layer change:
git diff CMakeLists.txt tests/CMakeLists.txt src/app_host/ src/ui/   # must be empty
# The alias never enters identity or dedup:
Select-String -Path src/catalog/dedup.cpp,src/catalog/stable_id.h -Pattern 'search_alias'
# expect: no match
# One normalization point:
Select-String -Path src/ -Pattern 'NormalizeName' -Recurse
# expect: the search_engine definition, catalog_refresh.cpp SetSnapshot only
# The schema bump is real:
Select-String -Path src/catalog/catalog_cache.cpp -Pattern 'kSchemaVersion = 2|kFieldCount = 7'
# expect: both
# Nothing outside search_engine.cpp knows about the tier values (NoMatch moved 5 -> 6):
Select-String -Path src/,tests/ -Pattern 'MatchRank' -Recurse
# expect: src/search/search_engine.cpp only
# The old cache is rebuilt, not quarantined:
Select-String -Path src/catalog/catalog_cache.cpp -Pattern 'PreserveCorrupt'
# expect: the decode/BOM, empty-lines, missing-prefix, unparsable-schema and
# bad-field arms only -- NOT the `schema != kSchemaVersion` arm
```

The worst-path timing number from §7 must appear in the test output; paste both
`SearchApps` timing lines into the handoff so the next reader can compare them:

```powershell
ctest --test-dir build -R search_engine --output-on-failure
```

## 交接區

（實作者填寫：修改的位置、建置與 CTest 結果、sanity greps、偏差、手動驗收、未完成事項。）

- **修改**：
  - `src/catalog/app_entry.h:24-31`：`AppEntry` 在 `usage_score` 後**附加最後一欄** `std::wstring search_alias`（含 NR-047 註解），其餘欄位與順序未動。
  - `src/catalog/start_menu_catalog.cpp:148-153`：`ProcessFile` 在 `entry.launch_identity = path;` 之後、`identity_key` block 之前，非空 `link.target` 時設 `entry.search_alias = FileStem(link.target)`；identity block（`identity_key`、`HashStableId`）一字未改。bare `.exe` 無 `link` 故 alias 為空。
  - `src/catalog/appsfolder_catalog.cpp:78`：`BuildAppsFolderEntry` 在 `out.source_path = parsing_name;` 旁設 `out.search_alias = parsing_name.substr(0, parsing_name.find(L'_'))`（AUMID 的 package-family 部分，無 `_` 時為整串）。
  - `src/catalog/catalog_refresh.cpp:126-130`：`SetSnapshot` 迴圈內新增**唯一的** alias 正規化點——無條件 `entry.search_alias = NormalizeName(entry.search_alias);`（空 alias 是合法值，故不像 normalized_name 有 empty 判斷）。
  - `src/catalog/catalog_cache.cpp`：`kSchemaVersion` 1→2（:22）、`kFieldCount` 6→7 並於註解列出 search_alias（:24-26）、`SerializeEntry` 在 `SourceNumber(entry.source)` 後 append `L'\t'`＋`EscapeText(entry.search_alias)`（:82）、parse 迴圈 `entry.search_alias = UnescapeText(fields[6])`（:182，空欄合法、不加入 `stable_id.empty()` 有效性判斷）、舊 schema arm 由 `PreserveCorrupt` 改為裸 `return false;`＋NR-047 註解（:151-157）。無遷移碼。另把 `WriteCache` 註解「cache version 1 is just merged entries」改為「the cache is just merged entries」（schema 已非 1，屬同改動的註解準確化）。
  - `src/search/search_engine.cpp`：`MatchRank` 於 `Subsequence = 4` 下、`NoMatch` 上插入 `Alias = 5`（:43，`NoMatch` 5→6）；`Rank(name, query)` 一字未改；`SearchApps` scan loop 在既有 `Rank` 呼叫後依序（`rank==NoMatch`→`.empty()`→第二次 `Rank`）三條件判斷，命中即 `rank = MatchRank::Alias`（:151-153），tie-break comparator 未動。
  - `tests/unit/search_engine_test.cpp`：先做 §8 cleanup（四個 fixture entry 改為只列各自需要的欄位，`display_name`／`usage_score`／`is_pinned` 值不變，`  CAL  ` 與 `3d` 斷言原樣通過）；新增 `alias_catalog` block（`計算機`＋`search_alias=L"calc"`，斷言查 `calc` 找到、name subsequence 的 Calculator 排在 alias exact 的計算機之前、空 alias 的 Paint 3D 只靠名稱命中）與「alias 原樣比對」block（`L"CALC"` 不被 `calc` 查到）；§7 新增第二個 timing block（5000 筆每筆給 `search_alias=L"target"+i`、查 `L"zzqx"`、斷言結果為空、同 50ms ceiling、同 wprintf 格式），既有 `L"e"` block 一字未改。檔頭加 scoped `#pragma clang diagnostic ignored "-Wmissing-designated-field-initializers"`（見偏差 2）。
  - `tests/unit/start_menu_catalog_test.cpp`：新增 `小算盤.lnk` → `C:\Windows\System32\calc.exe` fixture（在既有 `計算機.lnk` 旁）；斷言其 `display_name==L"小算盤"`、`search_alias==L"calc"`、Notepad.lnk 的 `search_alias==L"notepad"`、bare `.exe`（Portable）alias 為空、`Notepad.lnk`／`Notepad Copy.lnk` 仍共用 stable id、entry 數 6→7。
  - `tests/unit/catalog_refresh_test.cpp`：新增 `TestSetSnapshotNormalizesSearchAlias`（`  CALC  `→`calc`、空 alias 保持空）、`TestCacheRoundTripSearchAlias`（含空 alias 與 `foo\tbar\\baz` escaping 往返）、`TestOlderSchemaCacheRebuilds`（手寫 `schema=1` 載入回 false、原檔保留、無 `.corrupt`）；皆已註冊進 `wmain()`。
  - `docs/design-spec.md`：§4.4 新增次要比對鍵 bullet（:178）、§4.5 新增 tier 6 與「次要鍵命中一律歸為最低一層…」句（:188-190）。§10.1 只列出檔名、未載明 cache schema／欄位清單，依 item §9「若無則不加」不加任何內容。
- **建置與 CTest**：`cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release` configure 成功；`cmake --build build` 成功、**無新增警告**（LLVM-MinGW `-Wall -Wextra -Wpedantic` 全清，見偏差 2 的 pragma）；`ctest --test-dir build --output-on-failure` **23/23 全綠**；`ctest --test-dir build -R "start_menu_catalog|catalog_refresh" --output-on-failure` 2/2 通過（`search_engine` 不是測試名，見報告註記）。`ctest --test-dir build -R nimblerun_search_test` 1/1 通過。
- **SearchApps 兩條 timing（test 直接執行輸出）**：
  - `NR-038: SearchApps over 5000 pre-normalized entries took 603 us (0 ms), matched 5000`
  - `NR-047: SearchApps over 5000 alias-fallback entries took 204 us (0 ms), matched 0`
  - 最壞路徑（名稱全不命中＋每筆有 alias）204 us，遠低於 50ms ceiling，且比既有 5000 筆 `L"e"` 的 603 us 更短（alias 短於 display name，第二次 `Rank` 較便宜）。
- **Sanity greps**：
  - `git diff CMakeLists.txt tests/CMakeLists.txt src/app_host/ src/ui/` → **空**（無 CMake 變更、無 UI 層變更）。
  - `Select-String -Path src/catalog/dedup.cpp,src/catalog/stable_id.h -Pattern 'search_alias'` → **無 match**（alias 不進 identity、不進 dedup）。
  - `NormalizeName`：只有 `src/search/search_engine.cpp:88`（定義）、`search_engine.cpp:129`（每 keystroke 的 query 正規化，NR-038 既有）與 `catalog_refresh.cpp:121,125,128`（SetSnapshot block，含新的 alias 一行）→ **符合**「search_engine 定義＋catalog_refresh SetSnapshot 唯一正規化點」。
  - `src/catalog/catalog_cache.cpp` 的 `kSchemaVersion = 2`（:22）與 `kFieldCount = 7`（:24）→ **兩者皆在**。
  - `MatchRank`（`src/`＋`tests/`）→ 只出現在 `src/search/search_engine.cpp` → **符合**（`NoMatch` 5→6 不外洩）。
  - `src/catalog/catalog_cache.cpp` 的 `PreserveCorrupt` → 6 處全部是 decode/BOM、empty-lines、missing-prefix、unparsable-schema、bad-field-count、bad-field 六個 arm；`schema != kSchemaVersion` arm 為裸 `return false;` → **符合**。其他 `src/pins/`、`src/settings/`、`src/usage/` 的 `PreserveCorrupt` 屬各 store 既有行為，不在 item 檢查範圍。
- **與 item 文件的偏差（2 處必要調整，設計決策零偏差）**：
  1. §8 的 motivating entry（`計算機`＋`search_alias=L"calc"`）未放入共享四筆 fixture，而是放在獨立的 `alias_catalog` block。原因：`  CAL  ` 查詢正規化為 `cal`，是 alias `calc` 的 NamePrefix，會被新的 fallback 撈成第三筆，直接打破 §8 自己要求的「`  CAL  ` 斷言不變」。放在獨立 catalog（比照既有 `zebra` block 模式）後，§8 五個 bullet 全部照字面成立：查 alias 找得到、name subsequence 勝 alias exact、空 alias 不受影響、`  CAL  `／`3d` 原樣、alias 原樣比對。
  2. §8 cleanup 要求的省略式 designated-initializer 清單在此工具鏈（LLVM-MinGW 的 clang）會觸發 `-Wmissing-designated-field-initializers`（`-Wextra` 內建），與「no new warnings」衝突，且 item 禁止動 `tests/CMakeLists.txt`。解法：在 `search_engine_test.cpp` 加 scoped `#pragma clang diagnostic ignored`（檔頭 push、檔尾 pop）＋解釋註解。cleanup 形狀與 item 範例完全一致。
- **手動驗收（5 條）**：全部為人工操作／視覺驗證（launch、目視排序、刪 cache、留舊 schema cache、輸入延遲），依 `docs/work-items.md`「Agent 交付規則」由人類在 Release 版逐條執行：1) 在地化 Windows 打 `calc` 出地化 Calculator 且排在任一名稱命中之下、2) 打 `notepad` 排序與 item 前一致、3) 刪 `%LOCALAPPDATA%\NimbleRun\catalog.cache` 後重建且 target 比對可用、4) 保留 `schema=1` cache 啟動後靜默重建、無 `.corrupt` 無錯誤框、5) 打單一字母 `e` 無可見輸入延遲。
- **未完成**：無。5 條手動驗收留待人類於 Release 版逐條打勾。自動化面已由三份測試檔全數覆蓋。
