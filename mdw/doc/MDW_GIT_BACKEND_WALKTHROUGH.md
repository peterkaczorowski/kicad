# The Git Reference Backend

A narrated tour of `plugins/mdw/git`, the reference MDW backend that maps a git
working tree onto the workspace model. It exists to be read: it is small, it
advertises exactly one capability group, and it shows how to drive an external
CLI safely from a wxWidgets-free backend. Read the
[author guide](MDW_BACKEND_AUTHOR_GUIDE.md) first for the SDK mechanics.

Files:

- `kicad_mdw_git.cpp` — `main`, the descriptor, and every handler.
- `git_operations.{h,cpp}` — thin wrappers over the git CLI.
- `org.kicad.mdw.git.kicad-mdw.json` — the manifest.

## The model

Folders are directories, items are tracked files, and revision history is the
git log of a file. The mapping is deliberately literal:

- an item's `item_id` is its repo-relative path
- its `folder_id` is the parent directory (empty string at the root)
- its `revision_id` is the short hash of the last commit that touched it
- `modified_unix` is that commit's author time

The backend links `kiapi` and `nng` only, exactly like the SDK. It uses no git
library; it shells out to the `git` CLI through `MDW_RunProcess`.

## Configuring the repository

`GitRepo()` reads the working tree from the `KICAD_MDW_GIT_REPO` environment
variable, falling back to `.` (the process working directory) when it is unset
or empty:

```cpp
std::string GitRepo()
{
    const char* repo = std::getenv( "KICAD_MDW_GIT_REPO" );

    if( repo && repo[0] != '\0' )
        return std::string( repo );

    return std::string( "." );
}
```

Every git invocation is routed through `Git()`, which prefixes
`{ "git", "-C", GitRepo() }` onto the argument vector and runs it via
`MDW_RunProcess` (no shell, explicit argv):

```cpp
MDW_PROCESS_RESULT Git( const std::vector<std::string>& aArgs )
{
    std::vector<std::string> argv = { "git", "-C", GitRepo() };
    argv.insert( argv.end(), aArgs.begin(), aArgs.end() );
    return MDW_RunProcess( argv );
}
```

`MDW_PROCESS_RESULT` reports `started` (false if the process could not be
spawned), `exit_code`, `out`, and `err`. Handlers check `started &&
exit_code == 0` before trusting output.

## Robustness choices

Three deliberate choices make the git output stable to parse:

- **`-z` / NUL framing.** `ls-files -z` and `log -z` separate records with NUL
  bytes instead of newlines, so a filename or commit subject containing a newline
  cannot be mistaken for a record boundary. `Split( out, '\0' )` recovers the
  records. Within a `log` record, fields are joined with `%x1f` (ASCII Unit
  Separator, `FIELD_SEP = '\x1f'`), a byte that cannot appear in a path or a
  commit hash.
- **`core.quotePath=false`.** By default git octal-escapes non-ASCII bytes in
  paths. `GitLsFiles` and the revision log pass `-c core.quotePath=false` so
  paths come back as literal UTF-8, matching the `item_id`s KiCad sends.
- **`LC_ALL=C` / `LANG=C`.** `main` sets the C locale (non-Windows) before any
  git call, so git's diagnostic text is stable. This matters for `Checkin`,
  which distinguishes "nothing to commit" by substring match; a localized git
  message would break that test.

```cpp
#if !defined( _WIN32 )
    setenv( "LC_ALL", "C", 1 );
    setenv( "LANG", "C", 1 );
#endif
```

## Security validation

Two guards in `git_operations.cpp` sanitize every request-supplied string before
it reaches git:

- `IsSafeRef` rejects empty, a leading `-` (so a revision id cannot masquerade
  as a flag), and any character outside `[0-9A-Za-z._/-]`.
- `IsSafePath` rejects empty, a leading `-`, an absolute path, and any `..` path
  component (so `item_id` cannot escape the working tree).

Every handler that consumes an `item_id` or `revision_id` guards it first and
returns `WE_ITEM_NOT_FOUND` (path) or `WE_INVALID_PARAMETER` (ref) on rejection.
Where a path is still passed to git, it is passed after a `--` separator, so even
a validated path cannot be reinterpreted as an option.

## The descriptor

`buildDescriptor` advertises `CG_REVISIONS` and nothing else:

