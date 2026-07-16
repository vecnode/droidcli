#pragma once

#include "core/types.hpp"
#include "export.hpp"

namespace droidcli::reliability {

// The model has the real Desktop path in its system prompt (see
// SystemInfo::desktop_path, cli/system_info.hpp, injected in
// DroidHost::initialize()) but has been observed - repeatedly and
// reproducibly, not a one-off - writing a bare relative "desktop/..." or
// "desktop\..." token instead of using it (e.g. "-y ... desktop/red.png"),
// which can never succeed: no such directory exists relative to droidcli's
// own working directory. Rather than let a command that's guaranteed to
// fail reach the shell/ffmpeg, substitute the real resolved Desktop path in
// for a bare "desktop" token wherever one starts a path (preceded by
// whitespace, a quote, nothing, a single leading "/"/"\", or a leading
// "./"/".\"). Deliberately does NOT touch an already-correct absolute path
// like "C:\Users\...\Desktop\..." - there the character immediately before
// "Desktop" is a path separator, not a word boundary, so it never matches
// (see looks_like_invented_desktop_path below for the structural check that
// catches an *invented* absolute-looking path this can't).
DROIDCLI_API core::String substitute_bare_desktop_token(const core::String& args, const core::String& desktop_path);

// droidcli is a personal desktop assistant, not a build tool - a file/
// command reference with no location information at all should default to
// somewhere a human would actually go looking for it, the real Desktop, not
// wherever droidcli's own process happened to be launched from. A real
// transcript showed exactly this: asked to save an image "to Desktop," the
// model wrote a bare "output.png" with no directory at all, which landed in
// droidcli's own working directory (the git repo it was built from)
// instead. Only a truly bare name - no '/' or '\' anywhere in it - counts
// as "no location specified"; anything with any directory information at
// all, even a relative one ("subdir/x.png", "./x.png"), is left untouched
// as the caller having already specified where. Composes with
// substitute_bare_desktop_token() above, not a replacement for it: that one
// resolves an explicit "desktop/..." reference to the real path, this one
// covers the case where "desktop" was never mentioned at all.
DROIDCLI_API core::String default_bare_filename_to_desktop(const core::String& path, const core::String& desktop_path);

// Catches a distinct failure mode substitute_bare_desktop_token doesn't
// cover: a model inventing an entire plausible-looking *template* path -
// "/path/to/Desktop/green_image.png" - rather than a bare relative token.
// That specific string is the generic placeholder convention used in
// documentation/examples everywhere; a model that's seen a million code
// samples reaches for it when it doesn't actually know the real path,
// instead of calling list_dir/stat_path to find out. Also catches
// "/Users/username/..." - the same documentation-convention placeholder,
// just a different one (the generic "username" a man page/Stack Overflow
// answer uses in place of a real one). Checked as its own guard, at the
// tool-execution layer, on any path argument a filesystem tool receives.
DROIDCLI_API bool looks_like_placeholder_path(const core::String& path);

// A structural safety net beyond substitute_bare_desktop_token's own pattern
// coverage: that function only rewrites specific recognized token shapes
// and deliberately leaves anything that already looks like a real absolute
// path untouched, since "Desktop" preceded by more path is indistinguishable
// from an already-correct "C:\Users\...\Desktop\..." by pattern alone. But a
// model can still type an absolute-*looking* path with its own invented
// "desktop" segment that was never one of those recognized shapes at all -
// e.g. a literal "C:\desktop\hello" (root-level, not the real per-user
// Desktop) - and nothing catches that. There is exactly one real Desktop
// location; checked here, after substitution has already run, on the fully
// resolved path: any "desktop" path segment (bounded by separators, so this
// doesn't false-positive on a real folder that merely contains "desktop" as
// part of a longer name) that ISN'T the real, resolved desktop_path prefix
// is never legitimate, full stop - there is no second "desktop" folder
// anywhere on the machine that a path should ever be rooted at.
DROIDCLI_API bool looks_like_invented_desktop_path(const core::String& resolved_path, const core::String& real_desktop_path);

// A real transcript showed write_file "successfully" create two garbage
// image files: once with the literal ffmpeg command line as file content,
// once with the literal placeholder string "<base64 encoded blue 512x512
// pixel PNG image data>" - write_file has no way to generate real binary
// image/media data (its content argument is exact text, not a renderer), but
// nothing stopped it from writing obviously-fake bytes to a path that claims
// to be one anyway. Checked by file signature, not extension alone: a
// path ending in a known binary/image extension must have content that
// actually starts with that format's real magic bytes, or the write is
// rejected before it happens (same "reject before touching disk" pattern as
// looks_like_placeholder_path above) with guidance toward run_ffmpeg, the
// tool that can actually produce real image/media bytes. A path with no
// recognized extension, or an extension not in this list, is never checked -
// this only catches the specific "claims to be a real media file, isn't"
// shape, not a general content-quality judgment.
DROIDCLI_API bool looks_like_mismatched_binary_content(const core::String& path, const core::String& content);

} // namespace droidcli::reliability