```cpp
aDescriptor.info.set_backend_id( "org.kicad.mdw.git" );
aDescriptor.info.set_display_name( "Git Workspace" );
aDescriptor.info.mutable_version()->set_major( 1 );
aDescriptor.info.mutable_version()->set_minor( 0 );
aDescriptor.capabilities.add_groups( ws::CG_REVISIONS );
aDescriptor.schema.set_schema_version( "1" );
```

Because only `CG_REVISIONS` is advertised and only its handlers plus the
mandatory surface are registered, the SDK auto-answers `WE_NOT_SUPPORTED` for
every other group. No events are emitted, so it passes `nullptr` for the
publisher rather than paying for a socket and thread it never uses.

## The handlers

### Authentication

`Authenticate` is a local, always-succeeding identity: it returns
`authenticated = true` with `user_id` and `display_name` read from
`git config user.email` / `user.name` via `configValue`, which falls back to
`"local"` when a key is unset. `IsAuthenticated` returns true; `Logout` returns
`ok`. There is no real credential exchange — the working tree is the trust
boundary.

### Navigation — from `git ls-files`

All navigation derives from one command: `GitLsFiles` runs
`git -c core.quotePath=false ls-files -z` and splits on NUL. Its `aOk` out-param
distinguishes an empty repo from a failed git call, so handlers return
`WE_INTERNAL "git failed"` only on real failure.

- `GetRootFolders` returns the single root folder (id `""`), and sets
  `has_children` if any tracked path contains a `/`.
- `GetChildFolders` walks the file list, and for the requested `folder_id`
  prefix, collects the distinct next-level directory components (deduped through
  a `std::set`), setting `has_children` on each that has a deeper subdirectory.
- `ListItems` returns every tracked file whose `parentDir` equals the requested
  `folder_id`, each built by `itemInfo`.
- `GetItemInfo` validates the path, confirms `GitTracked` (via
  `git ls-files --error-unmatch -- <path>`), then returns `itemInfo`.

`itemInfo` fills revision and mtime from the last commit touching the path:
`git log -1 --format=%h%x1f%at -- <path>`, split on the Unit Separator into short
hash and author timestamp.

### Checkout / checkin

- `Checkout` validates the path, confirms it is tracked, and returns the item
  plus a `local_path`. When the request supplies a `local_path` it is echoed;
  otherwise the backend returns `GitRepo() + "/" + item_id` — the in-tree file,
  since a git working tree is already the local copy. It takes no lock (git has
  no per-file lock).
- `Checkin` stages and commits the one file:
  `git add -- <path>` then `git commit -m <comment> -- <path>`. If the commit
  output contains `"nothing to commit"` it returns `WE_NO_CHANGES`; any other
  commit failure is `WE_INTERNAL` carrying git's stderr. On success it reads
  `git rev-parse --short HEAD` and returns a `Revision` with that id, the
  configured author email, and the comment.
- `CancelCheckout` discards local edits with
  `git checkout -- <path>`, returning `WE_INTERNAL` on failure.

### Revisions — `CG_REVISIONS`

- `GetRevisionHistory` validates the path and runs
  `git -c core.quotePath=false log -z --format=%H%x1f%P%x1f%an%x1f%at%x1f%s
  -- <path>`. Records are NUL-separated; each is split into exactly five fields
  by `splitFields` (which takes the remainder as the final field so an empty
  commit subject is preserved). The parent list (`%P`) is space-split into
  `parent_revision_ids`, so merge history survives. Fields become the
  `Revision`'s id, parents, author, timestamp, and comment.
- `DownloadRevision` validates both `revision_id` (`IsSafeRef`) and `item_id`
  (`IsSafePath`), then runs `git show <revision>:<path>` and writes the captured
  bytes to the requested `local_path` (binary, truncating). A failed `git show`
  is `WE_REVISION_NOT_FOUND`; a write failure is `WE_INTERNAL`.

## Parsing helpers

`git_operations.cpp` provides the small text utilities the handlers lean on:
`Trimmed` (strip surrounding whitespace), `Split` (split on a delimiter, dropping
a trailing empty record — used with `'\0'`, `'\x1f'`, and `' '`), and, in
`kicad_mdw_git.cpp`, `splitFields` (fixed-count split preserving an empty
trailing field). Together with `-z`, `%x1f`, and the C locale, these keep the
parse robust against paths and commit messages that contain spaces, newlines, or
non-ASCII bytes.
